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

#ifndef SPI_H
#define SPI_H

#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>

namespace PowerBox
{
    class SPIBus {
        private:
            int fd;
            uint8_t mode = 0;
            uint8_t bits = 8;
            uint32_t speed = 1000000; // 1MHz

        public:
            SPIBus(const char* device) {
                fd = open(device, O_RDWR);
                if (fd < 0) {
                    perror("Failed to open SPI device");
                    exit(1);
                }
            }

            ~SPIBus() { close(fd); }

            void begin(uint32_t speed, uint8_t bits, uint8_t mode)
            {
                this->speed = speed;
                this->bits = bits;
                this->mode = mode;

                // Set SPI mode, bits per word, and max speed
                ioctl(fd, SPI_IOC_WR_MODE, &mode);
                ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
                ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
            }

            void transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx) {
                if (rx.size() < tx.size()) {
                    rx.resize(tx.size());
                }

                struct spi_ioc_transfer tr = {};
                tr.tx_buf = (unsigned long)tx.data();
                tr.rx_buf = (unsigned long)rx.data();
                tr.len = tx.size();
                tr.delay_usecs = 0;
                tr.speed_hz = speed;
                tr.bits_per_word = bits;

                if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
                    perror("SPI transfer failed");
                }
            }
    };
} /* namespace PowerBox */

#endif /* SPI_H */