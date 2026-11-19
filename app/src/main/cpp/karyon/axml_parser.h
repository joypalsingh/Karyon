#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace karyon::apk {

struct ParsedManifest {
    bool valid = false;
    std::string error;
    std::string package_name;
    std::string version_name;
    int32_t min_sdk = 0;
    int32_t target_sdk = 0;
    std::string main_activity;
    std::vector<std::string> permissions;
};

/**
 * Parses a compiled binary AndroidManifest.xml (AXML) buffer.
 */
ParsedManifest parse_binary_manifest(std::span<const uint8_t> data);

}  // namespace karyon::apk
