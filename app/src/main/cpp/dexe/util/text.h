#pragma once

#include <string>
#include <string_view>

namespace dexe {

// Decodes Modified UTF-8 (the encoding used for strings in dex files) to UTF-16.
std::u16string mutf8_to_utf16(std::string_view mutf8);

// Encodes UTF-16 as Modified UTF-8, as JNI's GetStringUTFChars does: U+0000 as two bytes and each surrogate as
// its own three-byte sequence.
std::string utf16_to_mutf8(std::u16string_view utf16);

// Encodes UTF-16 as standard UTF-8. Unpaired surrogates become U+FFFD.
std::string utf16_to_utf8(std::u16string_view utf16);

}  // namespace dexe
