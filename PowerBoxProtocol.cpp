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
#ifdef __unix__
#include <termios.h>
#endif

namespace PowerBox
{
    bool SendCommand(std::shared_ptr<Device> device, const char *command, int timeoutMs)
    {
        if (!device || !device->port || !device->port->IsOpen())
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

    bool QueryHandshake(std::shared_ptr<Device> device)
    {
        if (!device || !device->port)
        {
            return false;
        }

        PB_DEBUG("QueryHandshake: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryHandshake: Port not open");
            return false;
        }

        // Wait for port to fully stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        char response[128];

        // Flush kernel AND application buffers multiple times to ensure clean state
        device->port->Flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        device->port->Flush();

        // Aggressively drain any residual data in kernel buffer
        // Read with short timeout repeatedly until nothing remains
        PB_DEBUG("QueryHandshake: Aggressively draining residual data");
        unsigned char drainBuf[256];
        int drainAttempts = 0;
        while (drainAttempts < 10)
        {
            int n = device->port->Read(drainBuf, sizeof(drainBuf), '#', 20);
            if (n <= 0)
            {
                // No more data available
                break;
            }
            PB_DEBUG("QueryHandshake: Drained attempt %d: %d bytes", drainAttempts, n);
            drainAttempts++;
        }

        // One final flush after draining to ensure clean state
        device->port->Flush();

        // Send initial handshake and retry periodically if no response
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 15000;
        auto lastHandshakeSent = start;
        const int handshakeRetryIntervalMs = 600; // Resend handshake every 600ms

        // Send initial handshake
        if (!device->port->Write((const unsigned char *)":HS#", 4))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        PB_DEBUG("Handshake: Sent initial command");
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Give device time to process

        while (true)
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = now - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("Handshake: timeout waiting for device response after %dms", elapsedMs);
                return false;
            }

            // Check if we should resend the handshake command
            auto timeSinceLastHandshake = now - lastHandshakeSent;
            int msSinceLastHandshake = std::chrono::duration_cast<std::chrono::milliseconds>(timeSinceLastHandshake).count();
            if (msSinceLastHandshake >= handshakeRetryIntervalMs)
            {
                PB_DEBUG("Handshake: Resending handshake command (attempt at %dms)", elapsedMs);
                if (!device->port->Write((const unsigned char *)":HS#", 4))
                {
                    PB_DEBUG("Handshake: Retry write failed");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                lastHandshakeSent = now;
            }

            // Use shorter read timeout to allow checking for retry interval
            int readTimeoutMs = std::min(remainingMs, handshakeRetryIntervalMs);
            int n = device->port->Read((unsigned char *)response, 128, '#', readTimeoutMs);
            if (!n)
            {
                // Timeout on read - continue loop to potentially retry handshake
                continue;
            }

            char model[32];
            char uuid[40];
            char serial[32];
            char *p = strstr(response, "PINS:");
            if (p && sscanf(p, "PINS:%32[^:]:%40[^:]:%16[^:]:%d#", model, uuid, serial, &device->firmwareVersion) == 4)
            {
                device->modelType = model;
                device->uuid = uuid;
                device->serial = serial;
                return true;
            }

            /* If we got a response that looks like a UUID/serial/firmware but missing the PINS: prefix,
               it means we got a truncated handshake response. This sometimes happens on first attempts.
               Instead of waiting, immediately resend the handshake without waiting for the retry interval. */
            if (strstr(response, "POWERBOX") || (strlen(response) > 40 && strstr(response, ":")))
            {
                PB_DEBUG("Handshake: Got truncated response, immediately retrying");
                if (!device->port->Write((const unsigned char *)":HS#", 4))
                {
                    PB_DEBUG("Handshake: Retry write failed");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                lastHandshakeSent = std::chrono::high_resolution_clock::now();
                continue;
            }

            PB_DEBUG("Handshake: Ignored unrelated message: %s", response);
            /* loop and wait for the correct message until timeout */
        }
    }

    bool QueryEnv(std::shared_ptr<Device> device)
    {
        if(!device || !device->port)
        {
            PB_DEBUG("QueryEnv: invalid device");
            return false;
        }

        PB_DEBUG("QueryEnv: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryEnv: Port not open");
            return false;
        }

        device->port->Flush();
        if (!device->port->Write((const unsigned char *)":GES#", 5))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        // Read device status - ignore unrelated messages that may arrive
        char response[64];
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 3000;

        while (true)
        {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("QueryEnv: timeout reading from serial");
                return false;
            }

            int n = device->port->Read((unsigned char *)response, 64, '#', remainingMs);
            if (!n)
            {
                PB_DEBUG("QueryEnv: timeout reading from serial");
                return false;
            }

            char *p = strstr(response, "ENVMODEL:");
            if (p && sscanf(p,
                       "ENVMODEL:%f:%f:%d#",
                       &device->temperatureOffset,
                       &device->humidityOffset,
                       &device->envUpdateRate) == 3)
            {
                break; /* got the expected message */
            }

            PB_DEBUG("QueryEnv: Ignored unrelated message: %s", response);
            /* loop and wait for the correct message until timeout */
        }

        PB_DEBUG("QueryEnv: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    bool QueryPowerStatus(std::shared_ptr<Device> device)
    {
        if(!device || !device->port)
        {
            PB_DEBUG("QueryStatus: invalid device");
            return false;
        }

        PB_DEBUG("QueryStatus: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryStatus: Port not open");
            return false;
        }

        device->port->Flush();
        if (!device->port->Write((const unsigned char *)":GPS#", 5))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        // Read device status - ignore unrelated messages that may arrive
        char response[256];
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 3000;

        while (true)
        {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            int n = device->port->Read((unsigned char *)response, 256, '#', remainingMs);
            if (!n)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            char *p = strstr(response, "POUTS:");
            if (p && sscanf(p, "POUTS:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
                            &device->powerState[0], &device->powerBootstrap[0],
                            &device->powerState[1], &device->powerBootstrap[1],
                            &device->powerState[2], &device->powerBootstrap[2],
                            &device->powerState[3], &device->powerBootstrap[3],
                            &device->powerState[4], &device->powerBootstrap[4],
                            &device->powerState[5], &device->powerBootstrap[5]) == 12)
            {
                break;
            }

            PB_DEBUG("QueryStatus: Ignored unrelated message: %s", response);
        }

        PB_DEBUG("QueryStatus: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    bool QueryUSBStatus(std::shared_ptr<Device> device)
    {
        if(!device || !device->port)
        {
            PB_DEBUG("QueryStatus: invalid device");
            return false;
        }

        PB_DEBUG("QueryStatus: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryStatus: Port not open");
            return false;
        }

        device->port->Flush();
        if (!device->port->Write((const unsigned char *)":GUS#", 5))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        // Read device status - ignore unrelated messages that may arrive
        char response[256];
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 3000;

        while (true)
        {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            int n = device->port->Read((unsigned char *)response, 256, '#', remainingMs);
            if (!n)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            char *p = strstr(response, "USBS:");
            if (p && sscanf(p, "USBS:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
                            &device->usbState[0], &device->usbBootstrap[0],
                            &device->usbState[1], &device->usbBootstrap[1],
                            &device->usbState[2], &device->usbBootstrap[2],
                            &device->usbState[3], &device->usbBootstrap[3],
                            &device->usbState[4], &device->usbBootstrap[4],
                            &device->usbState[5], &device->usbBootstrap[5]) == 12)
            {
                break;
            }

            PB_DEBUG("QueryStatus: Ignored unrelated message: %s", response);
        }

        PB_DEBUG("QueryStatus: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    bool QueryDewStatus(std::shared_ptr<Device> device)
    {
        if(!device || !device->port)
        {
            PB_DEBUG("QueryStatus: invalid device");
            return false;
        }

        PB_DEBUG("QueryStatus: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryStatus: Port not open");
            return false;
        }

        device->port->Flush();
        if (!device->port->Write((const unsigned char *)":GDS#", 5))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        // Read device status - ignore unrelated messages that may arrive
        char response[256];
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 3000;

        while (true)
        {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            int n = device->port->Read((unsigned char *)response, 256, '#', remainingMs);
            if (!n)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            char *p = strstr(response, "DEWS:");
            if (p && sscanf(p, "DEWS:%d:%d:%d:%d:%d#",
                            &device->dewPwmResolution,
                            &device->dewThreshold[0], &device->dewAuto[0],
                            &device->dewThreshold[1], &device->dewAuto[1]) == 5)
            {
                break;
            }

            PB_DEBUG("QueryStatus: Ignored unrelated message: %s", response);
        }

        PB_DEBUG("QueryStatus: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    bool QueryAdjStatus(std::shared_ptr<Device> device)
    {
        if(!device || !device->port)
        {
            PB_DEBUG("QueryStatus: invalid device");
            return false;
        }

        PB_DEBUG("QueryStatus: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            PB_DEBUG("QueryStatus: Port not open");
            return false;
        }

        device->port->Flush();
        if (!device->port->Write((const unsigned char *)":GAS#", 5))
        {
            PB_DEBUG("Handshake: Writing to serial failed");
            return false;
        }

        // Read device status - ignore unrelated messages that may arrive
        char response[256];
        auto start = std::chrono::high_resolution_clock::now();
        const int totalTimeoutMs = 3000;

        while (true)
        {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int remainingMs = totalTimeoutMs - elapsedMs;
            if (remainingMs <= 0)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            int n = device->port->Read((unsigned char *)response, 256, '#', remainingMs);
            if (!n)
            {
                PB_DEBUG("QueryStatus: timeout reading from serial");
                return false;
            }

            char *p = strstr(response, "ADJS:");
            if (p && sscanf(p, "ADJS:%d:%d:%d:%d#",
                            &device->buckBootstrap, &device->buckState,
                            &device->pwmState, &device->pwmPwmResolution) == 4)
            {
                break;
            }

            PB_DEBUG("QueryStatus: Ignored unrelated message: %s", response);
        }

        PB_DEBUG("QueryStatus: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    /* Background listener thread function for status messages */
    static void StatusListenerThreadFunc(std::shared_ptr<Device> device)
    {
        char buffer[256];

        while(device->listenerRunning)
        {
            if (!device || !device->port)
            {
                PB_DEBUG("StatusListener: Port unavailable, exiting");
                device->listenerRunning = false;
                return;
            }

            if (!device->port->IsOpen())
            {
                PB_DEBUG("StatusListener: Port not open, exiting");
                device->listenerRunning = false;
                return;
            }

            if (device->port->Read((unsigned char *)buffer, 256, '#', 70000))
            {
                /* Parse different message types based on prefix */
                if (strstr(buffer, "UP:") == buffer)
                {
                    sscanf(buffer, "UP:%d#", &device->upTime);
                }
                else if (strstr(buffer, "ENV:") == buffer)
                {
                    sscanf(buffer, "ENV:%f:%f:%f#", &device->temperature, &device->humidity, &device->dewPoint);
                }
                else if (strstr(buffer, "PSUP:") == buffer)
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
                else if (strstr(buffer, "POUT:") == buffer)
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
                else if (strstr(buffer, "USB:") == buffer)
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
                else if (strstr(buffer, "DEW:") == buffer)
                {
                    int dewA[2];
                    float probe[2];
                    sscanf(buffer, "DEW:%d:%f:%d:%d:%d:%d:%f:%d:%d:%d#",
                           &dewA[0], &device->dewProbe[0], &device->dewPWM[0], &device->dewState[0], &device->dewOvercurrent[0],
                           &dewA[1], &device->dewProbe[1], &device->dewPWM[1], &device->dewState[1], &device->dewOvercurrent[1]);
                    for (int i = 0; i < 2; ++i)
                    {
                        device->dewCurrent[i] = dewA[i] / 1000.0f;
                    }
                }
                else if (strstr(buffer, "ADJ:") == buffer)
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
        device->listenerRunning = false;

        /* Small delay to let old thread exit if it's still running */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        /* Start new listener thread */
        device->listenerRunning = true;
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
        device->listenerRunning = false;
        PB_DEBUG("StopStatusListener: Listener stop requested");
    }
} /* namespace PowerBox */
