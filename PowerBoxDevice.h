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

#ifndef POWER_BOX_DEVICE_H
#define POWER_BOX_DEVICE_H

#include "PowerBoxSerialPort.h"
#include "PowerBoxSDK.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

namespace PowerBox
{
    /**
     * Device represents a Power Box device with its current state.
     */
    struct Device
    {
        std::shared_ptr<SerialPort> port;
        std::string portName;
        std::string modelType;
        std::string uuid;
        std::string serial;
        int firmwareVersion = 0;

        // Status
        int upTime = 0;
        float temperature = 0.0f;
        float humidity = 0.0f;
        float dewPoint = 0.0f;

        // Supply status
        float supply12V = 0.0f;
        float supply12A = 0.0f;
        float supply5V = 0.0f;
        float supply12Ah = 0.0f;
        float supply12Wh = 0.0f;

        // Port status
        float powerCurrent[PB_NUM_POWER_PORTS] = {0.0f};
        int powerState[PB_NUM_POWER_PORTS] = {0};
        int powerBootstrap[PB_NUM_POWER_PORTS] = {0};
        int powerOvercurrent[PB_NUM_POWER_PORTS] = {0};
        float usbCurrent[PB_NUM_USB_PORTS] = {0.0f};
        float usbVoltage[PB_NUM_USB_PORTS] = {0.0f};
        int usbState[PB_NUM_USB_PORTS] = {0};
        int usbBootstrap[PB_NUM_USB_PORTS] = {0};
        int usbOvercurrent[PB_NUM_USB_PORTS] = {0};
        float dewCurrent[PB_NUM_DEW_PORTS] = {0.0f};
        float dewProbe[PB_NUM_DEW_PORTS] = {-127.0f};
        int dewPWM[PB_NUM_DEW_PORTS] = {0};
        int dewState[PB_NUM_DEW_PORTS] = {0};
        int dewPwmResolution = 0;
        int dewThreshold[PB_NUM_DEW_PORTS] = {0};
        int dewAuto[PB_NUM_DEW_PORTS] = {0};
        int dewOvercurrent[PB_NUM_DEW_PORTS] = {0};
        float buckCurrent = 0.0f;
        float buckVoltage = 0.0f;
        float buckVmin = 0.0f;
        float buckVmax = 0.0f;
        float buckVset = 0.0f;
        int buckBootstrap = 0;
        int buckState = 0;
        int buckOvercurrent = 0;
        float pwmCurrent = 0.0f;
        int pwmPWM = 0;
        int pwmState = 0;
        int pwmOvercurrent = 0;
        int pwmPwmResolution = 0;

        // Config
        float temperatureOffset = 0.0f;
        float humidityOffset = 0.0f;
        int envUpdateRate = 3;
        int updateRate = 1;

        /* Listener thread state - don't store thread, just the flag */
        std::atomic<bool> listenerRunning{false};
        std::atomic<bool> isOpen{false};
        std::mutex listenerMutex;

        /* Simple destructor - nothing to clean up */
        ~Device() = default;
    };

    /**
     * Global device registry mapping device IDs to Device objects.
     */
    extern std::map<int, std::shared_ptr<Device>> g_devices;

    /**
     * Global mutex protecting access to g_devices.
     */
    extern std::mutex g_globalMutex;

} /* namespace PowerBox */

#endif /* POWER_BOX_DEVICE_H */
