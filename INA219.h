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

#ifndef INA219_H
#define INA219_H

#include <stdint.h>
#include <unistd.h>

#define INA219_CONFIG        0x00
#define INA219_SHUNT_VOLTAGE 0x01
#define INA219_BUS_VOLTAGE   0x02
#define INA219_POWER         0x03
#define INA219_CURRENT       0x04
#define INA219_CALIBRATION   0x05

namespace PowerBox
{
    enum
    {
        INA219_CONFIG_GAIN1 = 0x0000,
        INA219_CONFIG_GAIN2 = 0x0800,
        INA219_CONFIG_GAIN4 = 0x1000,
        INA219_CONFIG_GAIN8 = 0x1800
    };

    class INA219
    {
        public:
            INA219(const char* dev, uint8_t address);
            ~INA219(void);

            uint8_t begin(uint16_t gain, float current);

            float getShuntVoltage_mV(void) const;
            uint16_t getBusVoltage_mV(void) const;
            uint16_t getCurrent_mA(void) const;
            uint16_t getPower_mW(void) const;

        private:
            uint8_t writeRegister_(uint8_t reg, uint16_t val);
            uint16_t readRegister_(uint8_t reg) const;

            // I2C
            int i2c_fd_;
            const char* dev_;
            uint8_t address_;

            // Config
            uint16_t config_;

            // LSB
            float current_lsb_;
    };
} /* namespace PowerBox */

#endif // INA219_H