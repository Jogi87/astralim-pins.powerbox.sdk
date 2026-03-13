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

#include "PowerBoxProtocol.h"
#include "PowerBoxLogging.h"
#include <cstring>
#include <cstdio>
#include <memory>
#include <chrono>
#include <thread>

namespace PowerBox
{
    bool SendCommand(std::shared_ptr<Device> device, const char *command, int timeoutMs)
    {
        if (!device)
        {
            return PB_ERROR_NULL_POINTER;
        }

        if (!device->port || !device->port->IsOpen())
        {
            PB_DEBUG("SendCommand: device=%p, port=%p, isOpen=%d",
                     device.get(), device ? device->port.get() : nullptr,
                     device && device->port ? device->port->IsOpen() : 0);
            return false;
        }

        // 100 ms delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        PB_DEBUG("SendCommand: Writing '%s'", command);
        if (!device->port->Write((const unsigned char *)command, strlen(command)))
        {
            PB_DEBUG("SendCommand: Write failed");
            return false;
        }

        return true;
    }

    /* Message parsing helper functions */
    static void ParseHandshakeMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        char model[33];
        char uuid[41];
        char serial[33];
        int firmware;
        if (sscanf(buffer, "PINS:%32[^:]:%40[^:]:%32[^:]:%d#",
                   model, uuid, serial, &firmware) == 4)
        {
            // Store in device
            device->modelType = model;
            device->uuid = uuid;
            device->serial = serial;
            device->firmwareVersion = firmware;
            device->handshakePending = false;
        }

        device->handshakeCV.notify_one();
    }

    static void ParseUptimeMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        sscanf(buffer, "UP:%d#", &device->upTime);
    }

    static void ParseEnvironmentMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        sscanf(buffer, "ENV:%f:%f:%f:%d#", &device->temperature, &device->humidity, &device->dewPoint, &device->extSensor);
    }

    static void ParseEnvModelMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        float tempOffset, humOffset;
        int envUpdate;
        if (sscanf(buffer, "ENVMODEL:%f:%f:%d#", &tempOffset, &humOffset, &envUpdate) == 3)
        {
            // Store in device
            {
                device->temperatureOffset = tempOffset;
                device->humidityOffset = humOffset;
                device->envUpdateRate = envUpdate;
                device->envModelPending = false;
            }

            device->envModelCV.notify_one();
        }
    }

    static void ParseUpdateRateMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int rate;
        if (sscanf(buffer, "UDR:%d#", &rate) == 1)
        {
            // Store in device
            {
                device->updateRate = rate;
                device->updateRatePending = false;
            }

            device->updateRateCV.notify_one();
        }
    }

    static void ParsePowerSupplyMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int mV12, mA12, mV5;
        float mAh12, mWh12;
        sscanf(buffer, "PSUP:%d:%d:%d:%f:%f#", &mV12, &mA12, &mV5, &mAh12, &mWh12);
        device->supply12V = mV12 / 1000.0f;
        device->supply12A = mA12 / 1000.0f;
        device->supply5V = mV5 / 1000.0f;
        device->supply12Ah = mAh12 / 1000.0f;
        device->supply12Wh = mWh12 / 1000.0f;
    }

    static void ParsePowerOutputMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int mA[6];
        sscanf(buffer, "POUT:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
               &mA[0], &device->powerOvercurrent[0],
               &mA[1], &device->powerOvercurrent[1],
               &mA[2], &device->powerOvercurrent[2],
               &mA[3], &device->powerOvercurrent[3],
               &mA[4], &device->powerOvercurrent[4],
               &mA[5], &device->powerOvercurrent[5]);
        for (int i = 0; i < 6; ++i)
        {
            device->powerCurrent[i] = mA[i] / 1000.0f;
        }
    }

    static void ParsePowerOutputConfigMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int state[6], boot[6];
        if(sscanf(buffer, "POUTS:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
               &state[0], &boot[0], &state[1], &boot[1], &state[2], &boot[2],
               &state[3], &boot[3], &state[4], &boot[4], &state[5], &boot[5]) == 12)
        {
            // Store in device
            {
                for (int i = 0; i < 6; ++i)
                {
                    device->powerState[i] = state[i];
                    device->powerBootstrap[i] = boot[i];
                }
                device->pwrConfigPending = false;
            }

            device->pwrConfigCV.notify_one();
        }
    }

    static void ParseUSBMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int usbV[6];
        int usbA[6];
        sscanf(buffer, "USB:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
                &usbA[0], &usbV[0], &device->usbOvercurrent[0],
                &usbA[1], &usbV[1], &device->usbOvercurrent[1],
                &usbA[2], &usbV[2], &device->usbOvercurrent[2],
                &usbA[3], &usbV[3], &device->usbOvercurrent[3],
                &usbA[4], &usbV[4], &device->usbOvercurrent[4],
                &usbA[5], &usbV[5], &device->usbOvercurrent[5]);
        for (int i = 0; i < 6; ++i)
        {
            device->usbCurrent[i] = usbA[i] / 1000.0f;
            device->usbVoltage[i] = usbV[i] / 1000.0f;
        }
    }

    static void ParseUSBConfigMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int state[6], boot[6];
        if(sscanf(buffer, "USBS:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
               &state[0], &boot[0], &state[1], &boot[1], &state[2], &boot[2],
               &state[3], &boot[3], &state[4], &boot[4], &state[5], &boot[5]) == 12)
        {
            // Store in device
            {
                for (int i = 0; i < 6; ++i)
                {
                    device->usbState[i] = state[i];
                    device->usbBootstrap[i] = boot[i];
                }
                device->usbConfigPending = false;
            }

            device->usbConfigCV.notify_one();
        }
    }

    static void ParseDewMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int dewA[2];
        sscanf(buffer, "DEW:%d:%f:%d:%d:%d:%d:%f:%d:%d:%d#",
               &dewA[0], &device->dewProbe[0], &device->dewPWM[0], &device->dewState[0], &device->dewOvercurrent[0],
               &dewA[1], &device->dewProbe[1], &device->dewPWM[1], &device->dewState[1], &device->dewOvercurrent[1]);
        for (int i = 0; i < 2; ++i)
        {
            device->dewCurrent[i] = dewA[i] / 1000.0f;
        }
    }

    static void ParseDewConfigMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int pwmres, auto0, auto1, state0, state1;
        float thres0, thres1;
        if(sscanf(buffer, "DEWS:%d:%f:%d:%d:%f:%d:%d#",
                  &pwmres,
                  &thres0, &auto0, &state0,
                  &thres1, &auto1, &state1) == 7)
        {
            // Store in device
            {
                std::lock_guard<std::mutex> lock(device->dewConfigMutex);
                device->dewPwmResolution = pwmres;
                device->dewThreshold[0] = thres0;
                device->dewAuto[0] = auto0;
                device->dewState[0] = state0;
                device->dewThreshold[1] = thres1;
                device->dewAuto[1] = auto1;
                device->dewState[1] = state1;
                device->dewConfigPending = false;
            }

            device->dewConfigCV.notify_one();
        }
    }

    static void ParseAdjMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int adjA[2], Vadc, Vmin, Vmax, Vset;
        sscanf(buffer, "ADJ:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
               &adjA[0], &Vset, &Vadc, &Vmin, &Vmax, &device->buckOvercurrent,
               &adjA[1], &device->pwmPWM, &device->pwmOvercurrent);
        device->buckCurrent = adjA[0] / 1000.0f;
        device->buckVoltage = Vadc / 1000.0f;
        device->buckVmin = Vmin / 1000.0f;
        device->buckVmax = Vmax / 1000.0f;
        device->buckVset = Vset / 1000.0f;
        device->pwmCurrent = adjA[1] / 1000.0f;
    }

    static void ParseAdjConfigMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int bootstate, state, pwmstate, pwmres;
        if (sscanf(buffer, "ADJS:%d:%d:%d:%d#", &bootstate, &state, &pwmstate, &pwmres) == 4)
        {
            // Store in device
            {
                std::lock_guard<std::mutex> lock(device->adjConfigMutex);
                device->buckBootstrap = bootstate;
                device->buckState = state;
                device->pwmState = pwmstate;
                device->pwmPwmResolution = pwmres;
                device->adjConfigPending = false;
            }

            device->adjConfigCV.notify_one();
        }
    }

    static void ParseWiFiInfoMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int mode, channel, rssi, hostnameLen;
        char ssid[PB_SSID_LEN] = {0};
        char ip[PB_IP_LEN] = {0};
        char hostname[PB_HOSTNAME_LEN] = {0};

        if (sscanf(buffer, "WIFI:%d:%d:%31[^:]:%15[^:]:%d:%d:", 
                   &mode, &channel, ssid, ip, &rssi, &hostnameLen) == 6)
        {
            // Find the hostname part (after the 7th colon)
            const char *hostStart = buffer;
            int colonCount = 0;
            while (*hostStart && colonCount < 7)
            {
                if (*hostStart == ':')
                {
                    colonCount++;
                }
                hostStart++;
            }

            // Copy hostname
            if (hostnameLen > 0 && hostnameLen < PB_HOSTNAME_LEN)
            {
                strncpy(hostname, hostStart, hostnameLen);
                hostname[hostnameLen] = '\0';
            }

            // Store in device
            {
                std::lock_guard<std::mutex> lock(device->wifiInfoMutex);
                device->wifiMode = mode;
                device->wifiChannel = channel;
                strncpy(device->wifiSSID, ssid, PB_SSID_LEN - 1);
                device->wifiSSID[PB_SSID_LEN - 1] = '\0';
                strncpy(device->wifiIP, ip, PB_IP_LEN - 1);
                device->wifiIP[PB_IP_LEN - 1] = '\0';
                device->wifiRSSI = rssi;
                strncpy(device->wifiHostname, hostname, PB_HOSTNAME_LEN - 1);
                device->wifiHostname[PB_HOSTNAME_LEN - 1] = '\0';
                device->wifiInfoPending = false;
            }
            
            PB_DEBUG("WiFi Info: mode=%d, channel=%d, ssid='%s', ip='%s', rssi=%d, hostname='%s'",
                     mode, channel, ssid, ip, rssi, hostname);
            
            device->wifiInfoCV.notify_one();
        }
    }

    static void ParseWiFiSurveyMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int networks;
        if (sscanf(buffer, "WSURV:%d:", &networks) != 1 || networks > PB_MAX_WIFI_NETWORKS)
        {
            return;
        }

        // Find the position after "WSURV:count:"
        char *pos = strchr((char *)buffer + 6, ':'); // Find first ':' after WSURV
        if (!pos)
        {
            return;
        }
        pos++; // Move past the ':'
        
        PB_DEBUG("WiFi Survey: Found %d networks", networks);
        
        // Lock and clear results
        {
            std::lock_guard<std::mutex> lock(device->wifiScanMutex);
            device->wifiScanResult.count = 0;
        }
        
        for (int i = 0; i < networks && i < PB_MAX_WIFI_NETWORKS; i++)
        {
            char ssid[PB_SSID_LEN] = {0};
            int rssi = 0;
            
            // Parse SSID (everything until the next colon)
            int ssidLen = 0;
            while (*pos && *pos != ':' && ssidLen < PB_SSID_LEN - 1)
            {
                ssid[ssidLen++] = *pos++;
            }
            ssid[ssidLen] = '\0';
            
            if (*pos == ':')
            {
                pos++; // Skip the colon
                // Parse RSSI (negative integer)
                sscanf(pos, "%d", &rssi);
                
                // Move to next field
                while (*pos && *pos != ':')
                {
                    pos++;
                }
                if (*pos == ':')
                {
                    pos++;
                }
                
                PB_DEBUG("  Network %d: SSID='%s' RSSI=%d dBm", i + 1, ssid, rssi);
                
                // Store result
                {
                    std::lock_guard<std::mutex> lock(device->wifiScanMutex);
                    strncpy(device->wifiScanResult.networks[i].ssid, ssid, PB_SSID_LEN - 1);
                    device->wifiScanResult.networks[i].ssid[PB_SSID_LEN - 1] = '\0';
                    device->wifiScanResult.networks[i].rssi = rssi;
                    device->wifiScanResult.count = i + 1;
                }
            }
        }
        
        // Signal that scan is complete
        {
            std::lock_guard<std::mutex> lock(device->wifiScanMutex);
            device->wifiScanPending = false;
        }
        device->wifiScanCV.notify_one();
    }

    /* Background listener thread function for status messages */
    static void StatusListenerThreadFunc(std::shared_ptr<Device> device)
    {
        char buffer[256];

        while(device->statusListenerRunning)
        {
            if (!device || !device->port)
            {
                PB_DEBUG("StatusListener: Port unavailable, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (!device->port->IsOpen())
            {
                PB_DEBUG("StatusListener: Port not open, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (device->port->Read((unsigned char *)buffer, 256, '#', 5000))
            {
                /* Parse different message types based on prefix */
                if (strstr(buffer, "PINS:") == buffer)
                {
                    /* Handshake message */
                    ParseHandshakeMessage(device, buffer);
                }
                else if (strstr(buffer, "UP:") == buffer)
                {
                    /* Uptime status */
                    ParseUptimeMessage(device, buffer);
                }
                else if (strstr(buffer, "ENVMODEL:") == buffer)
                {
                    /* Environment model */
                    ParseEnvModelMessage(device, buffer);
                }
                else if (strstr(buffer, "UDR:") == buffer)
                {
                    /* Update rate */
                    ParseUpdateRateMessage(device, buffer);
                }
                else if (strstr(buffer, "POUTS:") == buffer)
                {
                    /* Power port config */
                    ParsePowerOutputConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "USBS:") == buffer)
                {
                    /* USB port config */
                    ParseUSBConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "DEWS:") == buffer)
                {
                    /* Dew port config */
                    ParseDewConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "ADJS:") == buffer)
                {
                    /* Adj port config */
                    ParseAdjConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "ENV:") == buffer)
                {
                    /* Environment status */
                    ParseEnvironmentMessage(device, buffer);
                }
                else if (strstr(buffer, "PSUP:") == buffer)
                {
                    /* Power supply status */
                    ParsePowerSupplyMessage(device, buffer);
                }
                else if (strstr(buffer, "POUT:") == buffer)
                {
                    /* Power port status */
                    ParsePowerOutputMessage(device, buffer);
                }
                else if (strstr(buffer, "USB:") == buffer)
                {
                    /* USB port status */
                    ParseUSBMessage(device, buffer);
                }
                else if (strstr(buffer, "DEW:") == buffer)
                {
                    /* Dew port status */
                    ParseDewMessage(device, buffer);
                }
                else if (strstr(buffer, "ADJ:") == buffer)
                {
                    /* Adj port status */
                    ParseAdjMessage(device, buffer);
                }
                else if (strstr(buffer, "WIFI:") == buffer)
                {
                    /* Wifi status */
                    ParseWiFiInfoMessage(device, buffer);
                }
                else if (strstr(buffer, "WSURV:") == buffer)
                {
                    /* Wifi survey */
                    ParseWiFiSurveyMessage(device, buffer);
                }
            }
        }

        PB_DEBUG("StatusListener: exiting");
    }

    void StartStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Stop any existing listener by setting the flag */
        device->statusListenerRunning = false;

        /* Small delay to let old thread exit if it's still running */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        /* Start new listener thread */
        device->statusListenerRunning = true;
        std::thread listenerThread(StatusListenerThreadFunc, device);
        listenerThread.detach(); /* Detach immediately - let it run independently */
        PB_DEBUG("StartStatusListener: Listener thread started");
    }

    void StopStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Signal listener thread to stop */
        device->statusListenerRunning = false;
        PB_DEBUG("StopStatusListener: Listener stop requested");
        
        /* Give the detached listener thread time to exit cleanly
         * With a 5-second read timeout, thread will check flag within 5 seconds
         * Adding extra buffer to ensure thread has time to clean up
         */
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
} /* namespace PowerBox */
