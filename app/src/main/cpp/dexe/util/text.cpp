#include "util/text.h"

namespace dexe {

std::u16string mutf8_to_utf16(std::string_view mutf8) {
    std::u16string out;
    out.reserve(mutf8.size());
    size_t i = 0, n = mutf8.size();
    auto cont = [&](size_t k) -> uint32_t { return k < n ? uint8_t(mutf8[k]) & 0x3f : 0; };
    while (i < n) {
        uint8_t c = uint8_t(mutf8[i]);
        if (c < 0x80) {
            out.push_back(c);
            i += 1;
        } else if ((c & 0xe0) == 0xc0) {
            out.push_back(char16_t(((c & 0x1f) << 6) | cont(i + 1)));
            i += 2;
        } else if ((c & 0xf0) == 0xe0) {
            out.push_back(char16_t(((c & 0x0f) << 12) | (cont(i + 1) << 6) | cont(i + 2)));
            i += 3;
        } else {
            // Not valid MUTF-8, but tolerate 4-byte UTF-8 sequences by producing a surrogate pair.
            uint32_t cp = ((c & 0x07) << 18) | (cont(i + 1) << 12) | (cont(i + 2) << 6) | cont(i + 3);
            cp -= 0x10000;
            out.push_back(char16_t(0xd800 + (cp >> 10)));
            out.push_back(char16_t(0xdc00 + (cp & 0x3ff)));
            i += 4;
        }
    }
    return out;
}

std::string utf16_to_mutf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size());
    for (char16_t c : utf16) {
        if (c != 0 && c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string utf16_to_utf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size());
    for (size_t i = 0; i < utf16.size(); ++i) {
        uint32_t cp = utf16[i];
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < utf16.size() && utf16[i + 1] >= 0xdc00 && utf16[i + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (utf16[i + 1] - 0xdc00);
            ++i;
        } else if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = 0xfffd;
        }
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xc0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3f)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xe0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(char(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(char(0xf0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(char(0x80 | (cp & 0x3f)));
        }
    }
    return out;
}

}  // namespace dexe
