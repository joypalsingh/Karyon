#include "dex/instruction.h"

#include <array>

#include "util/bytes.h"

namespace dexe::dex {

namespace {

constexpr std::array<OpcodeInfo, 256> make_opcode_table() {
    std::array<OpcodeInfo, 256> table{};
    for (auto& info : table) info = {nullptr, Format::k10x, IndexKind::kNone};
#define DEXE_OPCODE_INFO(value, name, mnemonic, format, index) \
    table[value] = {mnemonic, Format::format, IndexKind::index};
    DEXE_DEX_OPCODES(DEXE_OPCODE_INFO)
#undef DEXE_OPCODE_INFO
    return table;
}

constexpr auto kOpcodeTable = make_opcode_table();

constexpr uint32_t format_size(Format f) {
    switch (f) {
        case Format::k10x: case Format::k12x: case Format::k11n: case Format::k11x: case Format::k10t:
            return 1;
        case Format::k20t: case Format::k22x: case Format::k21t: case Format::k21s: case Format::k21h:
        case Format::k21c: case Format::k23x: case Format::k22b: case Format::k22t: case Format::k22s:
        case Format::k22c:
            return 2;
        case Format::k30t: case Format::k32x: case Format::k31i: case Format::k31t: case Format::k31c:
        case Format::k35c: case Format::k3rc:
            return 3;
        case Format::k45cc: case Format::k4rcc:
            return 4;
        case Format::k51l:
            return 5;
    }
    return 1;
}

}  // namespace

const OpcodeInfo& opcode_info(uint8_t opcode) { return kOpcodeTable[opcode]; }

Instruction decode(std::span<const uint16_t> code, uint32_t pc) {
    if (pc >= code.size()) throw FormatError("instruction address out of range");
    Instruction insn;
    uint16_t unit0 = code[pc];
    insn.opcode = uint8_t(unit0 & 0xff);

    // Inline data payloads share opcode 0x00 (nop) with a non-zero identifier in the high byte.
    if (insn.opcode == NOP && unit0 != 0) {
        auto u = [&](uint32_t i) -> uint32_t {
            if (pc + i >= code.size()) throw FormatError("payload runs past end of code");
            return code[pc + i];
        };
        insn.is_payload = true;
        switch (unit0) {
            case kPackedSwitchPayload: insn.size = u(1) * 2 + 4; break;
            case kSparseSwitchPayload: insn.size = u(1) * 4 + 2; break;
            case kFillArrayDataPayload: {
                uint32_t width = u(1);
                uint32_t count = u(2) | u(3) << 16;
                insn.size = uint32_t((uint64_t(count) * width + 1) / 2 + 4);
                break;
            }
            default: insn.is_payload = false; break;  // treat as a plain nop
        }
        if (insn.is_payload) {
            if (uint64_t(pc) + insn.size > code.size()) throw FormatError("payload runs past end of code");
            return insn;
        }
    }

    const OpcodeInfo& info = kOpcodeTable[insn.opcode];
    insn.size = format_size(info.format);
    if (uint64_t(pc) + insn.size > code.size()) throw FormatError("instruction runs past end of code");
    const uint16_t* p = &code[pc];
    uint32_t aa = unit0 >> 8;
    uint32_t a4 = (unit0 >> 8) & 0xf;
    uint32_t b4 = unit0 >> 12;
    auto u32 = [&](int i) { return uint32_t(p[i]) | uint32_t(p[i + 1]) << 16; };

    switch (info.format) {
        case Format::k10x: break;
        case Format::k12x: insn.a = a4; insn.b = b4; break;
        case Format::k11n: insn.a = a4; insn.literal = int16_t(unit0) >> 12; break;
        case Format::k11x: insn.a = aa; break;
        case Format::k10t: insn.literal = int8_t(aa); break;
        case Format::k20t: insn.literal = int16_t(p[1]); break;
        case Format::k22x: insn.a = aa; insn.b = p[1]; break;
        case Format::k21t:
        case Format::k21s: insn.a = aa; insn.literal = int16_t(p[1]); break;
        case Format::k21h:
            insn.a = aa;
            insn.literal = insn.opcode == CONST_WIDE_HIGH16 ? int64_t(uint64_t(p[1]) << 48) : int64_t(int32_t(uint32_t(p[1]) << 16));
            break;
        case Format::k21c: insn.a = aa; insn.index = p[1]; break;
        case Format::k23x: insn.a = aa; insn.b = p[1] & 0xff; insn.c = p[1] >> 8; break;
        case Format::k22b: insn.a = aa; insn.b = p[1] & 0xff; insn.literal = int8_t(p[1] >> 8); break;
        case Format::k22t:
        case Format::k22s: insn.a = a4; insn.b = b4; insn.literal = int16_t(p[1]); break;
        case Format::k22c: insn.a = a4; insn.b = b4; insn.index = p[1]; break;
        case Format::k30t: insn.literal = int32_t(u32(1)); break;
        case Format::k32x: insn.a = p[1]; insn.b = p[2]; break;
        case Format::k31i:
        case Format::k31t: insn.a = aa; insn.literal = int32_t(u32(1)); break;
        case Format::k31c: insn.a = aa; insn.index = u32(1); break;
        case Format::k35c:
        case Format::k45cc:
            insn.a = b4;  // argument count
            insn.index = p[1];
            insn.args[0] = p[2] & 0xf;
            insn.args[1] = (p[2] >> 4) & 0xf;
            insn.args[2] = (p[2] >> 8) & 0xf;
            insn.args[3] = p[2] >> 12;
            insn.args[4] = uint16_t(a4);
            if (insn.a > 5) throw FormatError("invoke with more than 5 arguments");
            if (info.format == Format::k45cc) insn.proto = p[3];
            break;
        case Format::k3rc:
        case Format::k4rcc:
            insn.a = aa;  // argument count
            insn.index = p[1];
            insn.c = p[2];  // first argument register
            if (info.format == Format::k4rcc) insn.proto = p[3];
            break;
        case Format::k51l:
            insn.a = aa;
            insn.literal = int64_t(uint64_t(u32(1)) | uint64_t(u32(3)) << 32);
            break;
    }
    return insn;
}

}  // namespace dexe::dex
