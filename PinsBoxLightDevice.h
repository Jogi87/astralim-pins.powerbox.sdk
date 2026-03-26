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

#ifndef PINS_BOX_LIGHT_DEVICE_H
#define PINS_BOX_LIGHT_DEVICE_H

#include "Device.h"
#include "GPIOManager.h"
#include "BTS7XXX.h"
#include "MCP320X.h"
#include <string>
#include <thread>

#define PINSBOX_NUM_POWER_PORTS 4

namespace PowerBox
{
    class PinsBoxLightDevice : public Device
    {
        public:
            PinsBoxLightDevice(void);
            virtual ~PinsBoxLightDevice(void);

            virtual PB_ERROR_TYPE Open(void);
            virtual void Close(void);

            virtual std::string GetSerial(void);

            virtual int GetUpTime(void);
            virtual float GetCoreTemp(void);

            virtual float GetSupply12V(void)  { return this->supply12V; }
            virtual float GetSupply12A(void) { return this->supply12A; }
            virtual float GetSupply12Ah(void) { return this->supply12Ah; }
            virtual float GetSupply12Wh(void) { return this->supply12Wh; }
            virtual float GetSupply12AverageA(void);

            virtual int GetUpdateRate() { return 1; }
            virtual bool SetUpdateRate(int val) { return true; }

            virtual int GetNumPowerPorts(void) const { return PINSBOX_NUM_POWER_PORTS; }

            virtual int GetPowerState(int i);
            virtual bool SetPowerState(int i, int state);
            virtual bool ResetPowerOvercurrent(int i)                               { return false; }
            virtual int GetPowerBootState(int i) { return powerBootstrap[i]; }
            virtual bool SetPowerBootState(int i, int state);

            virtual int GetFirmwareVersion() { return 1; }
            virtual std::string GetModelType() { return "Raspberry Pi"; }
            virtual std::string GetUUID() { return this->GetFullSerial(); }

            virtual PB_ERROR_TYPE FactoryReset(void)                                { return PB_SUCCESS; }

            virtual void StartStatusListener(void);
            virtual void StopStatusListener(void);

        protected:
            virtual void StatusListenerThreadFunc();

        private:
            void ResetProperties(void);
            std::string GetFullSerial(void);
            void GetMCP3202Data(void);
            void LoadSettings(void);
            void SaveSettings(void);

            GPIOManager *gpio_;
            MCP3202* adc_;

            BTS7006<MCP3202>* supply_;
            BTS7012<MCP3202>** pwr12_;
            BTS7080<MCP3202>** pwr34_;

            std::thread statusListenerThread_;
    };

#ifdef HAVE_LIBGPIOD
    bool ScanPinsBox(int *ids);
#else
    /* Stub when libgpiod / PinsBox support is not available */
    inline bool ScanPinsBox(int *ids) { (void)ids; return false; }
#endif

} /* namespace PowerBox */

#endif /* PINS_BOX_LIGHT_DEVICE_H */