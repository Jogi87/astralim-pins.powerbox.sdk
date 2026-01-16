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
#include <map>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cmath>
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

/* Import internal implementation for use in public C API */
using namespace PowerBox;

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

            if (QueryHandshake(tempDevice))
            {
                PB_DEBUG("Valid device found!");
                /* Valid device found - close port, will be reopened in PBOpen */
                port->Close();
                int id = count;
                g_devices[id] = tempDevice;
                ids[count] = id;
                count++;
            }
            else
            {
                PB_DEBUG("No response from device");
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

            if (QueryHandshake(tempDevice))
            {
                PB_DEBUG("Valid device found!");
                port->Close();
                int id = count;
                g_devices[id] = tempDevice;
                ids[count] = id;
                count++;
            }
            else
            {
                PB_DEBUG("No response from device");
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

    // Restart telemetry and listener threads on all currently open devices
    for (auto &pair : g_devices)
    {
        auto device = pair.second;
        if (device && device->isOpen)
        {
            StartStatusListener(device);
            SendCommand(device, ":BS#");
        }
    }

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

    /* Perform handshake */
    if (!QueryHandshake(device))
    {
        PB_ERROR("PBOpen: Handshake failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    // Fetch config at start up
    if(!QueryEnv(device))
    {
        PB_ERROR("PBOpen: Querying environment config failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    if(!QueryPowerStatus(device))
    {
        PB_ERROR("PBOpen: Querying power port config failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    if(!QueryUSBStatus(device))
    {
        PB_ERROR("PBOpen: Querying usb port config failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    if(!QueryDewStatus(device))
    {
        PB_ERROR("PBOpen: Querying dew port config failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    if(!QueryAdjStatus(device))
    {
        PB_ERROR("PBOpen: Querying adj port config failed");
        device->port->Close();
        return PB_ERROR_COMMUNICATION;
    }

    // Start status listener thread
    StartStatusListener(device);

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
        if(config->updateRate < 1 || config->humidityOffset > 255)
        {
            return PB_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SER%d#", config->envUpdateRate);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->envUpdateRate = config->envUpdateRate;
    }

    if(config->mask & MASK_PB_UPDATE_RATE)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SUR%d#", config->updateRate);

        if (!SendCommand(device, cmd))
        {
            return PB_ERROR_COMMUNICATION;
        }

        device->updateRate = config->updateRate;
    }

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
