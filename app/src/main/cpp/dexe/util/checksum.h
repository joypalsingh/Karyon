#pragma once

#include <cstdint>
#include <span>

namespace dexe {

uint32_t crc32(std::span<const uint8_t> data);
uint32_t adler32(std::span<const uint8_t> data);

}  // namespace dexe
