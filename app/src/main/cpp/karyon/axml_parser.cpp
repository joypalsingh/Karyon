#include "axml_parser.h"

#include <algorithm>
#include <cstring>

namespace karyon::apk {

namespace {

constexpr uint16_t kResStringPoolType = 0x0001;
constexpr uint16_t kResXmlType = 0x0003;
constexpr uint16_t kResXmlStartElement = 0x0102;
constexpr uint16_t kResXmlEndElement = 0x0103;
constexpr uint32_t kUtf8Flag = 1u << 8;

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string decode_utf8_len(const uint8_t*& p, const uint8_t* end) {
    auto read_len = [&]() -> uint32_t {
        if (p >= end) return 0;
        uint32_t v = *p++;
        if (v & 0x80) {
            if (p >= end) return 0;
            v = ((v & 0x7f) << 8) | *p++;
        }
        return v;
    };
    (void)read_len();
    const uint32_t byte_len = read_len();
    if (p + byte_len > end) return {};
    std::string out(reinterpret_cast<const char*>(p), byte_len);
    p += byte_len;
    return out;
}

std::string decode_utf16_str(const uint8_t* p, const uint8_t* end) {
    if (p + 2 > end) return {};
    uint32_t len = read_u16(p);
    p += 2;
    if (len & 0x8000) {
        if (p + 2 > end) return {};
        len = ((len & 0x7fff) << 16) | read_u16(p);
        p += 2;
    }
    std::string out;
    out.reserve(len);
    for (uint32_t i = 0; i < len && p + 2 <= end; ++i) {
        const uint16_t ch = read_u16(p);
        p += 2;
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

std::vector<std::string> parse_string_pool(std::span<const uint8_t> chunk) {
    std::vector<std::string> strings;
    if (chunk.size() < 28) return strings;
    const uint16_t header_size = read_u16(chunk.data() + 2);
    const uint32_t string_count = read_u32(chunk.data() + 8);
    const uint32_t flags = read_u32(chunk.data() + 16);
    const uint32_t strings_start = read_u32(chunk.data() + 20);
    const bool is_utf8 = (flags & kUtf8Flag) != 0;

    if (header_size + string_count * 4 > chunk.size() || strings_start >= chunk.size()) {
        return strings;
    }

    strings.reserve(string_count);
    const uint8_t* offsets_base = chunk.data() + header_size;
    const uint8_t* pool_base = chunk.data() + strings_start;
    const uint8_t* pool_end = chunk.data() + chunk.size();

    for (uint32_t i = 0; i < string_count; ++i) {
        const uint32_t rel = read_u32(offsets_base + i * 4);
        const uint8_t* str_ptr = pool_base + rel;
        if (str_ptr >= pool_end) {
            strings.emplace_back();
            continue;
        }
        if (is_utf8) {
            strings.push_back(decode_utf8_len(str_ptr, pool_end));
        } else {
            strings.push_back(decode_utf16_str(str_ptr, pool_end));
        }
    }
    return strings;
}

}  // namespace

ParsedManifest parse_binary_manifest(std::span<const uint8_t> data) {
    ParsedManifest res;
    if (data.size() < 8 || read_u16(data.data()) != kResXmlType) {
        res.error = "Not a valid binary AXML file";
        return res;
    }

    const uint32_t total_size = std::min<uint32_t>(read_u32(data.data() + 4), static_cast<uint32_t>(data.size()));
    size_t pos = read_u16(data.data() + 2);
    std::vector<std::string> pool;

    std::string current_activity;
    bool in_activity = false;
    bool activity_has_main = false;

    auto str_at = [&](uint32_t idx) -> std::string {
        return (idx < pool.size()) ? pool[idx] : std::string{};
    };

    while (pos + 8 <= total_size) {
        const uint16_t type = read_u16(data.data() + pos);
        const uint16_t header_size = read_u16(data.data() + pos + 2);
        const uint32_t chunk_size = read_u32(data.data() + pos + 4);
        if (chunk_size < 8 || pos + chunk_size > total_size) break;

        if (type == kResStringPoolType && pool.empty()) {
            pool = parse_string_pool(data.subspan(pos, chunk_size));
        } else if (type == kResXmlStartElement && chunk_size >= 36 && header_size >= 16) {
            const uint8_t* elem = data.data() + pos + header_size;
            const uint32_t name_idx = read_u32(elem + 4);
            const uint16_t attr_start = read_u16(elem + 8);
            const uint16_t attr_size = read_u16(elem + 10);
            const uint16_t attr_count = read_u16(elem + 12);
            const std::string tag = str_at(name_idx);

            const uint8_t* attrs_base = elem + attr_start;
            auto read_attr = [&](uint16_t idx, std::string& attr_name, uint32_t& val_str_idx, uint32_t& raw_type, uint32_t& raw_data) {
                const uint8_t* a = attrs_base + idx * attr_size;
                if (a + 20 > data.data() + pos + chunk_size) return false;
                attr_name = str_at(read_u32(a + 4));
                val_str_idx = read_u32(a + 8);
                raw_type = a[15];
                raw_data = read_u32(a + 16);
                return true;
            };

            if (tag == "manifest") {
                for (uint16_t i = 0; i < attr_count; ++i) {
                    std::string aname;
                    uint32_t vs = 0, rt = 0, rd = 0;
                    if (!read_attr(i, aname, vs, rt, rd)) break;
                    if (aname == "package") res.package_name = str_at(vs);
                    else if (aname == "versionName") res.version_name = str_at(vs);
                }
            } else if (tag == "uses-sdk") {
                for (uint16_t i = 0; i < attr_count; ++i) {
                    std::string aname;
                    uint32_t vs = 0, rt = 0, rd = 0;
                    if (!read_attr(i, aname, vs, rt, rd)) break;
                    if (aname == "minSdkVersion") res.min_sdk = static_cast<int32_t>(rd);
                    else if (aname == "targetSdkVersion") res.target_sdk = static_cast<int32_t>(rd);
                }
            } else if (tag == "uses-permission") {
                for (uint16_t i = 0; i < attr_count; ++i) {
                    std::string aname;
                    uint32_t vs = 0, rt = 0, rd = 0;
                    if (!read_attr(i, aname, vs, rt, rd)) break;
                    if (aname == "name") res.permissions.push_back(str_at(vs));
                }
            } else if (tag == "activity" || tag == "activity-alias") {
                in_activity = true;
                activity_has_main = false;
                current_activity.clear();
                for (uint16_t i = 0; i < attr_count; ++i) {
                    std::string aname;
                    uint32_t vs = 0, rt = 0, rd = 0;
                    if (!read_attr(i, aname, vs, rt, rd)) break;
                    if (aname == "name") current_activity = str_at(vs);
                }
                if (res.main_activity.empty() && !current_activity.empty()) {
                    res.main_activity = current_activity;
                }
            } else if (tag == "action" && in_activity) {
                for (uint16_t i = 0; i < attr_count; ++i) {
                    std::string aname;
                    uint32_t vs = 0, rt = 0, rd = 0;
                    if (!read_attr(i, aname, vs, rt, rd)) break;
                    if (aname == "name" && str_at(vs) == "android.intent.action.MAIN") {
                        activity_has_main = true;
                        if (!current_activity.empty()) res.main_activity = current_activity;
                    }
                }
            }
        } else if (type == kResXmlEndElement && chunk_size >= 24 && header_size >= 16) {
            const uint8_t* elem = data.data() + pos + header_size;
            const std::string tag = str_at(read_u32(elem + 4));
            if (tag == "activity" || tag == "activity-alias") {
                in_activity = false;
                (void)activity_has_main;
            }
        }

        pos += chunk_size;
    }

    res.valid = !res.package_name.empty();
    if (!res.valid && res.error.empty()) {
        res.error = "Missing <manifest package=\"...\"> attribute";
    }
    return res;
}

}  // namespace karyon::apk
