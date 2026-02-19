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

#ifndef ADJ_H
#define ADJ_H

#include "PWM.h"
#include "GPIOManager.h"
#include "MCP320X.h"
#include "MCP4725.h"
#include "BTS7XXX.h"
#include <cstdint>

namespace PowerBox
{
    class BuckPort
    {
        public:
            BuckPort(const GPIOManager& gpio, const MCP3204& mcp, const char* dev, uint8_t address);
            ~BuckPort(void);

            uint8_t begin(uint8_t diag_pin, uint8_t pwr_pin, uint8_t sel_pin, uint8_t port);

            void enable(void) { this->bts_->enable(); }
            void disable(void) { this->bts_->disable(); }
            void toggle(void) { this->bts_->toggle(); }
            void setState(uint8_t state, float targetVoltage);
            void setSupplyVoltage(float voltage) { this->voltage_supply_ = voltage * 1000; }

            void measureCurrent(void) { this->bts_->measureCurrent(); }

            uint16_t getCurrent_mA(void) const { return this->bts_->getCurrent_mA(); }
            uint8_t getState(void) const { return this->bts_->getState(); }
            float getVoltage(void) const;
            float getTargetVoltage(void) const { return this->voltage_target_ * 0.001f; }
            float getMinVoltage(void) const { return this->VMIN * 0.001f; }
            float getMaxVoltage(void) const { return this->voltage_max_ * 0.001f; }
            uint8_t getOverCurrent(void) const { return this->bts_->getOverCurrent(); }
            uint8_t getOverVoltage(void) const { return this->over_voltage_; }

            void resetOverCurrent(void) { this->bts_->resetOverCurrent(); }

        private:
            bool checkVoltage_(int voltage);
            void setVoltage_(int target);

            const GPIOManager* gpio_;
            const MCP3204* adc_;
            MCP4725* dac_;
            const char* dev_;
            uint8_t address_;

            BTS7080<MCP3204>* bts_;

            int voltage_adc_;
            int voltage_max_;
            int voltage_target_;
            int voltage_supply_;
            bool over_voltage_;

            const BTS7006<MCP3204>* supply_;

            static constexpr float ADJ_R1 = 82e3f;
            static constexpr float ADJ_R2 = 24.3e3f;
            static constexpr float ADJ_R3 = 5.23e3f;
            static constexpr float ADJ_REF = 0.6f;
            static constexpr float SUPPLY_R1 = 35.7e3f;
            static constexpr float SUPPLY_R2 = 10e3f;
            static constexpr float SUPPLY_REF = 3.3f;

            static constexpr int VMAX = 1e3 * (ADJ_REF + ADJ_REF * (ADJ_R1 / ADJ_R3 + ADJ_R1 / ADJ_R2));
            static constexpr int VMIN = std::max(1000, (int)(VMAX - ADJ_R1 * SUPPLY_REF / ADJ_R2 * 1e3));
    };

    class PWMPort
    {
        public:
            PWMPort(const GPIOManager& gpio, const MCP3204& mcp);
            ~PWMPort(void);

            uint8_t begin(uint8_t diag_pin, uint8_t pwr_pin, uint8_t sel_pin, uint8_t port, unsigned int freq);

            void enable(void) { this->bts_->enable(); }
            void disable(void) { this->bts_->disable(); }
            void toggle(void) { this->bts_->toggle(); }
            void setState(uint8_t state, uint8_t dc);

            void measureCurrent(void) { this->bts_->measureCurrent(); }

            uint16_t getCurrent_mA(void) const { return this->bts_->getCurrent_mA(); }
            uint8_t getState(void) const { return this->bts_->getState(); }
            uint8_t getDutyCycle(void) const;
            uint8_t getOverCurrent(void) const { return this->bts_->getOverCurrent(); }

            void resetOverCurrent(void) { this->bts_->resetOverCurrent(); }

        private:
            const GPIOManager* gpio_;
            const MCP3204* mcp_;

            BTS7080<MCP3204>* bts_;
            PWM* pwm_;
    };
} /* namespace PowerBox */

#endif /* ADJ_H */