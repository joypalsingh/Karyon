#include "dex/dex_file.h"

#include <bit>
#include <cstring>

#include "util/bytes.h"
#include "util/checksum.h"
#include "util/text.h"

namespace dexe::dex {

namespace {

constexpr size_t kHeaderSize = 0x70;
constexpr uint32_t kEndianConstant = 0x12345678;

template <typename T>
const T& checked_at(const std::vector<T>& v, uint32_t idx, const char* what) {
    if (idx >= v.size()) throw FormatError(std::string("dex: ") + what + " index out of range: " + std::to_string(idx));
    return v[idx];
}

EncodedValue read_encoded_value(ByteReader& r);

// Reads an encoded_array (size followed by values).
std::vector<EncodedValue> read_encoded_array(ByteReader& r) {
    uint32_t size = r.uleb128();
    std::vector<EncodedValue> values;
    values.reserve(size);
    for (uint32_t i = 0; i < size; ++i) values.push_back(read_encoded_value(r));
    return values;
}

// Reads an encoded_annotation (type, then name/value elements).
Annotation read_encoded_annotation(ByteReader& r) {
    Annotation a;
    a.visibility = 0;
    a.type_idx = r.uleb128();
    uint32_t size = r.uleb128();
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t name_idx = r.uleb128();
        a.elements.emplace_back(name_idx, read_encoded_value(r));
    }
    return a;
}

EncodedValue read_encoded_value(ByteReader& r) {
    uint8_t header = r.u1();
    uint32_t arg = header >> 5;
    EncodedValue v;
    v.type = EncodedValue::Type(header & 0x1f);
    uint32_t nbytes = arg + 1;

    auto read_unsigned = [&] {
        uint64_t value = 0;
        for (uint32_t i = 0; i < nbytes; ++i) value |= uint64_t(r.u1()) << (8 * i);
        return value;
    };
    auto read_signed = [&] {
        uint64_t value = read_unsigned();
        unsigned shift = 64 - 8 * nbytes;
        return int64_t(value << shift) >> shift;
    };

    using T = EncodedValue::Type;
    switch (v.type) {
        case T::kByte:
        case T::kShort:
        case T::kInt:
        case T::kLong:
            v.bits = read_signed();
            break;
        case T::kChar:
            v.bits = int64_t(read_unsigned());
            break;
        case T::kFloat:
            // Stored as the high-order bytes; the missing low-order bytes are zero.
            if (nbytes > 4) throw FormatError("dex: encoded float too wide");
            v.bits = int64_t(read_unsigned() << (8 * (4 - nbytes)));
            break;
        case T::kDouble:
            v.bits = int64_t(read_unsigned() << (8 * (8 - nbytes)));
            break;
        case T::kMethodType: case T::kMethodHandle: case T::kString: case T::kType:
        case T::kField: case T::kMethod: case T::kEnum:
            v.bits = int64_t(read_unsigned());
            break;
        case T::kArray:
            v.array = read_encoded_array(r);
            break;
        case T::kAnnotation:
            v.annotation = std::make_shared<const Annotation>(read_encoded_annotation(r));
            break;
        case T::kNull:
            break;
        case T::kBoolean:
            v.bits = arg;
            break;
        default:
            throw FormatError("dex: invalid encoded value type " + std::to_string(header & 0x1f));
    }
    return v;
}

}  // namespace

float EncodedValue::as_float() const { return std::bit_cast<float>(uint32_t(bits)); }
double EncodedValue::as_double() const { return std::bit_cast<double>(uint64_t(bits)); }

std::unique_ptr<DexFile> DexFile::parse(std::vector<uint8_t> data, std::string location) {
    std::unique_ptr<DexFile> dex(new DexFile());
    dex->data_ = std::move(data);
    dex->location_ = std::move(location);
    const auto& d = dex->data_;

    if (d.size() < kHeaderSize || std::memcmp(d.data(), "dex\n", 4) != 0 || d[7] != 0)
        throw FormatError("dex: bad magic in " + dex->location_);

    ByteReader r(d, 32);
    uint32_t file_size = r.u4();
    uint32_t header_size = r.u4();
    uint32_t endian_tag = r.u4();
    if (endian_tag != kEndianConstant) throw FormatError("dex: unsupported endianness");
    if (file_size > d.size() || header_size < kHeaderSize) throw FormatError("dex: bad header sizes");

    r.seek(56);
    uint32_t string_ids_size = r.u4(), string_ids_off = r.u4();
    uint32_t type_ids_size = r.u4(), type_ids_off = r.u4();
    uint32_t proto_ids_size = r.u4(), proto_ids_off = r.u4();
    uint32_t field_ids_size = r.u4(), field_ids_off = r.u4();
    uint32_t method_ids_size = r.u4(), method_ids_off = r.u4();
    uint32_t class_defs_size = r.u4(), class_defs_off = r.u4();

    r.seek(string_ids_off);
    dex->string_offs_.resize(string_ids_size);
    for (auto& off : dex->string_offs_) off = r.u4();

    r.seek(type_ids_off);
    dex->type_descs_.resize(type_ids_size);
    for (auto& idx : dex->type_descs_) idx = r.u4();

    r.seek(proto_ids_off);
    dex->protos_.resize(proto_ids_size);
    for (auto& p : dex->protos_) p = {r.u4(), r.u4(), r.u4()};

    r.seek(field_ids_off);
    dex->fields_.resize(field_ids_size);
    for (auto& f : dex->fields_) {
        f.class_idx = r.u2();
        f.type_idx = r.u2();
        f.name_idx = r.u4();
    }

    r.seek(method_ids_off);
    dex->methods_.resize(method_ids_size);
    for (auto& m : dex->methods_) {
        m.class_idx = r.u2();
        m.proto_idx = r.u2();
        m.name_idx = r.u4();
    }

    r.seek(class_defs_off);
    dex->class_defs_.resize(class_defs_size);
    for (auto& c : dex->class_defs_) c = {r.u4(), r.u4(), r.u4(), r.u4(), r.u4(), r.u4(), r.u4(), r.u4()};

    uint32_t map_off = ByteReader(d, 52).u4();
    if (map_off != 0 && map_off + 4 <= d.size()) {
        ByteReader mr(d, map_off);
        uint32_t map_size = mr.u4();
        for (uint32_t i = 0; i < map_size && mr.pos() + 12 <= d.size(); ++i) {
            uint16_t type = mr.u2();
            mr.u2();  // unused
            uint32_t size = mr.u4();
            uint32_t offset = mr.u4();
            if (type == 0x0007 && offset + size * 4 <= d.size()) {  // TYPE_CALL_SITE_ID_ITEM
                dex->call_site_offs_.resize(size);
                ByteReader csr(d, offset);
                for (auto& cs : dex->call_site_offs_) cs = csr.u4();
            } else if (type == 0x0008 && offset + size * 8 <= d.size()) {  // TYPE_METHOD_HANDLE_ITEM
                dex->method_handles_.resize(size);
                ByteReader mhr(d, offset);
                for (auto& mh : dex->method_handles_) {
                    mh.type = MethodHandleType(mhr.u2());
                    mhr.u2();  // unused
                    mh.field_or_method_id = mhr.u2();
                    mhr.u2();  // unused
                }
            }
        }
    }

    return dex;
}

const MethodHandleItem& DexFile::method_handle(uint32_t idx) const {
    return checked_at(method_handles_, idx, "method handle");
}

std::vector<EncodedValue> DexFile::call_site(uint32_t idx) const {
    uint32_t off = checked_at(call_site_offs_, idx, "call site");
    ByteReader r(data_, off);
    return read_encoded_array(r);
}

std::string_view DexFile::version() const { return {reinterpret_cast<const char*>(data_.data()) + 4, 3}; }

bool DexFile::checksum_ok() const {
    uint32_t stored = ByteReader(data_, 8).u4();
    return adler32(std::span(data_).subspan(12)) == stored;
}

std::string_view DexFile::string_mutf8(uint32_t string_idx) const {
    ByteReader r(data_, checked_at(string_offs_, string_idx, "string"));
    r.uleb128();  // length in UTF-16 code units
    size_t start = r.pos();
    while (r.u1() != 0) {
    }
    return {reinterpret_cast<const char*>(data_.data()) + start, r.pos() - start - 1};
}

std::u16string DexFile::string_utf16(uint32_t string_idx) const { return mutf8_to_utf16(string_mutf8(string_idx)); }

std::string DexFile::string_utf8(uint32_t string_idx) const { return utf16_to_utf8(string_utf16(string_idx)); }

std::string_view DexFile::type_descriptor(uint32_t type_idx) const {
    return string_mutf8(checked_at(type_descs_, type_idx, "type"));
}

const ProtoId& DexFile::proto(uint32_t proto_idx) const { return checked_at(protos_, proto_idx, "proto"); }

std::vector<uint16_t> DexFile::proto_parameters(uint32_t proto_idx) const {
    return type_list(proto(proto_idx).parameters_off);
}

std::string_view DexFile::proto_shorty(uint32_t proto_idx) const { return string_mutf8(proto(proto_idx).shorty_idx); }

const FieldId& DexFile::field(uint32_t field_idx) const { return checked_at(fields_, field_idx, "field"); }

const MethodId& DexFile::method(uint32_t method_idx) const { return checked_at(methods_, method_idx, "method"); }

std::vector<uint16_t> DexFile::type_list(uint32_t offset) const {
    if (offset == 0) return {};
    ByteReader r(data_, offset);
    uint32_t size = r.u4();
    std::vector<uint16_t> types(size);
    for (auto& t : types) t = r.u2();
    return types;
}

ClassData DexFile::class_data(const ClassDef& def) const {
    ClassData cd;
    if (def.class_data_off == 0) return cd;
    ByteReader r(data_, def.class_data_off);
    uint32_t sizes[4];
    for (auto& s : sizes) s = r.uleb128();

    auto read_fields = [&](std::vector<EncodedField>& out, uint32_t count) {
        uint32_t idx = 0;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            idx += r.uleb128();
            out.push_back({idx, r.uleb128()});
        }
    };
    auto read_methods = [&](std::vector<EncodedMethod>& out, uint32_t count) {
        uint32_t idx = 0;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            idx += r.uleb128();
            uint32_t flags = r.uleb128();
            out.push_back({idx, flags, r.uleb128()});
        }
    };
    read_fields(cd.static_fields, sizes[0]);
    read_fields(cd.instance_fields, sizes[1]);
    read_methods(cd.direct_methods, sizes[2]);
    read_methods(cd.virtual_methods, sizes[3]);
    return cd;
}

std::vector<uint16_t> DexFile::interfaces(const ClassDef& def) const { return type_list(def.interfaces_off); }

std::vector<EncodedValue> DexFile::static_values(const ClassDef& def) const {
    if (def.static_values_off == 0) return {};
    ByteReader r(data_, def.static_values_off);
    return read_encoded_array(r);
}

const EncodedValue* Annotation::find(const DexFile& dex, std::string_view name) const {
    for (const auto& [name_idx, value] : elements)
        if (dex.string_mutf8(name_idx) == name) return &value;
    return nullptr;
}

// Reads an annotation_set_item.
std::vector<Annotation> DexFile::annotation_set(uint32_t set_off) const {
    std::vector<Annotation> result;
    if (set_off == 0) return result;
    ByteReader set(data_, set_off);
    uint32_t count = set.u4();
    for (uint32_t i = 0; i < count; ++i) {
        ByteReader item(data_, set.u4());
        Annotation a;
        a.visibility = item.u1();
        a.type_idx = item.uleb128();
        uint32_t size = item.uleb128();
        for (uint32_t k = 0; k < size; ++k) {
            uint32_t name_idx = item.uleb128();
            a.elements.emplace_back(name_idx, read_encoded_value(item));
        }
        result.push_back(std::move(a));
    }
    return result;
}

std::vector<Annotation> DexFile::class_annotations(const ClassDef& def) const {
    if (def.annotations_off == 0) return {};
    return annotation_set(ByteReader(data_, def.annotations_off).u4());  // annotations_directory_item.class_annotations_off
}

std::vector<Annotation> DexFile::method_annotations(const ClassDef& def, uint32_t method_idx) const {
    if (def.annotations_off == 0) return {};
    // annotations_directory_item: class annotations, then the counts of the field, method and parameter lists,
    // then the field list (8-byte entries) and the method list (method_idx, annotations_off).
    ByteReader dir(data_, def.annotations_off);
    dir.u4();
    uint32_t fields_size = dir.u4();
    uint32_t methods_size = dir.u4();
    dir.u4();
    dir.skip(size_t(fields_size) * 8);
    for (uint32_t i = 0; i < methods_size; ++i) {
        uint32_t idx = dir.u4();
        uint32_t off = dir.u4();
        if (idx == method_idx) return annotation_set(off);
    }
    return {};
}

std::vector<Annotation> DexFile::field_annotations(const ClassDef& def, uint32_t field_idx) const {
    if (def.annotations_off == 0) return {};
    ByteReader dir(data_, def.annotations_off);
    dir.u4();
    uint32_t fields_size = dir.u4();
    dir.u4();
    dir.u4();
    for (uint32_t i = 0; i < fields_size; ++i) {
        uint32_t idx = dir.u4();
        uint32_t off = dir.u4();
        if (idx == field_idx) return annotation_set(off);
    }
    return {};
}

std::vector<std::vector<Annotation>> DexFile::parameter_annotations(const ClassDef& def, uint32_t method_idx) const {
    if (def.annotations_off == 0) return {};
    ByteReader dir(data_, def.annotations_off);
    dir.u4();
    uint32_t fields_size = dir.u4();
    uint32_t methods_size = dir.u4();
    uint32_t parameters_size = dir.u4();
    dir.skip((size_t(fields_size) + methods_size) * 8);
    for (uint32_t i = 0; i < parameters_size; ++i) {
        uint32_t idx = dir.u4();
        uint32_t off = dir.u4();
        if (idx != method_idx) continue;
        // annotation_set_ref_list: one annotation set (or none) per parameter.
        std::vector<std::vector<Annotation>> result;
        ByteReader list(data_, off);
        uint32_t count = list.u4();
        for (uint32_t p = 0; p < count; ++p) result.push_back(annotation_set(list.u4()));
        return result;
    }
    return {};
}

std::optional<Annotation> DexFile::method_annotation(const ClassDef& def, uint32_t method_idx,
                                                     std::string_view type_descriptor) const {
    for (auto& a : method_annotations(def, method_idx))
        if (this->type_descriptor(a.type_idx) == type_descriptor) return std::move(a);
    return std::nullopt;
}

std::optional<Annotation> DexFile::class_annotation(const ClassDef& def, std::string_view type_descriptor) const {
    for (auto& a : class_annotations(def))
        if (this->type_descriptor(a.type_idx) == type_descriptor) return std::move(a);
    return std::nullopt;
}

std::optional<CodeItem> DexFile::code_item(uint32_t code_off) const {
    if (code_off == 0) return std::nullopt;
    ByteReader r(data_, code_off);
    CodeItem code;
    code.registers_size = r.u2();
    code.ins_size = r.u2();
    code.outs_size = r.u2();
    uint16_t tries_size = r.u2();
    code.debug_info_off = r.u4();
    uint32_t insns_size = r.u4();
    code.insns.resize(insns_size);
    for (auto& unit : code.insns) unit = r.u2();
    if (tries_size == 0) return code;

    if (insns_size & 1) r.skip(2);  // padding to keep try_items 4-byte aligned
    struct RawTry {
        uint32_t start;
        uint16_t count;
        uint16_t handler_off;
    };
    std::vector<RawTry> raw(tries_size);
    for (auto& t : raw) {
        t.start = r.u4();
        t.count = r.u2();
        t.handler_off = r.u2();
    }

    size_t handlers_base = r.pos();
    for (const auto& t : raw) {
        TryBlock block{t.start, t.start + t.count, {}};
        ByteReader h(data_, handlers_base + t.handler_off);
        int32_t size = h.sleb128();
        uint32_t typed = uint32_t(size < 0 ? -size : size);
        for (uint32_t i = 0; i < typed; ++i) {
            uint32_t type_idx = h.uleb128();
            block.handlers.push_back({type_idx, h.uleb128()});
        }
        if (size <= 0) block.handlers.push_back({kNoIndex, h.uleb128()});
        code.tries.push_back(std::move(block));
    }
    return code;
}

int32_t DexFile::line_for_pc(uint32_t debug_info_off, uint32_t pc) const {
    if (debug_info_off == 0) return -1;
    try {
        ByteReader r(data_, debug_info_off);
        int64_t line = r.uleb128();
        uint32_t params = r.uleb128();
        for (uint32_t i = 0; i < params; ++i) r.uleb128();  // parameter names (uleb128p1)
        uint32_t address = 0;
        int32_t best = -1;
        // Runs the debug-info state machine; every emitted position entry maps an address to a line.
        for (;;) {
            uint8_t op = r.u1();
            switch (op) {
                case 0x00: return best;                                // DBG_END_SEQUENCE
                case 0x01: address += r.uleb128(); break;              // DBG_ADVANCE_PC
                case 0x02: line += r.sleb128(); break;                 // DBG_ADVANCE_LINE
                case 0x03: r.uleb128(); r.uleb128(); r.uleb128(); break;  // DBG_START_LOCAL
                case 0x04: r.uleb128(); r.uleb128(); r.uleb128(); r.uleb128(); break;  // DBG_START_LOCAL_EXTENDED
                case 0x05: case 0x06: r.uleb128(); break;              // DBG_END_LOCAL, DBG_RESTART_LOCAL
                case 0x07: case 0x08: break;                           // prologue end, epilogue begin
                case 0x09: r.uleb128(); break;                         // DBG_SET_FILE
                default: {
                    int adjusted = op - 0x0a;
                    line += -4 + adjusted % 15;
                    address += uint32_t(adjusted / 15);
                    if (address > pc) return best;
                    best = int32_t(line);
                    break;
                }
            }
        }
    } catch (const FormatError&) {
        return -1;
    }
}

std::string DexFile::method_signature(uint32_t proto_idx) const {
    std::string s = "(";
    for (uint16_t t : proto_parameters(proto_idx)) s += type_descriptor(t);
    s += ")";
    s += type_descriptor(proto(proto_idx).return_type_idx);
    return s;
}

std::string DexFile::pretty_method(uint32_t method_idx) const {
    const MethodId& m = method(method_idx);
    std::string s(type_descriptor(m.class_idx));
    s += "->";
    s += string_utf8(m.name_idx);
    s += method_signature(m.proto_idx);
    return s;
}

std::string DexFile::pretty_field(uint32_t field_idx) const {
    const FieldId& f = field(field_idx);
    std::string s(type_descriptor(f.class_idx));
    s += "->";
    s += string_utf8(f.name_idx);
    s += ":";
    s += type_descriptor(f.type_idx);
    return s;
}

std::string descriptor_to_java_name(std::string_view descriptor) {
    size_t dims = 0;
    while (dims < descriptor.size() && descriptor[dims] == '[') ++dims;
    std::string_view base = descriptor.substr(dims);
    std::string name;
    if (base.size() >= 2 && base.front() == 'L' && base.back() == ';') {
        name.assign(base.substr(1, base.size() - 2));
        for (char& c : name)
            if (c == '/') c = '.';
    } else if (base.size() == 1) {
        switch (base[0]) {
            case 'Z': name = "boolean"; break;
            case 'B': name = "byte"; break;
            case 'S': name = "short"; break;
            case 'C': name = "char"; break;
            case 'I': name = "int"; break;
            case 'J': name = "long"; break;
            case 'F': name = "float"; break;
            case 'D': name = "double"; break;
            case 'V': name = "void"; break;
            default: name.assign(base); break;
        }
    } else {
        name.assign(base);
    }
    for (size_t i = 0; i < dims; ++i) name += "[]";
    return name;
}

}  // namespace dexe::dex
