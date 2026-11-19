#pragma once

// Android file names on Windows. Linux allows any character but '/' and NUL in a name; Windows refuses
// < > : " \ | ? * and the control characters. Those are stored as private-use characters U+F000 + c, as WSL stores
// them, and turned back when names are read (Firebase names its settings files "frc_1:...:android:..._settings.xml").

#include <string>
#include <string_view>

namespace dexe {

inline bool windows_forbidden(char16_t c) {
    return c < 0x20 || c == u'<' || c == u'>' || c == u':' || c == u'"' || c == u'\\' || c == u'|' || c == u'?' ||
           c == u'*';
}

// One path component as Windows stores it.
inline std::u16string host_file_name(std::u16string_view name) {
    std::u16string out(name);
    for (char16_t& c : out) {
        if (windows_forbidden(c)) c = char16_t(0xf000 + c);
    }
    return out;
}

// One path component as Android sees it.
inline std::u16string android_file_name(std::u16string_view name) {
    std::u16string out(name);
    for (char16_t& c : out) {
        if (c >= 0xf000 && c < 0xf080 && windows_forbidden(char16_t(c - 0xf000))) c = char16_t(c - 0xf000);
    }
    return out;
}

inline std::wstring android_file_name(std::wstring_view name) {
    std::u16string u = android_file_name(std::u16string_view(reinterpret_cast<const char16_t*>(name.data()), name.size()));
    return std::wstring(u.begin(), u.end());
}

}  // namespace dexe
