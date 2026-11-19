#include "util/checksum.h"

#include <array>

namespace dexe {

namespace {

constexpr std::array<uint32_t, 256> make_crc_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

constexpr auto kCrcTable = make_crc_table();

}  // namespace

uint32_t crc32(std::span<const uint8_t> data) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = kCrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32(std::span<const uint8_t> data) {
    constexpr uint32_t kMod = 65521;
    uint32_t a = 1, b = 0;
    size_t i = 0;
    while (i < data.size()) {
        // 5552 is the largest block size for which b cannot overflow 32 bits.
        size_t end = i + 5552 < data.size() ? i + 5552 : data.size();
        for (; i < end; ++i) {
            a += data[i];
            b += a;
        }
        a %= kMod;
        b %= kMod;
    }
    return (b << 16) | a;
}

}  // namespace dexe
