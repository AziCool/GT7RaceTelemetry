#include "salsa20.h"

#include <cstring>

namespace gt7 {

namespace {
constexpr char kConstants[] = "expand 32-byte k";
}

Salsa20::Salsa20(const uint8_t* key) {
    state_.fill(0);
    std::memcpy(&state_[0], kConstants, 4);
    std::memcpy(&state_[5], kConstants + 4, 4);
    std::memcpy(&state_[10], kConstants + 8, 4);
    std::memcpy(&state_[15], kConstants + 12, 4);
    for (std::size_t index = 0; index < 4; ++index) {
        state_[index + 1] = load32(key + (index * 4));
        state_[index + 11] = load32(key + ((index + 4) * 4));
    }
}

void Salsa20::setIv(const uint8_t* iv) {
    state_[6] = load32(iv);
    state_[7] = load32(iv + 4);
    state_[8] = 0;
    state_[9] = 0;
}

void Salsa20::processBytes(const uint8_t* input, uint8_t* output, std::size_t byteCount) {
    uint8_t keyStream[kBlockSize];
    while (byteCount > 0) {
        generateKeyStream(keyStream);
        const std::size_t blockBytes = byteCount < kBlockSize ? byteCount : kBlockSize;
        for (std::size_t index = 0; index < blockBytes; ++index) {
            output[index] = input[index] ^ keyStream[index];
        }
        input += blockBytes;
        output += blockBytes;
        byteCount -= blockBytes;
    }
}

void Salsa20::generateKeyStream(uint8_t output[kBlockSize]) {
    auto working = state_;
    for (int round = 20; round > 0; round -= 2) {
        working[4] ^= rotate(working[0] + working[12], 7); working[8] ^= rotate(working[4] + working[0], 9); working[12] ^= rotate(working[8] + working[4], 13); working[0] ^= rotate(working[12] + working[8], 18);
        working[9] ^= rotate(working[5] + working[1], 7); working[13] ^= rotate(working[9] + working[5], 9); working[1] ^= rotate(working[13] + working[9], 13); working[5] ^= rotate(working[1] + working[13], 18);
        working[14] ^= rotate(working[10] + working[6], 7); working[2] ^= rotate(working[14] + working[10], 9); working[6] ^= rotate(working[2] + working[14], 13); working[10] ^= rotate(working[6] + working[2], 18);
        working[3] ^= rotate(working[15] + working[11], 7); working[7] ^= rotate(working[3] + working[15], 9); working[11] ^= rotate(working[7] + working[3], 13); working[15] ^= rotate(working[11] + working[7], 18);
        working[1] ^= rotate(working[0] + working[3], 7); working[2] ^= rotate(working[1] + working[0], 9); working[3] ^= rotate(working[2] + working[1], 13); working[0] ^= rotate(working[3] + working[2], 18);
        working[6] ^= rotate(working[5] + working[4], 7); working[7] ^= rotate(working[6] + working[5], 9); working[4] ^= rotate(working[7] + working[6], 13); working[5] ^= rotate(working[4] + working[7], 18);
        working[11] ^= rotate(working[10] + working[9], 7); working[8] ^= rotate(working[11] + working[10], 9); working[9] ^= rotate(working[8] + working[11], 13); working[10] ^= rotate(working[9] + working[8], 18);
        working[12] ^= rotate(working[15] + working[14], 7); working[13] ^= rotate(working[12] + working[15], 9); working[14] ^= rotate(working[13] + working[12], 13); working[15] ^= rotate(working[14] + working[13], 18);
    }
    for (std::size_t index = 0; index < kVectorSize; ++index) {
        store32(working[index] + state_[index], output + (index * 4));
    }
    ++state_[8];
    if (state_[8] == 0) {
        ++state_[9];
    }
}

uint32_t Salsa20::rotate(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32 - bits));
}

uint32_t Salsa20::load32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void Salsa20::store32(uint32_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

}