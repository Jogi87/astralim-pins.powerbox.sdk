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

#include "DEW.h"
#include "common.h"

namespace PowerBox
{
    DewPort::DewPort(const GPIOManager& gpio, const MCP3204& mcp, const char* chip, int ch)
    {
        this->gpio_ = &gpio;
        this->mcp_ = &mcp;

        this->probe_ = new DS18B20(gpio);
        this->bts_ = new BTS7080<MCP3204>(gpio, mcp);
        this->pwm_ = new PWM(chip, ch);
    }

    DewPort::~DewPort(void)
    {
        if(this->probe_) {
            delete this->probe_;
            this->probe_ = nullptr;
        }
        if(this->bts_) {
            delete this->bts_;
            this->bts_ = nullptr;
        }
        if(this->pwm_) {
            delete this->pwm_;
            this->pwm_ = nullptr;
        }
    }

    uint8_t DewPort::begin(uint8_t diag_pin, uint8_t pwr_pin, uint8_t sel_pin, uint8_t probe_pin, uint8_t port, unsigned int freq)
    {
        this->probe_->begin(probe_pin);

        this->bts_->begin(1200, diag_pin, pwr_pin, sel_pin, port);
        this->bts_->setMaxCurrent(3.f);
        this->bts_->setChannel(2);
        this->bts_->setSampling(100, 50);

        RETURN_IF_ERROR(this->pwm_->setExport());
        RETURN_IF_ERROR(this->pwm_->setPeriod(1e9 / freq));
        RETURN_IF_ERROR(this->pwm_->setDutyCycle(0));
        RETURN_IF_ERROR(this->pwm_->setState(0));

        return true;
    }

    void DewPort::setState(uint8_t state, uint8_t dc)
    {
        // Convert duty cycle to nano seconds
        unsigned int period = this->pwm_->getPeriod();
        unsigned int dutyCycle = period * dc / 255;

        // Set duty cycle
        this->pwm_->setDutyCycle(dutyCycle);

        if(dutyCycle == 0)
        {
            // Turn off
            this->pwm_->setState(0);
            this->bts_->setState(0);
        }

        this->pwm_->setState(state);
        this->bts_->setState(state);
    }

    uint8_t DewPort::getDutyCycle(void) const
    {
        unsigned int period = this->pwm_->getPeriod();
        unsigned int dutyCycle_ns = this->pwm_->getDutyCycle();
        
        if (period == 0) {
            return 0;
        }

        // Convert nanoseconds back to 0-255 scale
        return (dutyCycle_ns * 255) / period;
    }
} /* namespace PowerBox */