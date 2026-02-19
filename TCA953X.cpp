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

#include "TCA953X.h"
#include "common.h"
#include "PowerBoxLogging.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

template <uint8_t PINS>
TCA953X_<PINS>::TCA953X_(const char* dev, uint8_t address)
{
    this->init_ = false;
    this->dev_ = dev;
    this->address_ = address;
    this->i2c_fd_ = -1;
}

template <uint8_t PINS>
TCA953X_<PINS>::~TCA953X_(void)
{
    if (this->i2c_fd_ >= 0) {
        close(this->i2c_fd_);
        this->i2c_fd_ = -1;
    }
    this->init_ = false;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::begin(void)
{
    if(this->init_)
    {
        return true;
    }

    // Open I2C device
    this->i2c_fd_ = open(this->dev_, O_RDWR);
    if (this->i2c_fd_ < 0)
    {
        PB_DEBUG("TCA953X failed to open %s", this->dev_);
        return false;
    }

    PB_DEBUG("TCA953X opened %s, fd=%d", this->dev_, this->i2c_fd_);

    // Set I2C slave address
    if (ioctl(this->i2c_fd_, I2C_SLAVE, this->address_) < 0) {
        PB_DEBUG("TCA953X failed to set I2C slave address 0x%02x", this->address_);
        ::close(this->i2c_fd_);
        this->i2c_fd_ = -1;
        return false;
    }

    PB_DEBUG("TCA953X configured to address 0x%02x", this->address_);
    this->init_ = true;
    return true;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::pinMode(uint8_t pin, uint8_t mode)
{
    if(pin >= PINS)
    {
        return false;
    }

    if((mode != INPUT) && (mode != OUTPUT))
    {
        return false;
    }

    uint8_t config = PINS > 8 ? (pin < 8 ? 0x06 : 0x07) : 0x03;
    uint8_t mask   = 1 << (pin < 8 ? pin : pin - 8);
    uint8_t val    = this->readRegister_(config);

    if(mode == INPUT)
    {
        val |= mask;
    }

    if(mode == OUTPUT)
    {
        val &= ~mask;
    }

    this->writeRegister_(config, val);

    return true;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::digitalWrite(uint8_t pin, uint8_t data)
{
    if(pin >= PINS)
    {
        return false;
    }

    uint8_t config = PINS > 8 ? (pin < 8 ? 0x02 : 0x03) : 0x01;
    uint8_t mask   = 1 << (pin < 8 ? pin : pin - 8);
    uint8_t val    = this->readRegister_(config);

    if(data)
    {
        val |= mask;
    }
    else
    {
        val &= ~mask;
    }

    this->writeRegister_(config, val);

    return true;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::digitalRead(uint8_t pin)
{
    if(pin >= PINS)
    {
        return false;
    }

    uint8_t config = PINS > 8 ? (pin < 8 ? 0x00 : 0x01) : 0x00;
    uint8_t mask   = 1 << (pin < 8 ? pin : pin - 8);
    uint8_t val    = this->readRegister_(config);

    if(val & mask)
    {
        return 1;
    }

    return 0;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::writeRegister_(uint8_t reg, uint8_t val)
{
    uint8_t buffer[2] = { reg, val };

    if (write(this->i2c_fd_, buffer, 2) != 2) {
        return false;
    }

    return true;
}

template <uint8_t PINS>
uint8_t TCA953X_<PINS>::readRegister_(uint8_t reg)
{
    if (this->i2c_fd_ < 0) {
        PB_DEBUG("TCA953X readRegister: i2c_fd_ is invalid (%d)", this->i2c_fd_);
        return 0;
    }

    // Write register address
    if (write(this->i2c_fd_, &reg, 1) != 1) {
        PB_DEBUG("TCA953X readRegister: failed to write register address 0x%02x", reg);
        return 0;
    }

    // Read 1 byte
    uint8_t data;
    if (read(this->i2c_fd_, &data, 1) != 1) {
        PB_DEBUG("TCA953X readRegister: failed to read register 0x%02x", reg);
        return 0;
    }

    return data;
}

template class TCA953X_<8>;
template class TCA953X_<16>;
