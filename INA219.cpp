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

#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "INA219.h"

namespace PowerBox
{
    INA219::INA219(const char* dev, uint8_t address)
    {
        this->dev_ = dev;
        this->address_ = address;
        this->config_  = 0x0;
        this->i2c_fd_ = -1;
        this->current_lsb_ = 0.0f;
    }

    INA219::~INA219(void)
    {
        if (this->i2c_fd_ >= 0) {
            close(this->i2c_fd_);
            this->i2c_fd_ = -1;
        }
    }

    uint8_t INA219::begin(uint16_t gain, float max_current)
    {
        // Open I2C device
        this->i2c_fd_ = open(this->dev_, O_RDWR);
        if (this->i2c_fd_ < 0)
        {
            return false;
        }

        // Set I2C slave address
        if (ioctl(this->i2c_fd_, I2C_SLAVE, this->address_) < 0) {
            close(this->i2c_fd_);
            this->i2c_fd_ = -1;
            return false;
        }

        // Set gain
        this->config_ |= gain;
        this->config_ |= 0x0180;
        this->config_ |= 0x0018;
        this->config_ |= 0x07;

        this->writeRegister_(INA219_CONFIG, this->config_);

        // Calibration with maximal expected current and shunt resistor
        this->current_lsb_ = max_current / 32767.f;
        uint16_t calibration = trunc(0.04096 / (this->current_lsb_ * 0.1f));
        this->writeRegister_(INA219_CALIBRATION, calibration);

        return true;
    }

    float INA219::getShuntVoltage_mV(void) const
    {
        // Shunt voltage can be negative, so we need signed integer
        int16_t reg = this->readRegister_(INA219_SHUNT_VOLTAGE);

        // Convert to mV (right-shift 2 bits, each LSB = 10uV = 0.01mV)
        float shunt = (reg >> 2) * 0.01f;

        return (shunt < 0.f ? 0.f : shunt);
    }

    uint16_t INA219::getBusVoltage_mV(void) const
    {
        // Read from register
        uint16_t reg = this->readRegister_(INA219_BUS_VOLTAGE);

        // Bus voltage can be negative, so we need signed integer
        int16_t bus = (reg >> 3) * 4;

        return (bus > 0 ? bus : 0);
    }

    uint16_t INA219::getCurrent_mA(void) const
    {
        // Current can be negativ, so we need signed integer
        int16_t reg = this->readRegister_(INA219_CURRENT);

        // Apply LSB and convert to mA
        float current = reg * this->current_lsb_ * 1e3f;

        return (current > 0.0f ? current : 0.0f);
    }

    uint16_t INA219::getPower_mW(void) const
    {
        // Power can be negativ, so we need signed integer
        int16_t reg = this->readRegister_(INA219_POWER);

        // Apply LSB and convert to mW
        float power = reg * 20.f * this->current_lsb_ * 1e3f;

        return (power > 0.0f ? power : 0.0f);
    }

    uint8_t INA219::writeRegister_(uint8_t reg, uint16_t val)
    {
        uint8_t buffer[3];
        buffer[0] = reg;
        buffer[1] = val >> 8;
        buffer[2] = val & 0xff;

        if (write(this->i2c_fd_, buffer, 3) != 3) {
            return false;
        }

        return true;
    }

    uint16_t INA219::readRegister_(uint8_t reg) const
    {
        uint8_t buffer[1] = { reg };
        
        // Write register address
        if (write(this->i2c_fd_, buffer, 1) != 1) {
            return 0;
        }

        // Read 2 bytes
        uint8_t data[2];
        if (read(this->i2c_fd_, data, 2) != 2) {
            return 0;
        }

        return ((data[0] << 8) | data[1]);
    }
} /* namespace PowerBox */