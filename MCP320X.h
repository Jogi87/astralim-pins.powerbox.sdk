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

#ifndef MCP320X_H
#define MCP320X_H

#include <stdint.h>

namespace PowerBox
{
    class SPIBus;
    class GPIOManager;

    template <uint8_t NCH, uint8_t BIT>
    class MCP320X_
    {
        public:
            MCP320X_(const GPIOManager& gpio);
            ~MCP320X_(void);

            void begin(float vRef);
            uint8_t isConnected(void) const;

            uint16_t analogRead(uint8_t ch) const;
            uint16_t analogReadAverage(uint8_t ch, uint8_t n, uint16_t delay = 0) const;

        private:
            SPIBus* spi_;
            const GPIOManager* gpio_;
            uint8_t init_;

            float v_ref_;
    };

    using MCP3204 = MCP320X_<4, 12>;
} /* namespace PowerBox */

#endif // MCP320X_H