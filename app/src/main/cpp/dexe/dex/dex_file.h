#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dexe::dex {

constexpr uint32_t kNoIndex = 0xffffffff;

// Access flags (shared by classes, fields and methods; some bits mean different things per kind).
enum AccessFlags : uint32_t {
    kAccPublic = 0x1,
    kAccPrivate = 0x2,
    kAccProtected = 0x4,
    kAccStatic = 0x8,
    kAccFinal = 0x10,
    kAccSynchronized = 0x20,
    kAccVolatile = 0x40,
    kAccBridge = 0x40,
    kAccTransient = 0x80,
    kAccVarargs = 0x80,
    kAccNative = 0x100,
    kAccInterface = 0x200,
    kAccAbstract = 0x400,
    kAccStrict = 0x800,
    kAccSynthetic = 0x1000,
    kAccAnnotation = 0x2000,
    kAccEnum = 0x4000,
    kAccConstructor = 0x10000,
    kAccDeclaredSynchronized = 0x20000,
};

struct ProtoId {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t parameters_off;
};

struct FieldId {
    uint16_t class_idx;
    uint16_t type_idx;
    uint32_t name_idx;
};

struct MethodId {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
};

struct ClassDef {
    uint32_t class_idx;
    uint32_t access_flags;
    uint32_t superclass_idx;  // kNoIndex for java.lang.Object
    uint32_t interfaces_off;
    uint32_t source_file_idx;
    uint32_t annotations_off;
    uint32_t class_data_off;
    uint32_t static_values_off;
};

struct EncodedField {
    uint32_t field_idx;
    uint32_t access_flags;
};

struct EncodedMethod {
    uint32_t method_idx;
    uint32_t access_flags;
    uint32_t code_off;  // 0 for abstract and native methods
};

struct ClassData {
    std::vector<EncodedField> static_fields;
    std::vector<EncodedField> instance_fields;
    std::vector<EncodedMethod> direct_methods;   // static, private and constructors
    std::vector<EncodedMethod> virtual_methods;
};

struct CatchClause {
    uint32_t type_idx;  // kNoIndex for a catch-all clause
    uint32_t address;
};

struct TryBlock {
    uint32_t start_addr;  // inclusive, in code units
    uint32_t end_addr;    // exclusive
    std::vector<CatchClause> handlers;  // in order; a catch-all, if any, is last
};

struct CodeItem {
    uint16_t registers_size;
    uint16_t ins_size;
    uint16_t outs_size;
    uint32_t debug_info_off;
    std::vector<uint16_t> insns;
    std::vector<TryBlock> tries;
};

// A constant from a static_values array or annotation.
struct EncodedValue {
    enum class Type : uint8_t {
        kByte = 0x00, kShort = 0x02, kChar = 0x03, kInt = 0x04, kLong = 0x06, kFloat = 0x10, kDouble = 0x11,
        kMethodType = 0x15, kMethodHandle = 0x16, kString = 0x17, kType = 0x18, kField = 0x19, kMethod = 0x1a,
        kEnum = 0x1b, kArray = 0x1c, kAnnotation = 0x1d, kNull = 0x1e, kBoolean = 0x1f,
    };
    Type type = Type::kNull;
    int64_t bits = 0;  // integer value, raw float/double bits, boolean, or pool index
    std::vector<EncodedValue> array;
    std::shared_ptr<const struct Annotation> annotation;  // kAnnotation: a nested annotation

    float as_float() const;
    double as_double() const;
};

// An annotation of a class, field, method or parameter (an annotation_item; a nested annotation has visibility 0).
struct Annotation {
    uint8_t visibility;  // 0 build, 1 runtime, 2 system
    uint32_t type_idx;
    std::vector<std::pair<uint32_t, EncodedValue>> elements;  // (name string index, value)

    const EncodedValue* find(const class DexFile& dex, std::string_view name) const;
};

enum class MethodHandleType : uint16_t {
    kStaticPut = 0x00,
    kStaticGet = 0x01,
    kInstancePut = 0x02,
    kInstanceGet = 0x03,
    kInvokeStatic = 0x04,
    kInvokeInstance = 0x05,
    kInvokeConstructor = 0x06,
    kInvokeDirect = 0x07,
    kInvokeInterface = 0x08,
};

struct MethodHandleItem {
    MethodHandleType type = MethodHandleType::kInvokeStatic;
    uint16_t field_or_method_id = 0;
};

// A parsed .dex file. All accessors validate indices and throw FormatError if they are out of range.
class DexFile {
public:
    static std::unique_ptr<DexFile> parse(std::vector<uint8_t> data, std::string location);

    const std::string& location() const { return location_; }
    std::string_view version() const;  // e.g. "035"
    bool checksum_ok() const;
    std::span<const uint8_t> bytes() const { return data_; }

    size_t num_strings() const { return string_offs_.size(); }
    size_t num_types() const { return type_descs_.size(); }
    size_t num_protos() const { return protos_.size(); }
    size_t num_fields() const { return fields_.size(); }
    size_t num_methods() const { return methods_.size(); }

    std::string_view string_mutf8(uint32_t string_idx) const;  // raw Modified UTF-8 bytes
    std::u16string string_utf16(uint32_t string_idx) const;
    std::string string_utf8(uint32_t string_idx) const;

    std::string_view type_descriptor(uint32_t type_idx) const;  // e.g. "Ljava/lang/String;"
    const ProtoId& proto(uint32_t proto_idx) const;
    std::vector<uint16_t> proto_parameters(uint32_t proto_idx) const;
    std::string_view proto_shorty(uint32_t proto_idx) const;
    const FieldId& field(uint32_t field_idx) const;
    const MethodId& method(uint32_t method_idx) const;

    size_t num_method_handles() const { return method_handles_.size(); }
    const struct MethodHandleItem& method_handle(uint32_t idx) const;
    size_t num_call_sites() const { return call_site_offs_.size(); }
    std::vector<EncodedValue> call_site(uint32_t idx) const;

    const std::vector<ClassDef>& class_defs() const { return class_defs_; }
    ClassData class_data(const ClassDef& def) const;
    std::vector<uint16_t> interfaces(const ClassDef& def) const;
    std::vector<EncodedValue> static_values(const ClassDef& def) const;
    std::vector<Annotation> class_annotations(const ClassDef& def) const;
    // The annotation of the given type on a class, if present.
    std::optional<Annotation> class_annotation(const ClassDef& def, std::string_view type_descriptor) const;
    // The annotations of a method of a class, and the one of the given type, if present.
    std::vector<Annotation> method_annotations(const ClassDef& def, uint32_t method_idx) const;
    std::optional<Annotation> method_annotation(const ClassDef& def, uint32_t method_idx,
                                                std::string_view type_descriptor) const;
    std::vector<Annotation> field_annotations(const ClassDef& def, uint32_t field_idx) const;
    // The annotations of each parameter of a method (empty when it has none).
    std::vector<std::vector<Annotation>> parameter_annotations(const ClassDef& def, uint32_t method_idx) const;
    std::optional<CodeItem> code_item(uint32_t code_off) const;
    // Source line of the instruction at `pc`, from a code item's debug info; -1 if unknown.
    int32_t line_for_pc(uint32_t debug_info_off, uint32_t pc) const;

    // Human-readable names in smali style.
    std::string method_signature(uint32_t proto_idx) const;  // "(ILjava/lang/String;)V"
    std::string pretty_method(uint32_t method_idx) const;     // "Lpkg/Cls;->name(I)V"
    std::string pretty_field(uint32_t field_idx) const;       // "Lpkg/Cls;->name:I"

private:
    DexFile() = default;
    std::vector<uint16_t> type_list(uint32_t offset) const;
    std::vector<Annotation> annotation_set(uint32_t set_off) const;

    std::vector<uint8_t> data_;
    std::string location_;
    std::vector<uint32_t> string_offs_;
    std::vector<uint32_t> type_descs_;
    std::vector<ProtoId> protos_;
    std::vector<FieldId> fields_;
    std::vector<MethodId> methods_;
    std::vector<struct MethodHandleItem> method_handles_;
    std::vector<uint32_t> call_site_offs_;
    std::vector<ClassDef> class_defs_;
};

// Converts a type descriptor to Java source form: "Ljava/lang/String;" -> "java.lang.String", "[I" -> "int[]".
std::string descriptor_to_java_name(std::string_view descriptor);

}  // namespace dexe::dex
