#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gt7 {

class Salsa20 {
public:
    Salsa20(const uint8_t* key);
    void setIv(const uint8_t* iv);
    void processBytes(const uint8_t* input, uint8_t* output, std::size_t byteCount);

private:
    static constexpr std::size_t kVectorSize = 16;
    static constexpr std::size_t kBlockSize = 64;

    static uint32_t rotate(uint32_t value, uint32_t bits);
    static uint32_t load32(const uint8_t* bytes);
    static void store32(uint32_t value, uint8_t* bytes);
    void generateKeyStream(uint8_t output[kBlockSize]);

    std::array<uint32_t, kVectorSize> state_{};
};

}