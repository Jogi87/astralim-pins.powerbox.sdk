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

#include "ADJ.h"
#include "common.h"

namespace PowerBox
{
    BuckPort::BuckPort(const GPIOManager& gpio, const MCP3204& mcp, const char* dev, uint8_t address)
    {
        this->gpio_ = &gpio;
        this->dev_ = dev;
        this->address_ = address;
        this->adc_ = &mcp;

        this->voltage_adc_ = 0;
        this->voltage_max_ = VMAX;
        this->voltage_supply_ = 0;
        this->over_voltage_ = false;

        this->dac_ = new MCP4725(dev, address, this->SUPPLY_REF);
        this->bts_ = new BTS7080<MCP3204>(gpio, mcp);
    }

    BuckPort::~BuckPort(void)
    {
        delete this->bts_;
        delete this->dac_;
    }

    uint8_t BuckPort::begin(uint8_t diag_pin, uint8_t pwr_pin, uint8_t sel_pin, uint8_t port, float target, uint8_t state)
    {
        this->bts_->begin(1200, diag_pin, pwr_pin, sel_pin, port);
        this->bts_->setMaxCurrent(3.0f);
        this->bts_->setChannel(2);
        this->bts_->setSampling(10, 1);

        this->dac_->begin();

        this->voltage_supply_ = this->adc_->analogReadAverage(1, 5, 0) * (36e3 + 4.7e3) / 4.7e3;
        
        // Set power accordingly
        this->setState(state, target);

        return true;
    }
    
    void BuckPort::setState(uint8_t state, float targetVoltage)
    {
        this->setVoltage_(targetVoltage * 1000);
        this->bts_->setState(state);
    }
    
    float BuckPort::getVoltage(void) const
    {
        return this->adc_->analogReadAverage(3, 10, 1) * (SUPPLY_R1 + SUPPLY_R2) / SUPPLY_R2 * 0.001f;
    }

    bool BuckPort::checkVoltage_(int voltage)
    {
        // We need to check whether supply voltage is still able to power
        // the requested output voltage
        this->voltage_max_ = std::min((int)(this->voltage_supply_ * 0.95f), VMAX);

        if(voltage > 0)
        {
            if(voltage > this->voltage_max_ || voltage < VMIN)
            {
                this->bts_->disable();
                this->dac_->setVoltage(SUPPLY_REF);

                this->voltage_target_ = VMIN;
                this->over_voltage_ = 1;

                PB_ERROR("Target voltage %d is out of range (%d - %d)", voltage, VMIN, this->voltage_max_);

                return false;
            }
        }
        else if(voltage == 0)
        {
            this->bts_->disable();
            this->dac_->setVoltage(SUPPLY_REF);
            this->voltage_target_ = VMIN;

            return false;
        }

        return true;
    }

    void BuckPort::setVoltage_(int target)
    {
        // Check if requested voltage is possible
        if(!this->checkVoltage_(target))
        {
            return;
        }

        // First we need to check what is possible, depending on the supply voltage
        // as we only support up to 95% of supply voltage
        // e.g. on 12.0V supply, we can have 11.4V at most

        // Set current power state
        this->voltage_target_ = target;

        // Calculate required DAC voltage
        float Vdac = ADJ_R2 * std::max(0.f, VMAX - target * 1.015f) / ADJ_R1 * 0.001f;

        // Set DAC accordingly
        this->dac_->setVoltage(Vdac);

        // Reset over voltage flag
        this->over_voltage_ = false;
    }










    template <typename MCP, uint16_t kILIS>
    PWMPortT<MCP, kILIS>::PWMPortT(const GPIOManager& gpio, const MCP& mcp, const char* chip, int ch)
    {
        this->gpio_ = &gpio;
        this->mcp_ = &mcp;

        this->bts_ = new BTS7XXX_<MCP, kILIS>(gpio, mcp);
        this->pwm_ = new PWM(chip, ch);
    }

    template <typename MCP, uint16_t kILIS>
    PWMPortT<MCP, kILIS>::~PWMPortT(void)
    {
        delete this->bts_;
        delete this->pwm_;
    }

    template <typename MCP, uint16_t kILIS>
    uint8_t PWMPortT<MCP, kILIS>::begin(uint8_t diag_pin, uint8_t pwr_pin, uint8_t sel_pin, uint8_t port, unsigned int freq, uint8_t adc_ch, float maxCurrent)
    {
        this->bts_->begin(1200, diag_pin, pwr_pin, sel_pin, port);
        this->bts_->setMaxCurrent(maxCurrent);
        this->bts_->setChannel(adc_ch);
        this->bts_->setSampling(100, 50);

        RETURN_IF_ERROR(this->pwm_->setExport());
        RETURN_IF_ERROR(this->pwm_->setPeriod(1e9 / freq));
        RETURN_IF_ERROR(this->pwm_->setDutyCycle(0));
        RETURN_IF_ERROR(this->pwm_->setState(0));

        return true;
    }

    template <typename MCP, uint16_t kILIS>
    void PWMPortT<MCP, kILIS>::setState(uint8_t state, uint8_t dc)
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

    template <typename MCP, uint16_t kILIS>
    uint8_t PWMPortT<MCP, kILIS>::getDutyCycle(void) const
    {
        unsigned int period = this->pwm_->getPeriod();
        unsigned int dutyCycle_ns = this->pwm_->getDutyCycle();

        if (period == 0) {
            return 0;
        }

        // Convert nanoseconds back to 0-255 scale
        return (dutyCycle_ns * 255) / period;
    }

    /* Explicit instantiations for the supported (ADC, BTS) combinations */
    template class PWMPortT<MCP3204, 1800>;  // PinsBox: BTS7080 PWM port
    template class PWMPortT<MCP3202, 4785>;  // PinsBoxMini: BTS7012 PWM port
} /* namespace PowerBox */