#include "dex/disassembler.h"

#include <cstdio>

#include "util/bytes.h"

namespace dexe::dex {

namespace {

std::string reg(uint32_t r) { return "v" + std::to_string(r); }

std::string hex(uint64_t v, int width = 4) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*llx", width, static_cast<unsigned long long>(v));
    return buf;
}

std::string target(uint32_t pc, int64_t offset) {
    return (offset < 0 ? "-" : "+") + std::to_string(offset < 0 ? -offset : offset) + " (" +
           hex(uint64_t(int64_t(pc) + offset)) + ")";
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uint8_t(c) < 0x20) out += "\\x" + hex(uint8_t(c), 2);
                else out += c;
        }
    }
    return out + "\"";
}

}  // namespace

std::string describe_index(const DexFile& dex, IndexKind kind, uint32_t index) {
    try {
        switch (kind) {
            case IndexKind::kNone: return "";
            case IndexKind::kString: return quote(dex.string_utf8(index));
            case IndexKind::kType: return std::string(dex.type_descriptor(index));
            case IndexKind::kField: return dex.pretty_field(index);
            case IndexKind::kMethod: return dex.pretty_method(index);
            case IndexKind::kCallSite: return "call_site@" + std::to_string(index);
            case IndexKind::kMethodHandle: return "method_handle@" + std::to_string(index);
            case IndexKind::kProto: return dex.method_signature(index);
        }
    } catch (const FormatError&) {
    }
    return "<invalid index " + std::to_string(index) + ">";
}

std::string disassemble(const DexFile& dex, const Instruction& insn, uint32_t pc) {
    if (insn.is_payload) return "(data payload, " + std::to_string(insn.size) + " code units)";

    const OpcodeInfo& info = opcode_info(insn.opcode);
    if (!info.name) return "<unused opcode 0x" + hex(insn.opcode, 2) + ">";

    std::string s = info.name;
    auto idx = [&](uint32_t i) { return describe_index(dex, info.index_kind, i); };
    auto lit = [&] { return "#" + std::to_string(insn.literal); };
    auto arg_list = [&] {
        std::string list = "{";
        for (uint32_t i = 0; i < insn.a; ++i) list += (i ? ", " : "") + reg(insn.args[i]);
        return list + "}";
    };
    auto range_list = [&] {
        if (insn.a == 0) return std::string("{}");
        return "{" + reg(insn.c) + " .. " + reg(insn.c + insn.a - 1) + "}";
    };

    switch (info.format) {
        case Format::k10x: break;
        case Format::k12x: s += " " + reg(insn.a) + ", " + reg(insn.b); break;
        case Format::k11n: s += " " + reg(insn.a) + ", " + lit(); break;
        case Format::k11x: s += " " + reg(insn.a); break;
        case Format::k10t:
        case Format::k20t:
        case Format::k30t: s += " " + target(pc, insn.literal); break;
        case Format::k22x:
        case Format::k32x: s += " " + reg(insn.a) + ", " + reg(insn.b); break;
        case Format::k21t:
        case Format::k31t: s += " " + reg(insn.a) + ", " + target(pc, insn.literal); break;
        case Format::k21s:
        case Format::k21h:
        case Format::k31i:
        case Format::k51l: s += " " + reg(insn.a) + ", " + lit(); break;
        case Format::k21c:
        case Format::k31c: s += " " + reg(insn.a) + ", " + idx(insn.index); break;
        case Format::k23x: s += " " + reg(insn.a) + ", " + reg(insn.b) + ", " + reg(insn.c); break;
        case Format::k22b:
        case Format::k22s: s += " " + reg(insn.a) + ", " + reg(insn.b) + ", " + lit(); break;
        case Format::k22t: s += " " + reg(insn.a) + ", " + reg(insn.b) + ", " + target(pc, insn.literal); break;
        case Format::k22c: s += " " + reg(insn.a) + ", " + reg(insn.b) + ", " + idx(insn.index); break;
        case Format::k35c: s += " " + arg_list() + ", " + idx(insn.index); break;
        case Format::k3rc: s += " " + range_list() + ", " + idx(insn.index); break;
        case Format::k45cc:
            s += " " + arg_list() + ", " + idx(insn.index) + ", " + describe_index(dex, IndexKind::kProto, insn.proto);
            break;
        case Format::k4rcc:
            s += " " + range_list() + ", " + idx(insn.index) + ", " + describe_index(dex, IndexKind::kProto, insn.proto);
            break;
    }
    return s;
}

}  // namespace dexe::dex
