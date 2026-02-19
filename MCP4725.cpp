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

#include "MCP4725.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cmath>

#define MCP4725_MAX 4095

namespace PowerBox
{
    MCP4725::MCP4725(const char* dev, uint8_t address, float vref)
    {
        this->dev_ = dev;
        this->address_ = address;
        this->i2c_fd_ = -1;
        this->vref_ = vref;
    }

    MCP4725::~MCP4725(void)
    {
        if (this->i2c_fd_ >= 0) {
            close(this->i2c_fd_);
            this->i2c_fd_ = -1;
        }
    }

    uint8_t MCP4725::begin(void)
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

        this->current_val_ = readValue_();

        return true;
    }

    uint8_t MCP4725::setVoltage(float v)
    {
        return this->setValue_(round((v * MCP4725_MAX) / this->vref_));
    }

    float MCP4725::getVoltage(void)
    {
        return this->getValue_() * this->vref_ / MCP4725_MAX;
    }

    uint8_t MCP4725::setValue_(uint16_t val)
    {
        if(val == this->current_val_)
        {
            // Nothing to do
            return 0;
        }

        // Check for valid values
        if(val > MCP4725_MAX)
        {
            return 1;
        }

        return this->writeRegister_(val);
    }

    uint16_t MCP4725::readValue_(void)
    {
        uint8_t buffer[3];

        this->readRegister_(buffer, 3);

        uint16_t val = buffer[1];
        val <<= 4;
        val += (buffer[2] >> 4);

        return val;
    }

    uint8_t MCP4725::writeRegister_(uint16_t val)
    {
        uint8_t msb = (val / 16);
        uint8_t lsb = (val & 0x0F) << 4;

        uint8_t buffer[3] = { 0x60, msb, lsb };
        
        if (write(this->i2c_fd_, buffer, 3) != 3) {
            return 1;  // Error
        }

        return 0;  // Success
    }

    uint8_t MCP4725::readRegister_(uint8_t* buffer, uint8_t len)
    {
        ssize_t nbytes = read(this->i2c_fd_, buffer, len);

        if (nbytes < 0) {
            return 0;  // Error
        }

        return nbytes;
    }
} /* namespace PowerBox */