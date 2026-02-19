/*
 * MIT License
 * Copyright (c) 2024 Kazuki Ota
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "stdint.h"
#include "string.h"

/**
* Convert between binary and base64 encoded string.
* @see https://github.com/dojyorin/arduino_base64
*/
namespace base64 {
    /**
    * Convert binary to base64 encoded string.
    * If input is string, cast to `uint8_t*`.
    * @example
    * ```c++
    * const uint8_t input[] = {0x17, 0x77, 0x3B, 0x11, 0x82, 0xA4, 0xC4, 0xC8};
    * auto inputLength = sizeof(input);
    * char output[base64::encodeLength(inputLength)];
    * base64::encode(input, inputLength, output);
    * ```
    */
    void encode(const uint8_t* input, size_t inputLength, char* output);

    /**
    * Calculate number of output characters.
    * @example
    * ```c++
    * const uint8_t input[] = {0x17, 0x77, 0x3B, 0x11, 0x82, 0xA4, 0xC4, 0xC8};
    * auto inputLength = sizeof(input);
    * char output[base64::encodeLength(inputLength)];
    * base64::encode(input, inputLength, output);
    * ```
    */
    size_t encodeLength(size_t inputLength);

    /**
    * Convert base64 encoded string to binary.
    * If output is string, cast to `char*`.
    * @example
    * ```c++
    * const char input[] = "F3c7EYKkxMgnvO0nB8FWVw==";
    * uint8_t output[base64::decodeLength(input)];
    * base64::decode(input, output);
    * ```
    */
    void decode(const char* input, uint8_t* output);

    /**
    * Calculate number of output bytes.
    * @example
    * ```c++
    * const char input[] = "F3c7EYKkxMgnvO0nB8FWVw==";
    * uint8_t output[base64::decodeLength(input)];
    * base64::decode(input, output);
    * ```
    */
    size_t decodeLength(const char* input);
}
