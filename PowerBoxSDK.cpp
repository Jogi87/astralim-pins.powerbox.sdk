/* *******************************************************************************
 * MIT License
 *
 * Copyright (c) 2026 Nico Trost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * **************************************************************************** */

#include "PowerBoxSDK.h"
#include "PowerBoxLogging.h"
#include "PowerBoxDevice.h"
#include "PowerBoxProtocol.h"
#include "PowerBoxSerialPort.h"
#include "arduino_base64.hpp"
#include <map>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <cmath>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cctype>
#include <mutex>
#ifdef __unix__
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <dirent.h>
#include <libudev.h>
#elif defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#endif

#define SDK_VERSION "1.0.0"

/* Handshake retry configuration */
#define HANDSHAKE_MAX_RETRIES 5
#define HANDSHAKE_RETRY_DELAY_MS 200
#define HANDSHAKE_TIMEOUT 1000

/* Import internal implementation for use in public C API */
using namespace PowerBox;

/* ============================================================================
 * ENCRYPTION HELPER FUNCTIONS
 * ============================================================================ */

// Generate AES key and IV from UUID (matching device's GenerateAesKey)
static void GenerateAesKeyFromUUID(const char* uuid, uint8_t* key, uint8_t startIndex)
{
    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    // Parse hex string to binary bytes first
    uint8_t binaryUuid[16] = {0};
    int byteIdx = 0;
    int charIdx = 0;
    
    // Skip dashes and parse hex pairs
    while (charIdx < (int)strlen(uuid) && byteIdx < 16)
    {
        char c = uuid[charIdx];
        
        if (c == '-')
        {
            charIdx++;
            continue;
        }
        
        // Parse two hex characters
        if (charIdx + 1 < (int)strlen(uuid))
        {
            char nextc = uuid[charIdx + 1];
            if (nextc != '-')
            {
                std::string hexPair = std::string(1, c) + std::string(1, nextc);
                binaryUuid[byteIdx++] = (uint8_t)strtol(hexPair.c_str(), nullptr, 16);
                charIdx += 2;
                continue;
            }
        }
        charIdx++;
    }
    
    // Now convert bytes [startIndex : startIndex+8] to hex ASCII representation
    // This matches device's GenerateAesKey function
    int keyPos = 0;
    for (uint8_t i = startIndex; i < startIndex + 8 && i < 16; i++)
    {
        // High nibble
        key[keyPos++] = "0123456789ABCDEF"[binaryUuid[i] >> 4];
        // Low nibble
        key[keyPos++] = "0123456789ABCDEF"[binaryUuid[i] & 0x0F];
    }
}

// Encrypt password using AES-128-CBC with PKCS7 padding
static bool EncryptPassword(const char* plainPassword, const char* uuid, char* encryptedBase64Output)
{
    if (!plainPassword || !uuid || !encryptedBase64Output)
        return false;
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    // Generate key and IV from UUID
    uint8_t aesKey[16];
    uint8_t aesIv[16];
    
    GenerateAesKeyFromUUID(uuid, aesKey, 0);
    GenerateAesKeyFromUUID(uuid, aesIv, 8);

    // Allocate buffer for encrypted data (plaintext + block size for padding)
    int plainLen = strlen(plainPassword);
    std::vector<uint8_t> encryptedBytes(plainLen + EVP_MAX_BLOCK_LENGTH);
    int encryptedLen = 0;
    int tempLen = 0;

    // Encrypt
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, aesKey, aesIv))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (!EVP_EncryptUpdate(ctx, encryptedBytes.data(), &tempLen, (const uint8_t*)plainPassword, plainLen))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    encryptedLen = tempLen;

    if (!EVP_EncryptFinal_ex(ctx, encryptedBytes.data() + encryptedLen, &tempLen))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    encryptedLen += tempLen;

    EVP_CIPHER_CTX_free(ctx);

    // Base64 encode the encrypted bytes
    size_t encodedLen = base64::encodeLength(encryptedLen);
    base64::encode(encryptedBytes.data(), encryptedLen, encryptedBase64Output);
    encryptedBase64Output[encodedLen] = '\0';

    return true;
}

/* Helper function to send a command and wait for the response with timeout */
static bool SendAndWaitForReply(std::shared_ptr<PowerBox::Device> device, 
                                const char *command,
                                std::mutex &configMutex,
                                std::condition_variable &configCV,
                                std::atomic<bool> &configPending,
                                const char *timeoutMsg,
                                int timeoutMs = 1000)
{
    {
        std::lock_guard<std::mutex> lock(configMutex);
        configPending = true;
    }

    if (!device->port->Write((const unsigned char *)command, strlen(command)))
    {
        PB_DEBUG("SendAndWaitForReply: Failed to send %s command", command);
        std::lock_guard<std::mutex> lock(configMutex);
        configPending = false;
        return false;
    }

    /* Wait for config to be received with specified timeout */
    {
        std::unique_lock<std::mutex> lock(configMutex);
        configCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&configPending]() { return !configPending; });
        if (configPending)
        {
            PB_DEBUG("SendAndWaitForReply: Timeout waiting for %s (timeout=%dms)", timeoutMsg, timeoutMs);
            configPending = false;
            return false;
        }
    }

    return true;
}

/* Helper function to send a command and wait for the response with retry mechanism */
static bool SendAndWaitForReplyWithRetry(std::shared_ptr<PowerBox::Device> device,
                                         const char *command,
                                         std::mutex &configMutex,
                                         std::condition_variable &configCV,
                                         std::atomic<bool> &configPending,
                                         const char *timeoutMsg,
                                         int timeoutMs = 1000,
                                         int maxRetries = HANDSHAKE_MAX_RETRIES,
                                         int retryDelayMs = HANDSHAKE_RETRY_DELAY_MS)
{
    for (int attempt = 1; attempt <= maxRetries; ++attempt)
    {
        PB_DEBUG("SendAndWaitForReplyWithRetry: Attempt %d/%d for %s (timeout=%dms)", attempt, maxRetries, timeoutMsg, timeoutMs);

        if (SendAndWaitForReply(device, command, configMutex, configCV, configPending, timeoutMsg, timeoutMs))
        {
            PB_DEBUG("SendAndWaitForReplyWithRetry: Success on attempt %d for %s", attempt, timeoutMsg);
            return true;
        }

        if (attempt < maxRetries)
        {
            PB_DEBUG("SendAndWaitForReplyWithRetry: Failed on attempt %d, retrying after %d ms", attempt, retryDelayMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
        }
    }

    PB_DEBUG("SendAndWaitForReplyWithRetry: All %d attempts failed for %s", maxRetries, timeoutMsg);
    return false;
}

/* ============================================================================
 * PUBLIC SDK API IMPLEMENTATION
 * ============================================================================ */

PBAPI PB_ERROR_TYPE PBGetSDKVersion(char *version)
{
    if (!version)
    {
        return PB_ERROR_NULL_POINTER;
    }

    strncpy(version, SDK_VERSION, PB_VERSION_LEN - 1);
    version[PB_VERSION_LEN - 1] = '\0';
    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBScan(int *number, int *ids)
{
    if (!number || !ids)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    // Stop telemetry and listener threads on all currently open devices
    for (auto &pair : g_devices)
    {
        auto device = pair.second;
        if (device && device->isOpen)
        {
            SendCommand(device, ":ES#");
            StopStatusListener(device);
        }
    }

    int count = 0;

#ifdef __unix__
    /* Create udev context */
    struct udev *udev = udev_new();
    if (!udev)
    {
        return PB_ERROR_COMMUNICATION;
    }

    /* Create enumeration for tty devices */
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (!enumerate)
    {
        udev_unref(udev);
        return PB_ERROR_COMMUNICATION;
    }

    /* Filter for tty subsystem */
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;

    char response[64];

    /* Iterate through all tty devices */
    udev_list_entry_foreach(entry, devices)
    {
        if (count >= PB_MAX_NUM)
            break;

        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *device = udev_device_new_from_syspath(udev, path);
        if (!device)
        {
            continue;
        }

        /* Get the parent USB device */
        struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
            device, "usb", "usb_device");

        if (!parent)
        {
            udev_device_unref(device);
            continue;
        }

        /* Check VID and PID for CP210x (10c4:ea60) */
        const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
        const char *pid = udev_device_get_sysattr_value(parent, "idProduct");

        if (!vid || !pid)
        {
            udev_device_unref(device);
            continue;
        }

        PB_DEBUG("Found device with VID:%s PID:%s", vid, pid);

        if (strcmp(vid, "10c4") != 0 || strcmp(pid, "ea60") != 0)
        {
            udev_device_unref(device);
            continue;
        }

        /* Get the device node (e.g., /dev/ttyUSB0) */
        const char *deviceNode = udev_device_get_devnode(device);
        if (!deviceNode)
        {
            udev_device_unref(device);
            continue;
        }

        PB_DEBUG("Trying to open device: %s", deviceNode);

        /* Try to open the port */
        auto port = std::make_shared<SerialPort>();
        if (port->Open(deviceNode))
        {
            PB_DEBUG("Port opened, flushing and sending command...");

            auto tempDevice = std::make_shared<Device>();
            tempDevice->port = port;
            tempDevice->portName = deviceNode;

            // Send HS to wake up device
            SendCommand(tempDevice, ":HS#", 200);

            // Start status listener thread
            StartStatusListener(tempDevice);

            // Perform handshake with retry mechanism
            if(SendAndWaitForReplyWithRetry(tempDevice, ":HS#", tempDevice->handshakeMutex, tempDevice->handshakeCV,
                                            tempDevice->handshakePending, "handshake"))
            {
                PB_DEBUG("Valid device found!");

                /* Stop listener */
                StopStatusListener(tempDevice);

                /* Valid device found - close port, will be reopened in PBOpen */
                port->Close();
                int id = count;
                g_devices[id] = tempDevice;
                ids[count] = id;
                count++;
            }
            else
            {
                PB_DEBUG("No response from device after handshake retries");
                /* Not a valid device, close port */
                port->Close();
            }
        }
        else
        {
            PB_DEBUG("Failed to open port %s", deviceNode);
        }

        udev_device_unref(device);
    }

    /* Clean up udev resources */
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
#elif defined(_WIN32)
    PB_DEBUG("PBScan: Enumerating serial ports on Windows");

    // Enumerate devices in the Ports class and look for USB devices with matching VID/PID
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        PB_DEBUG("SetupDiGetClassDevs failed");
        *number = 0;
        return PB_SUCCESS;
    }

    for (DWORD idx = 0; count < PB_MAX_NUM; ++idx)
    {
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(devInfo);
        if (!SetupDiEnumDeviceInfo(hDevInfo, idx, &devInfo))
        {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS)
                break;
            else
                continue;
        }

        // Try to get the device instance ID (contains VID/PID for USB-serial)
        char instanceId[512] = {0};
        if (!SetupDiGetDeviceInstanceIdA(hDevInfo, &devInfo, instanceId, (DWORD)sizeof(instanceId), NULL))
        {
            // ignore
        }

        // Look for VID_10C4 and PID_EA60 in instance ID (case-insensitive)
        bool isTarget = false;
        if (instanceId[0])
        {
            std::string iid(instanceId);
            for (auto &c : iid) c = (char)toupper((unsigned char)c);
            if (iid.find("VID_10C4") != std::string::npos && iid.find("PID_EA60") != std::string::npos)
            {
                isTarget = true;
            }
        }

        if (!isTarget)
            continue;

        // Try to extract COM port name. First try PortName from device registry, fallback to FriendlyName
        char portName[128] = {0};

        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE)
        {
            DWORD type = 0;
            DWORD cb = (DWORD)sizeof(portName);
            if (RegQueryValueExA(hKey, "PortName", NULL, &type, (LPBYTE)portName, &cb) == ERROR_SUCCESS)
            {
                // portName now contains e.g. "COM3"
            }
            RegCloseKey(hKey);
        }

        if (!portName[0])
        {
            // Fallback: get friendly name and parse (e.g., "USB-SERIAL CH340 (COM3)")
            char friendly[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly, (DWORD)sizeof(friendly), NULL))
            {
                char *p = strstr(friendly, "(COM");
                if (p)
                {
                    char *q = strchr(p, ')');
                    if (q && q > p)
                    {
                        size_t len = (size_t)(q - p - 1); // skip '(' and ')'
                        if (len < sizeof(portName))
                        {
                            // p points to "(COM3" so copy from p+1
                            strncpy(portName, p + 1, len);
                            portName[len] = '\0';
                        }
                    }
                }
            }
        }

        if (!portName[0])
            continue; // can't determine COM port

        PB_DEBUG("Found target device instance=%s port=%s", instanceId, portName);

        // Try to open the port and perform handshake
        std::string deviceNode = std::string(portName); // SerialPort_win handles COM prefix for CreateFile
        auto port = std::make_shared<SerialPort>();
        if (port->Open(deviceNode.c_str()))
        {
            PB_DEBUG("Port opened, flushing and sending command...");

            auto tempDevice = std::make_shared<Device>();
            tempDevice->port = port;
            tempDevice->portName = deviceNode;

            // Send HS to wake up device
            SendCommand(tempDevice, ":HS#", 200);

            // Start status listener thread
            StartStatusListener(tempDevice);

            // Perform handshake with retry mechanism
            if(SendAndWaitForReplyWithRetry(tempDevice, ":HS#", tempDevice->handshakeMutex, tempDevice->handshakeCV,
                                            tempDevice->handshakePending, "handshake"))
            {
                PB_DEBUG("Valid device found!");

                /* Stop listener */
                StopStatusListener(tempDevice);

                /* Valid device found - close port, will be reopened in PBOpen */
                port->Close();
                int id = count;
                g_devices[id] = tempDevice;
                ids[count] = id;
                count++;
            }
            else
            {
                PB_DEBUG("No response from device after handshake retries");
                port->Close();
            }
        }
        else
        {
            PB_DEBUG("Failed to open port %s", portName);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
#endif

    *number = count;
    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBOpen(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);
    PB_DEBUG("PBOpen: Opening device id=%d", id);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        PB_ERROR("PBOpen: Device id=%d not found", id);
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    PB_DEBUG("PBOpen: Found device, portName=%s", device->portName.c_str());

    /* Create a new SerialPort instance and open it */
    if (!device->port)
    {
        PB_DEBUG("PBOpen: Creating new SerialPort instance");
        device->port = std::make_shared<SerialPort>();
    }

    PB_DEBUG("PBOpen: Attempting to open port %s", device->portName.c_str());
    if (!device->port->Open(device->portName.c_str()))
    {
        PB_ERROR("PBOpen: Failed to open port");
        return PB_ERROR_COMMUNICATION;
    }

    PB_DEBUG("PBOpen: Port opened successfully, performing handshake");

    // Send HS to wake up device
    SendCommand(device, ":HS#", 200);

    // Start status listener thread
    StartStatusListener(device);

    // Perform handshake with retry mechanism
    if(!SendAndWaitForReplyWithRetry(device, ":HS#", device->handshakeMutex, device->handshakeCV,
                                     device->handshakePending, "handshake"))
    {
        PB_ERROR("PBOpen: Handshake failed after retries");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch ENV config
    if(!SendAndWaitForReply(device, ":GES#", device->envModelMutex, device->envModelCV,
                            device->envModelPending, "environment model"))
    {
        PB_ERROR("PBOpen: Failed to fetch environment model");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch update rate
    if(!SendAndWaitForReply(device, ":GUR#", device->updateRateMutex, device->updateRateCV,
                            device->updateRatePending, "update rate"))
    {
        PB_ERROR("PBOpen: Failed to fetch update rate");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch PWR config
    if(!SendAndWaitForReply(device, ":GPS#", device->pwrConfigMutex, device->pwrConfigCV,
                            device->pwrConfigPending, "power port config"))
    {
        PB_ERROR("PBOpen: Failed to fetch power port config");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch USB config
    if(!SendAndWaitForReply(device, ":GUS#", device->usbConfigMutex, device->usbConfigCV, 
                            device->usbConfigPending, "usb port config"))
    {
        PB_ERROR("PBOpen: Failed to fetch usb port config");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch DEW config
    if(!SendAndWaitForReply(device, ":GDS#", device->dewConfigMutex, device->dewConfigCV,
                            device->dewConfigPending, "dew port config"))
    {
        PB_ERROR("PBOpen: Failed to fetch dew port config");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch ADJ config
    if(!SendAndWaitForReply(device, ":GAS#", device->adjConfigMutex, device->adjConfigCV,
                            device->adjConfigPending, "adj port config"))
    {
        PB_ERROR("PBOpen: Failed to fetch adj port config");
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch WiFi info
    if(!SendAndWaitForReply(device, ":GWI#", device->wifiInfoMutex, device->wifiInfoCV,
                            device->wifiInfoPending, "WiFi info"))
    {
        PB_ERROR("PBOpen: Failed to fetch WiFi info");
        return PB_ERROR_COMMUNICATION;
    }

    // Start telemetry
    if (!SendCommand(device, ":BS#"))
    {
        PB_ERROR("PBOpen: Failed to start telemetry");
        StopStatusListener(device);
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    device->isOpen = true;
    PB_INFO("[OK] Device opened");
    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBClose(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    // Stop telemetry
    if (device->port && device->port->IsOpen())
    {
        SendCommand(device, ":ES#");
    }

    StopStatusListener(device);

    if (device->port)
    {
        device->port->Close();
    }

    device->isOpen = false;
    PB_INFO("[OK] Device closed");
    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetSerial(int id, char *serial)
{
    if (!serial)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    strncpy(serial, device->serial.c_str(), PB_VERSION_LEN - 1);
    serial[PB_VERSION_LEN - 1] = '\0';

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetConfig(int id, PB_DEVICE_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    config->temperatureOffset = device->temperatureOffset;
    config->humidityOffset = device->humidityOffset;
    config->envUpdateRate = device->envUpdateRate;
    config->updateRate = device->updateRate;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetConfig(int id, PB_DEVICE_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (config->mask & MASK_PB_TEMPERATURE_OFFSET)
    {
        if (config->temperatureOffset < -12.5f || config->temperatureOffset > 12.5f)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SET%f#", config->temperatureOffset);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->temperatureOffset = config->temperatureOffset;
    }

    if(config->mask & MASK_PB_HUMIDITY_OFFSET)
    {
        if(config->humidityOffset < -12.5f || config->humidityOffset > 12.5f)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SEH%f#", config->humidityOffset);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->humidityOffset = config->humidityOffset;
    }

    if(config->mask & MASK_PB_ENV_UPDATE_RATE)
    {
        if(config->envUpdateRate < 1 || config->envUpdateRate > 60)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SEU%d#", config->envUpdateRate);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->envUpdateRate = config->envUpdateRate;
    }

    if(config->mask & MASK_PB_UPDATE_RATE)
    {
        if(config->updateRate < 1 || config->updateRate > 60)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SUR%d#", config->updateRate);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->updateRate = config->updateRate;
    }

    if(config->mask & MASK_PB_EXT_TEMPERATURE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SCT%f#", config->temperature);

        if(!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->temperature = config->temperature;
    }

    if(config->mask & MASK_PB_EXT_HUMIDITY)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SCH%f#", config->humidity);

        if(!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->humidity = config->humidity;
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetPowerPortConfig(int id, PB_POWER_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx >= PB_NUM_POWER_PORTS)
    {
        return PB_ERROR_INVALID_PARAMETER;
    }

    config->enabled = device->powerState[idx] != 0;
    config->bootState = device->powerBootstrap[idx] != 0;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetPowerPortConfig(int id, PB_POWER_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx == 0 || idx >= PB_NUM_POWER_PORTS)
    {
        // Port 0 is always on and not configurable
        return PB_ERROR_INVALID_PARAMETER;
    }

    if (config->mask & MASK_PORT_ENABLE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SPS%d%d#", config->index, config->enabled != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->powerState[idx] = config->enabled != 0;
    }

    if(config->mask & MASK_PORT_BOOT_STATE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SPB%d%d#", config->index, config->bootState != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->powerBootstrap[idx] = config->bootState != 0;
    }

    if(config->mask & MASK_PORT_OVERCURRENT_RESET)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SPO%d#", config->index);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetUSBPortConfig(int id, PB_USB_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx >= PB_NUM_USB_PORTS)
    {
        return PB_ERROR_INVALID_PARAMETER;
    }

    config->enabled = device->usbState[idx] != 0;
    config->bootState = device->usbBootstrap[idx] != 0;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetUSBPortConfig(int id, PB_USB_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx >= PB_NUM_USB_PORTS)
    {
        return PB_ERROR_INVALID_PARAMETER;
    }

    if (config->mask & MASK_PORT_ENABLE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SUS%d%d#", config->index, config->enabled != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->usbState[idx] = config->enabled != 0;
    }

    if(config->mask & MASK_PORT_BOOT_STATE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SUB%d%d#", config->index, config->bootState != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->usbBootstrap[idx] = config->bootState != 0;
    }

    if(config->mask & MASK_PORT_OVERCURRENT_RESET)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SUO%d#", config->index);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetDewPortConfig(int id, PB_DEW_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx >= PB_NUM_DEW_PORTS)
    {
        return PB_ERROR_INVALID_PARAMETER;
    }

    config->enabled = device->dewState[idx] != 0;
    config->autoMode = device->dewAuto[idx] != 0;
    config->autoThreshold = device->dewThreshold[idx];
    config->power = device->dewPWM[idx];

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetDewPortConfig(int id, PB_DEW_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    auto idx = config->index;

    if(idx >= PB_NUM_DEW_PORTS)
    {
        return PB_ERROR_INVALID_PARAMETER;
    }

    // State and power is set with a single call, need to combine
    int newState = (config->mask & MASK_PORT_ENABLE) ? (config->enabled != 0) : device->dewState[idx];
    int newPower = (config->mask & MASK_PORT_POWER) ? config->power : device->dewPWM[idx];

    if (config->mask & MASK_PORT_ENABLE || config->mask & MASK_PORT_POWER)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SDS%d%d%d#", config->index, newState != 0, newPower);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->dewState[idx] = newState != 0;
        device->dewPWM[idx] = newPower;
    }

    if (config->mask & MASK_PORT_AUTO_DEW_MODE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SDB%d%d#", config->index, config->autoMode != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->dewAuto[idx] = config->autoMode != 0;
    }

    if (config->mask & MASK_PORT_AUTO_DEW_THRESHOLD)
    {
        if (config->autoThreshold < 0.0f || config->autoThreshold > 25.5f)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SDT%d%f#", config->index, config->autoThreshold);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->dewThreshold[idx] = config->autoThreshold;
    }

    if(config->mask & MASK_PORT_OVERCURRENT_RESET)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SDO%d#", config->index);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetBuckPortConfig(int id, PB_BUCK_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    // State and power is set with a single call, need to combine
    int newState = (config->mask & MASK_PORT_ENABLE) ? (config->enabled != 0) : device->buckState;
    int newTarget = ((config->mask & MASK_PORT_VOLTAGE) ? config->targetVoltage : device->buckVset) * 1000.0f;

    if (config->mask & MASK_PORT_ENABLE || config->mask & MASK_PORT_VOLTAGE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SAS0%d%d#", newState != 0, newTarget);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->buckState = newState != 0;
        device->buckVset = newTarget;
    }

    if (config->mask & MASK_PORT_BOOT_STATE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SAB0%d#", config->bootState != 0);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->buckBootstrap = config->bootState != 0;
    }

    if(config->mask & MASK_PORT_OVERCURRENT_RESET)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SAO0#");

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetPWMPortConfig(int id, PB_PWM_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    // State and power is set with a single call, need to combine
    int newState = (config->mask & MASK_PORT_ENABLE) ? (config->enabled != 0) : device->pwmState;
    int newPower = (config->mask & MASK_PORT_POWER) ? config->power : device->pwmPWM;

    if (config->mask & MASK_PORT_ENABLE || config->mask & MASK_PORT_POWER)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SAS1%d%d#", newState != 0, newPower);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->pwmState = newState != 0;
        device->pwmPWM = newPower;
    }

    if(config->mask & MASK_PORT_OVERCURRENT_RESET)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SAO1#");

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetBuckPortConfig(int id, PB_BUCK_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    config->enabled = device->buckState != 0;
    config->bootState = device->buckBootstrap != 0;
    config->targetVoltage = device->buckVset;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetPWMPortConfig(int id, PB_PWM_PORT_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    config->enabled = device->pwmState != 0;
    config->power = device->pwmPWM;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetStatus(int id, PB_DEVICE_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->upTime = device->upTime;
    status->temperature = device->temperature;
    status->humidity = device->humidity;
    status->dewPoint = device->dewPoint;
    status->extSensor = device->extSensor;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetSupplyStatus(int id, PB_SUPPLY_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->mainVoltage = device->supply12V;
    status->usbVoltage = device->supply5V;
    status->current = device->supply12A;
    status->ampereHours = device->supply12Ah;
    status->wattHours = device->supply12Wh;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetPowerPortStatus(int id, PB_POWER_PORT_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    for (int i = 0; i < PB_NUM_POWER_PORTS; ++i)
    {
        status->current[i] = device->powerCurrent[i];
        status->overcurrent[i] = device->powerOvercurrent[i];
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetUSBPortStatus(int id, PB_USB_PORT_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    for (int i = 0; i < PB_NUM_USB_PORTS; ++i)
    {
        status->current[i] = device->usbCurrent[i];
        status->voltage[i] = device->usbVoltage[i];
        status->overcurrent[i] = device->usbOvercurrent[i];
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetDewPortStatus(int id, PB_DEW_PORT_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->pwmResolution = device->dewPwmResolution;

    for (int i = 0; i < PB_NUM_DEW_PORTS; ++i)
    {
        status->current[i] = device->dewCurrent[i];
        status->overcurrent[i] = device->dewOvercurrent[i];
        status->probe[i] = device->dewProbe[i];
        status->pwm[i] = device->dewPWM[i];
        status->state[i] = device->dewState[i];
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetBuckPortStatus(int id, PB_BUCK_PORT_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->current = device->buckCurrent;
    status->voltage = device->buckVoltage;
    status->overcurrent = device->buckOvercurrent;
    status->vset = device->buckVset;
    status->vmin = device->buckVmin;
    status->vmax = device->buckVmax;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetPWMPortStatus(int id, PB_PWM_PORT_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->pwmResolution = device->pwmPwmResolution;
    status->current = device->pwmCurrent;
    status->overcurrent = device->pwmOvercurrent;
    status->pwm = device->pwmPWM;

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBRestart(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    if (!device || !device->port || !device->port->IsOpen())
    {
        return PB_ERROR_INVALID_STATE;
    }

    /* Send factory reset command */
    if (!SendCommand(device, ":RD#"))
    {
        return PB_ERROR_COMMUNICATION;
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBFactoryReset(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    if (!device || !device->port || !device->port->IsOpen())
    {
        return PB_ERROR_INVALID_STATE;
    }

    /* Send factory reset command */
    if (!SendCommand(device, ":FR#"))
    {
        return PB_ERROR_COMMUNICATION;
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetVersion(int id, PB_VERSION *version)
{
    if (!version)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;
    version->firmware = device->firmwareVersion;

    // Model
    strncpy(version->model, device->modelType.c_str(), sizeof(version->model) - 1);
    version->model[sizeof(version->model) - 1] = '\0';

    // UUID
    strncpy(version->uuid, device->uuid.c_str(), 37);

    // Serial
    strncpy(version->serial, device->serial.c_str(), 9);

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBScanWiFi(int id, PB_WIFI_SCAN_RESULT *result)
{
    if (!result)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    // Clear previous results and mark scan as pending
    {
        std::lock_guard<std::mutex> wifiLock(device->wifiScanMutex);
        device->wifiScanResult.count = 0;
        device->wifiScanPending = true;
    }

    // Ask for wifi survey
    if (device->port && device->port->IsOpen())
    {
        if(!SendCommand(device, ":GWS#"))
        {
            PB_ERROR("PBScanWiFi: Failed to start WiFi survey");
            std::lock_guard<std::mutex> wifiLock(device->wifiScanMutex);
            device->wifiScanPending = false;
            return PB_ERROR_COMMUNICATION;
        }
    }
    else
    {
        std::lock_guard<std::mutex> wifiLock(device->wifiScanMutex);
        device->wifiScanPending = false;
        return PB_ERROR_COMMUNICATION;
    }

    // Wait for WiFi scan results (with 10 second timeout)
    {
        std::unique_lock<std::mutex> wifiLock(device->wifiScanMutex);
        const auto timeout = std::chrono::seconds(10);
        bool completed = device->wifiScanCV.wait_for(wifiLock, timeout, [device]() {
            return !device->wifiScanPending;
        });

        if (!completed)
        {
            PB_ERROR("PBScanWiFi: Timeout waiting for WiFi scan results");
            device->wifiScanPending = false;
            return PB_ERROR_TIMEOUT;
        }

        // Copy results to output parameter
        *result = device->wifiScanResult;
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetWiFiStatus(int id, PB_WIFI_STATUS *status)
{
    if (!status)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    /* Return cached WiFi info from listener thread */
    {
        std::lock_guard<std::mutex> infoLock(device->wifiInfoMutex);
        strncpy(status->IP, device->wifiIP, PB_IP_LEN - 1);
        status->IP[PB_IP_LEN - 1] = '\0';
        status->rssi = device->wifiRSSI;
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBGetWiFiConfig(int id, PB_WIFI_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    /* Return cached WiFi info from listener thread */
    {
        std::lock_guard<std::mutex> infoLock(device->wifiInfoMutex);
        config->channel = device->wifiChannel;
        config->mode = device->wifiMode ? PB_WIFI_MODE_CLIENT : PB_WIFI_MODE_AP;
        strncpy(config->ssid, device->wifiSSID, PB_SSID_LEN - 1);
        config->ssid[PB_SSID_LEN - 1] = '\0';
        strncpy(config->hostname, device->wifiHostname, PB_HOSTNAME_LEN - 1);
        config->hostname[PB_HOSTNAME_LEN - 1] = '\0';
    }

    return PB_SUCCESS;
}

PBAPI PB_ERROR_TYPE PBSetWiFiConfig(int id, PB_WIFI_CONFIG *config)
{
    if (!config)
    {
        return PB_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return PB_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (!device->port || !device->port->IsOpen())
    {
        return PB_ERROR_COMMUNICATION;
    }

    // First, query current WiFi info if we need any cached values
    if (!(config->mask & (MASK_WIFI_SSID | MASK_WIFI_PASSWORD | MASK_WIFI_MODE)))
    {
        // No fields to update
        return PB_SUCCESS;
    }

    // Build and send the set WiFi info command
    // Format: :SWI<ssidlen><ssid><passlen><pass><mode>#
    // Always send the full command with cached values for fields not being updated
    
    char cmdBuffer[512];
    int cmdLen = 0;

    // Start command
    cmdLen = snprintf(cmdBuffer, sizeof(cmdBuffer), ":SWI");

    // Add SSID length and SSID
    const char *ssidToUse = (config->mask & MASK_WIFI_SSID) ? config->ssid : device->wifiSSID;
    int ssidLen = strlen(ssidToUse);
    cmdLen += snprintf(cmdBuffer + cmdLen, sizeof(cmdBuffer) - cmdLen, "%d%s", ssidLen, ssidToUse);

    // Add password length and password (AES encrypted and base64 encoded)
    const char *passToUse = (config->mask & MASK_WIFI_PASSWORD) ? config->pass : "";
    
    char encryptedPass[512];
    encryptedPass[0] = '\0';
    size_t encryptedLen = 0;
    
    if (strlen(passToUse) > 0)
    {
        // Encrypt password using device's UUID
        if (EncryptPassword(passToUse, device->uuid.c_str(), encryptedPass))
        {
            encryptedLen = strlen(encryptedPass);
            PB_DEBUG("WiFi Password: plaintext='%s', encrypted='%s', encryptedLen=%zu", passToUse, encryptedPass, encryptedLen);
        }
    }
    
    cmdLen += snprintf(cmdBuffer + cmdLen, sizeof(cmdBuffer) - cmdLen, "%zu%s", encryptedLen, encryptedPass);

    // Add mode
    int modeToUse = (config->mask & MASK_WIFI_MODE) ? config->mode : device->wifiMode;
    cmdLen += snprintf(cmdBuffer + cmdLen, sizeof(cmdBuffer) - cmdLen, "%d", modeToUse);

    // Terminate command
    snprintf(cmdBuffer + cmdLen, sizeof(cmdBuffer) - cmdLen, "#");

    if (!SendCommand(device, cmdBuffer))
    {
        PB_ERROR("PBSetWiFiConfig: Failed to send WiFi config command");
        return PB_ERROR_COMMUNICATION;
    }

    // Wait for device to echo back the updated WiFi info with timeout
    {
        std::lock_guard<std::mutex> infoLock(device->wifiInfoMutex);
        device->wifiInfoPending = true;
    }
    
    std::unique_lock<std::mutex> infoLock(device->wifiInfoMutex);
    if (!device->wifiInfoCV.wait_for(infoLock, std::chrono::seconds(2), 
                                      [device]() { return !device->wifiInfoPending; }))
    {
        PB_DEBUG("PBSetWiFiConfig: Timeout waiting for device WiFi response");
        device->wifiInfoPending = false;
    }

    // Update device cache with the new values that were sent
    if (config->mask & MASK_WIFI_SSID)
    {
        strncpy(device->wifiSSID, config->ssid, PB_SSID_LEN - 1);
        device->wifiSSID[PB_SSID_LEN - 1] = '\0';
    }

    if (config->mask & MASK_WIFI_MODE)
    {
        device->wifiMode = config->mode;
    }

    return PB_SUCCESS;
}
