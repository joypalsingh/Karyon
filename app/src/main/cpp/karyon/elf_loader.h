#pragma once

#include <string>
#include <vector>

namespace karyon::elf {

struct ElfExecRequest {
    std::string rootfs_dir;
    std::string guest_binary;
    std::vector<std::string> argv;
    std::vector<std::string> envp;
};

/**
 * Resolves a guest path cleanly inside [rootfs_dir], safely following container-relative
 * or container-root (`/system/...`, `/apex/...`) symlinks without escaping [rootfs_dir].
 */
std::string resolve_in_rootfs(const std::string& rootfs_dir, const std::string& guest_path);

/**
 * Loads an ELF64 ET_DYN/ET_EXEC binary (and its PT_INTERP dynamic linker if present)
 * into anonymous RW->RX memory mappings cleanly under Android 10+ W^X (`execmem`),
 * sets up the guest stack + auxiliary vector, and transfers control (noreturn on success).
 * Returns an error string if mapping or validation fails.
 */
std::string load_and_exec_elf64(const ElfExecRequest& req);

}  // namespace karyon::elf
