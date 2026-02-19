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

#ifndef MCP4725_H
#define MCP4725_H

#include <stdint.h>

namespace PowerBox
{
    class MCP4725
    {
        public:
            MCP4725(const char* dev, uint8_t address, float vref);
            ~MCP4725(void);

            uint8_t begin(void);
            uint8_t isConnected(void);

            uint8_t setVoltage(float v);
            float getVoltage(void);

        private:
            uint8_t setValue_(uint16_t val);
            uint16_t getValue_(void) { return this->current_val_; }
            uint16_t readValue_(void);

            uint8_t writeRegister_(uint16_t val);
            uint8_t readRegister_(uint8_t* buffer, uint8_t len);

            // I2C
            int i2c_fd_;
            const char* dev_;
            uint8_t address_;

            float vref_;
            uint16_t current_val_;
    };
} /* namespace PowerBox */

#endif // MCP4725_H