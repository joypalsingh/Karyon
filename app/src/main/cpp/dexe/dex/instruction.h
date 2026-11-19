#pragma once

#ifdef CONST
#undef CONST
#endif

#include <cstdint>
#include <span>

namespace dexe::dex {

// Instruction encoding formats, named as in the Dalvik bytecode spec
// (https://source.android.com/docs/core/runtime/instruction-formats).
enum class Format : uint8_t {
    k10x, k12x, k11n, k11x, k10t, k20t, k22x, k21t, k21s, k21h, k21c, k23x, k22b, k22t, k22s, k22c,
    k30t, k32x, k31i, k31t, k31c, k35c, k3rc, k45cc, k4rcc, k51l,
};

// What kind of constant-pool entry an instruction's index operand refers to.
enum class IndexKind : uint8_t { kNone, kString, kType, kField, kMethod, kCallSite, kMethodHandle, kProto };

// V(value, ENUM_NAME, "mnemonic", format, index kind)
#define DEXE_DEX_OPCODES(V)                                                      \
    V(0x00, NOP, "nop", k10x, kNone)                                              \
    V(0x01, MOVE, "move", k12x, kNone)                                            \
    V(0x02, MOVE_FROM16, "move/from16", k22x, kNone)                              \
    V(0x03, MOVE_16, "move/16", k32x, kNone)                                      \
    V(0x04, MOVE_WIDE, "move-wide", k12x, kNone)                                  \
    V(0x05, MOVE_WIDE_FROM16, "move-wide/from16", k22x, kNone)                    \
    V(0x06, MOVE_WIDE_16, "move-wide/16", k32x, kNone)                            \
    V(0x07, MOVE_OBJECT, "move-object", k12x, kNone)                              \
    V(0x08, MOVE_OBJECT_FROM16, "move-object/from16", k22x, kNone)                \
    V(0x09, MOVE_OBJECT_16, "move-object/16", k32x, kNone)                        \
    V(0x0a, MOVE_RESULT, "move-result", k11x, kNone)                              \
    V(0x0b, MOVE_RESULT_WIDE, "move-result-wide", k11x, kNone)                    \
    V(0x0c, MOVE_RESULT_OBJECT, "move-result-object", k11x, kNone)                \
    V(0x0d, MOVE_EXCEPTION, "move-exception", k11x, kNone)                        \
    V(0x0e, RETURN_VOID, "return-void", k10x, kNone)                              \
    V(0x0f, RETURN, "return", k11x, kNone)                                        \
    V(0x10, RETURN_WIDE, "return-wide", k11x, kNone)                              \
    V(0x11, RETURN_OBJECT, "return-object", k11x, kNone)                          \
    V(0x12, CONST_4, "const/4", k11n, kNone)                                      \
    V(0x13, CONST_16, "const/16", k21s, kNone)                                    \
    V(0x14, CONST, "const", k31i, kNone)                                          \
    V(0x15, CONST_HIGH16, "const/high16", k21h, kNone)                            \
    V(0x16, CONST_WIDE_16, "const-wide/16", k21s, kNone)                          \
    V(0x17, CONST_WIDE_32, "const-wide/32", k31i, kNone)                          \
    V(0x18, CONST_WIDE, "const-wide", k51l, kNone)                                \
    V(0x19, CONST_WIDE_HIGH16, "const-wide/high16", k21h, kNone)                  \
    V(0x1a, CONST_STRING, "const-string", k21c, kString)                          \
    V(0x1b, CONST_STRING_JUMBO, "const-string/jumbo", k31c, kString)              \
    V(0x1c, CONST_CLASS, "const-class", k21c, kType)                              \
    V(0x1d, MONITOR_ENTER, "monitor-enter", k11x, kNone)                          \
    V(0x1e, MONITOR_EXIT, "monitor-exit", k11x, kNone)                            \
    V(0x1f, CHECK_CAST, "check-cast", k21c, kType)                                \
    V(0x20, INSTANCE_OF, "instance-of", k22c, kType)                              \
    V(0x21, ARRAY_LENGTH, "array-length", k12x, kNone)                            \
    V(0x22, NEW_INSTANCE, "new-instance", k21c, kType)                            \
    V(0x23, NEW_ARRAY, "new-array", k22c, kType)                                  \
    V(0x24, FILLED_NEW_ARRAY, "filled-new-array", k35c, kType)                    \
    V(0x25, FILLED_NEW_ARRAY_RANGE, "filled-new-array/range", k3rc, kType)        \
    V(0x26, FILL_ARRAY_DATA, "fill-array-data", k31t, kNone)                      \
    V(0x27, THROW, "throw", k11x, kNone)                                          \
    V(0x28, GOTO, "goto", k10t, kNone)                                            \
    V(0x29, GOTO_16, "goto/16", k20t, kNone)                                      \
    V(0x2a, GOTO_32, "goto/32", k30t, kNone)                                      \
    V(0x2b, PACKED_SWITCH, "packed-switch", k31t, kNone)                          \
    V(0x2c, SPARSE_SWITCH, "sparse-switch", k31t, kNone)                          \
    V(0x2d, CMPL_FLOAT, "cmpl-float", k23x, kNone)                                \
    V(0x2e, CMPG_FLOAT, "cmpg-float", k23x, kNone)                                \
    V(0x2f, CMPL_DOUBLE, "cmpl-double", k23x, kNone)                              \
    V(0x30, CMPG_DOUBLE, "cmpg-double", k23x, kNone)                              \
    V(0x31, CMP_LONG, "cmp-long", k23x, kNone)                                    \
    V(0x32, IF_EQ, "if-eq", k22t, kNone)                                          \
    V(0x33, IF_NE, "if-ne", k22t, kNone)                                          \
    V(0x34, IF_LT, "if-lt", k22t, kNone)                                          \
    V(0x35, IF_GE, "if-ge", k22t, kNone)                                          \
    V(0x36, IF_GT, "if-gt", k22t, kNone)                                          \
    V(0x37, IF_LE, "if-le", k22t, kNone)                                          \
    V(0x38, IF_EQZ, "if-eqz", k21t, kNone)                                        \
    V(0x39, IF_NEZ, "if-nez", k21t, kNone)                                        \
    V(0x3a, IF_LTZ, "if-ltz", k21t, kNone)                                        \
    V(0x3b, IF_GEZ, "if-gez", k21t, kNone)                                        \
    V(0x3c, IF_GTZ, "if-gtz", k21t, kNone)                                        \
    V(0x3d, IF_LEZ, "if-lez", k21t, kNone)                                        \
    V(0x44, AGET, "aget", k23x, kNone)                                            \
    V(0x45, AGET_WIDE, "aget-wide", k23x, kNone)                                  \
    V(0x46, AGET_OBJECT, "aget-object", k23x, kNone)                              \
    V(0x47, AGET_BOOLEAN, "aget-boolean", k23x, kNone)                            \
    V(0x48, AGET_BYTE, "aget-byte", k23x, kNone)                                  \
    V(0x49, AGET_CHAR, "aget-char", k23x, kNone)                                  \
    V(0x4a, AGET_SHORT, "aget-short", k23x, kNone)                                \
    V(0x4b, APUT, "aput", k23x, kNone)                                            \
    V(0x4c, APUT_WIDE, "aput-wide", k23x, kNone)                                  \
    V(0x4d, APUT_OBJECT, "aput-object", k23x, kNone)                              \
    V(0x4e, APUT_BOOLEAN, "aput-boolean", k23x, kNone)                            \
    V(0x4f, APUT_BYTE, "aput-byte", k23x, kNone)                                  \
    V(0x50, APUT_CHAR, "aput-char", k23x, kNone)                                  \
    V(0x51, APUT_SHORT, "aput-short", k23x, kNone)                                \
    V(0x52, IGET, "iget", k22c, kField)                                           \
    V(0x53, IGET_WIDE, "iget-wide", k22c, kField)                                 \
    V(0x54, IGET_OBJECT, "iget-object", k22c, kField)                             \
    V(0x55, IGET_BOOLEAN, "iget-boolean", k22c, kField)                           \
    V(0x56, IGET_BYTE, "iget-byte", k22c, kField)                                 \
    V(0x57, IGET_CHAR, "iget-char", k22c, kField)                                 \
    V(0x58, IGET_SHORT, "iget-short", k22c, kField)                               \
    V(0x59, IPUT, "iput", k22c, kField)                                           \
    V(0x5a, IPUT_WIDE, "iput-wide", k22c, kField)                                 \
    V(0x5b, IPUT_OBJECT, "iput-object", k22c, kField)                             \
    V(0x5c, IPUT_BOOLEAN, "iput-boolean", k22c, kField)                           \
    V(0x5d, IPUT_BYTE, "iput-byte", k22c, kField)                                 \
    V(0x5e, IPUT_CHAR, "iput-char", k22c, kField)                                 \
    V(0x5f, IPUT_SHORT, "iput-short", k22c, kField)                               \
    V(0x60, SGET, "sget", k21c, kField)                                           \
    V(0x61, SGET_WIDE, "sget-wide", k21c, kField)                                 \
    V(0x62, SGET_OBJECT, "sget-object", k21c, kField)                             \
    V(0x63, SGET_BOOLEAN, "sget-boolean", k21c, kField)                           \
    V(0x64, SGET_BYTE, "sget-byte", k21c, kField)                                 \
    V(0x65, SGET_CHAR, "sget-char", k21c, kField)                                 \
    V(0x66, SGET_SHORT, "sget-short", k21c, kField)                               \
    V(0x67, SPUT, "sput", k21c, kField)                                           \
    V(0x68, SPUT_WIDE, "sput-wide", k21c, kField)                                 \
    V(0x69, SPUT_OBJECT, "sput-object", k21c, kField)                             \
    V(0x6a, SPUT_BOOLEAN, "sput-boolean", k21c, kField)                           \
    V(0x6b, SPUT_BYTE, "sput-byte", k21c, kField)                                 \
    V(0x6c, SPUT_CHAR, "sput-char", k21c, kField)                                 \
    V(0x6d, SPUT_SHORT, "sput-short", k21c, kField)                               \
    V(0x6e, INVOKE_VIRTUAL, "invoke-virtual", k35c, kMethod)                      \
    V(0x6f, INVOKE_SUPER, "invoke-super", k35c, kMethod)                          \
    V(0x70, INVOKE_DIRECT, "invoke-direct", k35c, kMethod)                        \
    V(0x71, INVOKE_STATIC, "invoke-static", k35c, kMethod)                        \
    V(0x72, INVOKE_INTERFACE, "invoke-interface", k35c, kMethod)                  \
    V(0x74, INVOKE_VIRTUAL_RANGE, "invoke-virtual/range", k3rc, kMethod)          \
    V(0x75, INVOKE_SUPER_RANGE, "invoke-super/range", k3rc, kMethod)              \
    V(0x76, INVOKE_DIRECT_RANGE, "invoke-direct/range", k3rc, kMethod)            \
    V(0x77, INVOKE_STATIC_RANGE, "invoke-static/range", k3rc, kMethod)            \
    V(0x78, INVOKE_INTERFACE_RANGE, "invoke-interface/range", k3rc, kMethod)      \
    V(0x7b, NEG_INT, "neg-int", k12x, kNone)                                      \
    V(0x7c, NOT_INT, "not-int", k12x, kNone)                                      \
    V(0x7d, NEG_LONG, "neg-long", k12x, kNone)                                    \
    V(0x7e, NOT_LONG, "not-long", k12x, kNone)                                    \
    V(0x7f, NEG_FLOAT, "neg-float", k12x, kNone)                                  \
    V(0x80, NEG_DOUBLE, "neg-double", k12x, kNone)                                \
    V(0x81, INT_TO_LONG, "int-to-long", k12x, kNone)                              \
    V(0x82, INT_TO_FLOAT, "int-to-float", k12x, kNone)                            \
    V(0x83, INT_TO_DOUBLE, "int-to-double", k12x, kNone)                          \
    V(0x84, LONG_TO_INT, "long-to-int", k12x, kNone)                              \
    V(0x85, LONG_TO_FLOAT, "long-to-float", k12x, kNone)                          \
    V(0x86, LONG_TO_DOUBLE, "long-to-double", k12x, kNone)                        \
    V(0x87, FLOAT_TO_INT, "float-to-int", k12x, kNone)                            \
    V(0x88, FLOAT_TO_LONG, "float-to-long", k12x, kNone)                          \
    V(0x89, FLOAT_TO_DOUBLE, "float-to-double", k12x, kNone)                      \
    V(0x8a, DOUBLE_TO_INT, "double-to-int", k12x, kNone)                          \
    V(0x8b, DOUBLE_TO_LONG, "double-to-long", k12x, kNone)                        \
    V(0x8c, DOUBLE_TO_FLOAT, "double-to-float", k12x, kNone)                      \
    V(0x8d, INT_TO_BYTE, "int-to-byte", k12x, kNone)                              \
    V(0x8e, INT_TO_CHAR, "int-to-char", k12x, kNone)                              \
    V(0x8f, INT_TO_SHORT, "int-to-short", k12x, kNone)                            \
    V(0x90, ADD_INT, "add-int", k23x, kNone)                                      \
    V(0x91, SUB_INT, "sub-int", k23x, kNone)                                      \
    V(0x92, MUL_INT, "mul-int", k23x, kNone)                                      \
    V(0x93, DIV_INT, "div-int", k23x, kNone)                                      \
    V(0x94, REM_INT, "rem-int", k23x, kNone)                                      \
    V(0x95, AND_INT, "and-int", k23x, kNone)                                      \
    V(0x96, OR_INT, "or-int", k23x, kNone)                                        \
    V(0x97, XOR_INT, "xor-int", k23x, kNone)                                      \
    V(0x98, SHL_INT, "shl-int", k23x, kNone)                                      \
    V(0x99, SHR_INT, "shr-int", k23x, kNone)                                      \
    V(0x9a, USHR_INT, "ushr-int", k23x, kNone)                                    \
    V(0x9b, ADD_LONG, "add-long", k23x, kNone)                                    \
    V(0x9c, SUB_LONG, "sub-long", k23x, kNone)                                    \
    V(0x9d, MUL_LONG, "mul-long", k23x, kNone)                                    \
    V(0x9e, DIV_LONG, "div-long", k23x, kNone)                                    \
    V(0x9f, REM_LONG, "rem-long", k23x, kNone)                                    \
    V(0xa0, AND_LONG, "and-long", k23x, kNone)                                    \
    V(0xa1, OR_LONG, "or-long", k23x, kNone)                                      \
    V(0xa2, XOR_LONG, "xor-long", k23x, kNone)                                    \
    V(0xa3, SHL_LONG, "shl-long", k23x, kNone)                                    \
    V(0xa4, SHR_LONG, "shr-long", k23x, kNone)                                    \
    V(0xa5, USHR_LONG, "ushr-long", k23x, kNone)                                  \
    V(0xa6, ADD_FLOAT, "add-float", k23x, kNone)                                  \
    V(0xa7, SUB_FLOAT, "sub-float", k23x, kNone)                                  \
    V(0xa8, MUL_FLOAT, "mul-float", k23x, kNone)                                  \
    V(0xa9, DIV_FLOAT, "div-float", k23x, kNone)                                  \
    V(0xaa, REM_FLOAT, "rem-float", k23x, kNone)                                  \
    V(0xab, ADD_DOUBLE, "add-double", k23x, kNone)                                \
    V(0xac, SUB_DOUBLE, "sub-double", k23x, kNone)                                \
    V(0xad, MUL_DOUBLE, "mul-double", k23x, kNone)                                \
    V(0xae, DIV_DOUBLE, "div-double", k23x, kNone)                                \
    V(0xaf, REM_DOUBLE, "rem-double", k23x, kNone)                                \
    V(0xb0, ADD_INT_2ADDR, "add-int/2addr", k12x, kNone)                          \
    V(0xb1, SUB_INT_2ADDR, "sub-int/2addr", k12x, kNone)                          \
    V(0xb2, MUL_INT_2ADDR, "mul-int/2addr", k12x, kNone)                          \
    V(0xb3, DIV_INT_2ADDR, "div-int/2addr", k12x, kNone)                          \
    V(0xb4, REM_INT_2ADDR, "rem-int/2addr", k12x, kNone)                          \
    V(0xb5, AND_INT_2ADDR, "and-int/2addr", k12x, kNone)                          \
    V(0xb6, OR_INT_2ADDR, "or-int/2addr", k12x, kNone)                            \
    V(0xb7, XOR_INT_2ADDR, "xor-int/2addr", k12x, kNone)                          \
    V(0xb8, SHL_INT_2ADDR, "shl-int/2addr", k12x, kNone)                          \
    V(0xb9, SHR_INT_2ADDR, "shr-int/2addr", k12x, kNone)                          \
    V(0xba, USHR_INT_2ADDR, "ushr-int/2addr", k12x, kNone)                        \
    V(0xbb, ADD_LONG_2ADDR, "add-long/2addr", k12x, kNone)                        \
    V(0xbc, SUB_LONG_2ADDR, "sub-long/2addr", k12x, kNone)                        \
    V(0xbd, MUL_LONG_2ADDR, "mul-long/2addr", k12x, kNone)                        \
    V(0xbe, DIV_LONG_2ADDR, "div-long/2addr", k12x, kNone)                        \
    V(0xbf, REM_LONG_2ADDR, "rem-long/2addr", k12x, kNone)                        \
    V(0xc0, AND_LONG_2ADDR, "and-long/2addr", k12x, kNone)                        \
    V(0xc1, OR_LONG_2ADDR, "or-long/2addr", k12x, kNone)                          \
    V(0xc2, XOR_LONG_2ADDR, "xor-long/2addr", k12x, kNone)                        \
    V(0xc3, SHL_LONG_2ADDR, "shl-long/2addr", k12x, kNone)                        \
    V(0xc4, SHR_LONG_2ADDR, "shr-long/2addr", k12x, kNone)                        \
    V(0xc5, USHR_LONG_2ADDR, "ushr-long/2addr", k12x, kNone)                      \
    V(0xc6, ADD_FLOAT_2ADDR, "add-float/2addr", k12x, kNone)                      \
    V(0xc7, SUB_FLOAT_2ADDR, "sub-float/2addr", k12x, kNone)                      \
    V(0xc8, MUL_FLOAT_2ADDR, "mul-float/2addr", k12x, kNone)                      \
    V(0xc9, DIV_FLOAT_2ADDR, "div-float/2addr", k12x, kNone)                      \
    V(0xca, REM_FLOAT_2ADDR, "rem-float/2addr", k12x, kNone)                      \
    V(0xcb, ADD_DOUBLE_2ADDR, "add-double/2addr", k12x, kNone)                    \
    V(0xcc, SUB_DOUBLE_2ADDR, "sub-double/2addr", k12x, kNone)                    \
    V(0xcd, MUL_DOUBLE_2ADDR, "mul-double/2addr", k12x, kNone)                    \
    V(0xce, DIV_DOUBLE_2ADDR, "div-double/2addr", k12x, kNone)                    \
    V(0xcf, REM_DOUBLE_2ADDR, "rem-double/2addr", k12x, kNone)                    \
    V(0xd0, ADD_INT_LIT16, "add-int/lit16", k22s, kNone)                          \
    V(0xd1, RSUB_INT, "rsub-int", k22s, kNone)                                    \
    V(0xd2, MUL_INT_LIT16, "mul-int/lit16", k22s, kNone)                          \
    V(0xd3, DIV_INT_LIT16, "div-int/lit16", k22s, kNone)                          \
    V(0xd4, REM_INT_LIT16, "rem-int/lit16", k22s, kNone)                          \
    V(0xd5, AND_INT_LIT16, "and-int/lit16", k22s, kNone)                          \
    V(0xd6, OR_INT_LIT16, "or-int/lit16", k22s, kNone)                            \
    V(0xd7, XOR_INT_LIT16, "xor-int/lit16", k22s, kNone)                          \
    V(0xd8, ADD_INT_LIT8, "add-int/lit8", k22b, kNone)                            \
    V(0xd9, RSUB_INT_LIT8, "rsub-int/lit8", k22b, kNone)                          \
    V(0xda, MUL_INT_LIT8, "mul-int/lit8", k22b, kNone)                            \
    V(0xdb, DIV_INT_LIT8, "div-int/lit8", k22b, kNone)                            \
    V(0xdc, REM_INT_LIT8, "rem-int/lit8", k22b, kNone)                            \
    V(0xdd, AND_INT_LIT8, "and-int/lit8", k22b, kNone)                            \
    V(0xde, OR_INT_LIT8, "or-int/lit8", k22b, kNone)                              \
    V(0xdf, XOR_INT_LIT8, "xor-int/lit8", k22b, kNone)                            \
    V(0xe0, SHL_INT_LIT8, "shl-int/lit8", k22b, kNone)                            \
    V(0xe1, SHR_INT_LIT8, "shr-int/lit8", k22b, kNone)                            \
    V(0xe2, USHR_INT_LIT8, "ushr-int/lit8", k22b, kNone)                          \
    V(0xfa, INVOKE_POLYMORPHIC, "invoke-polymorphic", k45cc, kMethod)             \
    V(0xfb, INVOKE_POLYMORPHIC_RANGE, "invoke-polymorphic/range", k4rcc, kMethod) \
    V(0xfc, INVOKE_CUSTOM, "invoke-custom", k35c, kCallSite)                      \
    V(0xfd, INVOKE_CUSTOM_RANGE, "invoke-custom/range", k3rc, kCallSite)          \
    V(0xfe, CONST_METHOD_HANDLE, "const-method-handle", k21c, kMethodHandle)      \
    V(0xff, CONST_METHOD_TYPE, "const-method-type", k21c, kProto)

enum Opcode : uint8_t {
#define DEXE_OPCODE_ENUM(value, name, mnemonic, format, index) name = value,
    DEXE_DEX_OPCODES(DEXE_OPCODE_ENUM)
#undef DEXE_OPCODE_ENUM
};

struct OpcodeInfo {
    const char* name;  // nullptr for unused opcode values
    Format format;
    IndexKind index_kind;
};

const OpcodeInfo& opcode_info(uint8_t opcode);

// Identifiers of the data payloads that may appear inline in a method's code (after a NOP opcode byte).
constexpr uint16_t kPackedSwitchPayload = 0x0100;
constexpr uint16_t kSparseSwitchPayload = 0x0200;
constexpr uint16_t kFillArrayDataPayload = 0x0300;

// A decoded instruction. Field meaning depends on the format; names follow the spec (vA, vB, vC).
struct Instruction {
    uint8_t opcode = 0;
    uint32_t size = 1;      // length in 16-bit code units
    uint32_t a = 0;         // first register (or argument count for 35c/3rc)
    uint32_t b = 0;         // second register
    uint32_t c = 0;         // third register (or first register of a /range call)
    int64_t literal = 0;    // literal value or branch offset in code units
    uint32_t index = 0;     // constant-pool index
    uint32_t proto = 0;     // proto index for invoke-polymorphic
    uint16_t args[5] = {};  // argument registers for 35c/45cc
    bool is_payload = false;  // true if this is an inline data payload rather than an instruction
};

// Decodes the instruction at code[pc]. Throws FormatError if it runs past the end of code.
Instruction decode(std::span<const uint16_t> code, uint32_t pc);

}  // namespace dexe::dex
