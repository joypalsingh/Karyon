#pragma once

#include <cstdint>
#include <string>

#include "dex/dex_file.h"
#include "dex/instruction.h"

namespace dexe::dex {

// Renders one decoded instruction (located at code unit `pc`) in smali-like text.
std::string disassemble(const DexFile& dex, const Instruction& insn, uint32_t pc);

// Renders a constant-pool reference of the given kind, e.g. a quoted string or a method signature.
std::string describe_index(const DexFile& dex, IndexKind kind, uint32_t index);

}  // namespace dexe::dex
