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

#include "MCP320X.h"
#include "common.h"
#include "SPIBus.h"
#include "GPIOManager.h"
#include <thread>
#include <chrono>
#include <vector>

namespace PowerBox
{
    template <uint8_t NCH, uint8_t BIT>
    MCP320X_<NCH, BIT>::MCP320X_(const GPIOManager& gpio)
    {
        this->gpio_ = &gpio;
        this->init_ = false;
        this->v_ref_ = 0.0f;

        this->spi_ = new SPIBus("/dev/spidev0.0");
    }

    template <uint8_t NCH, uint8_t BIT>
    MCP320X_<NCH, BIT>::~MCP320X_(void)
    {
        if(this->spi_) {
            delete this->spi_;
            this->spi_ = nullptr;
        }
        this->init_ = false;
        this->v_ref_ = 0.0f;
    }

    template <uint8_t NCH, uint8_t BIT>
    void MCP320X_<NCH, BIT>::begin(float vRef)
    {
        this->v_ref_ = vRef;

        this->spi_->begin(1000000, 8, 0);

        this->init_ = true;
    }

    template <uint8_t NCH, uint8_t BIT>
    uint8_t MCP320X_<NCH, BIT>::isConnected(void) const
    {
        return this->init_;
    }

    template <uint8_t NCH, uint8_t BIT>
    uint16_t MCP320X_<NCH, BIT>::analogRead(uint8_t ch) const
    {
        std::vector<uint8_t> tx;
        std::vector<uint8_t> rx;

        // Build the SPI command (matches Python daemon)
        tx.push_back(NCH == 2 ? 0x01 : 0x06);
        tx.push_back(NCH == 2 ? (ch == 0 ? 0xA0 : 0xE0) : (ch << 6));
        tx.push_back(0x00);

        // Perform single SPI transaction with all 3 bytes
        this->spi_->transfer(tx, rx);

        // Extract the 12-bit result from bytes 1 and 2
        uint8_t msb = rx[1];
        uint8_t lsb = rx[2];

        return ((msb << 8) | lsb) & 0xFFF;
    }

    template <uint8_t NCH, uint8_t BIT>
    uint16_t MCP320X_<NCH, BIT>::analogReadAverage(uint8_t ch, uint8_t n, uint16_t delay_us) const
    {
        float adc = 0.f;

        for(uint8_t i = 0; i < n; ++i)
        {
            adc += this->analogRead(ch);
            std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        }

        // Divide by n to average the samples
        adc /= n;

        // Obtain the analog value
        adc = adc * this->v_ref_ * 1e3f / (1 << BIT);

        // Convert it back to the analog value using digital range
        // and reference voltage
        return adc;
    }

    template class MCP320X_<4, 12>;
} /* namespace PowerBox */