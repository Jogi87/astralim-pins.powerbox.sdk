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

#include "arduino_base64.hpp"
#include <string>
#include <vector>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {
    constexpr char CODE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    uint8_t indexOf(char search) {
        for(uint8_t i = 0; i < 64; i++) {
            if(::CODE[i] == search) {
                return i;
            }
        }

        return 0xFF;
    }

    void to6x4(uint8_t* input, uint8_t* output) {
        output[0] = (input[0] & 0xFC) >> 2;
        output[1] = ((input[0] & 0x03) << 4) + ((input[1] & 0xF0) >> 4);
        output[2] = ((input[1] & 0x0F) << 2) + ((input[2] & 0xC0) >> 6);
        output[3] = input[2] & 0x3F;
    }

    void to8x3(uint8_t* input, uint8_t* output) {
        output[0] = (input[0] << 2) + ((input[1] & 0x30) >> 4);
        output[1] = ((input[1] & 0x0F) << 4) + ((input[2] & 0x3C) >> 2);
        output[2] = ((input[2] & 0x03) << 6) + input[3];
    }
}

void base64::encode(const uint8_t* input, size_t inputLength, char* output) {
    uint8_t position = 0;
    uint8_t bit8x3[3] = {};
    uint8_t bit6x4[4] = {};

    while(inputLength--) {
        bit8x3[position++] = *input++;

        if(position == 3) {
            ::to6x4(bit8x3, bit6x4);

            for(const auto &v: bit6x4) {
                *output++ = ::CODE[v];
            }

            position = 0;
        }
    }

    if(position) {
        for(uint8_t i = position; i < 3; i++) {
            bit8x3[i] = 0x00;
        }

        ::to6x4(bit8x3, bit6x4);

        for(uint8_t i = 0; i < position + 1; i++) {
            *output++ = ::CODE[bit6x4[i]];
        }

        while(position++ < 3) {
            *output++ = '=';
        }
    }

    *output = '\0';
}

size_t base64::encodeLength(size_t inputLength) {
    return (inputLength + 2 - ((inputLength + 2) % 3)) / 3 * 4 + 1;
}

void base64::decode(const char* input, uint8_t* output) {
    auto inputLength = strlen(input);
    uint8_t position = 0;
    uint8_t bit8x3[3] = {};
    uint8_t bit6x4[4] = {};

    while(inputLength--) {
        if(*input == '=') {
            break;
        }

        bit6x4[position++] = ::indexOf(*input++);

        if(position == 4) {
            ::to8x3(bit6x4, bit8x3);

            for(const auto &v: bit8x3) {
                *output++ = v;
            }

            position = 0;
        }
    }

    if(position) {
        for(uint8_t i = position; i < 4; i++) {
            bit6x4[i] = 0x00;
        }

        ::to8x3(bit6x4, bit8x3);

        for(uint8_t i = 0; i < position - 1; i++) {
            *output++ = bit8x3[i];
        }
    }
}

size_t base64::decodeLength(const char* input) {
    auto inputLength = strlen(input);
    uint8_t equal = 0;

    input += inputLength - 1;

    while(*input-- == '=') {
        equal++;
    }

    return 6 * inputLength / 8 - equal;
}

/* ============================================================================
 * ENCRYPTION HELPER FUNCTIONS
 * ============================================================================ */

// Generate AES key and IV from UUID (matching device's GenerateAesKey)
static void GenerateAesKeyFromUUID(const char* uuid, uint8_t* key, uint8_t startIndex)
{
    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    // Parse hex string to binary bytes first
    uint8_t binaryUuid[16] = {0};
    int byteIdx = 0;
    int charIdx = 0;
    
    // Skip dashes and parse hex pairs
    while (charIdx < (int)strlen(uuid) && byteIdx < 16)
    {
        char c = uuid[charIdx];
        
        if (c == '-')
        {
            charIdx++;
            continue;
        }
        
        // Parse two hex characters
        if (charIdx + 1 < (int)strlen(uuid))
        {
            char nextc = uuid[charIdx + 1];
            if (nextc != '-')
            {
                std::string hexPair = std::string(1, c) + std::string(1, nextc);
                binaryUuid[byteIdx++] = (uint8_t)strtol(hexPair.c_str(), nullptr, 16);
                charIdx += 2;
                continue;
            }
        }
        charIdx++;
    }
    
    // Now convert bytes [startIndex : startIndex+8] to hex ASCII representation
    // This matches device's GenerateAesKey function
    int keyPos = 0;
    for (uint8_t i = startIndex; i < startIndex + 8 && i < 16; i++)
    {
        // High nibble
        key[keyPos++] = "0123456789ABCDEF"[binaryUuid[i] >> 4];
        // Low nibble
        key[keyPos++] = "0123456789ABCDEF"[binaryUuid[i] & 0x0F];
    }
}


// Encrypt password using AES-128-CBC with PKCS7 padding
bool base64::EncryptPassword(const char* plainPassword, const char* uuid, char* encryptedBase64Output)
{
    if (!plainPassword || !uuid || !encryptedBase64Output)
        return false;
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    // Generate key and IV from UUID
    uint8_t aesKey[16];
    uint8_t aesIv[16];
    
    GenerateAesKeyFromUUID(uuid, aesKey, 0);
    GenerateAesKeyFromUUID(uuid, aesIv, 8);

    // Allocate buffer for encrypted data (plaintext + block size for padding)
    int plainLen = strlen(plainPassword);
    std::vector<uint8_t> encryptedBytes(plainLen + EVP_MAX_BLOCK_LENGTH);
    int encryptedLen = 0;
    int tempLen = 0;

    // Encrypt
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, aesKey, aesIv))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (!EVP_EncryptUpdate(ctx, encryptedBytes.data(), &tempLen, (const uint8_t*)plainPassword, plainLen))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    encryptedLen = tempLen;

    if (!EVP_EncryptFinal_ex(ctx, encryptedBytes.data() + encryptedLen, &tempLen))
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    encryptedLen += tempLen;

    EVP_CIPHER_CTX_free(ctx);

    // Base64 encode the encrypted bytes
    size_t encodedLen = base64::encodeLength(encryptedLen);
    base64::encode(encryptedBytes.data(), encryptedLen, encryptedBase64Output);
    encryptedBase64Output[encodedLen] = '\0';

    return true;
}