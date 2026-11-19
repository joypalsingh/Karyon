#include "elf_loader.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace fs = std::filesystem;

namespace karyon::elf {

namespace {

constexpr uintptr_t kPageSize = 4096;

uintptr_t page_floor(uintptr_t v) {
    return v & ~(kPageSize - 1);
}

uintptr_t page_ceil(uintptr_t v) {
    return (v + kPageSize - 1) & ~(kPageSize - 1);
}

int prot_from_flags(uint32_t p_flags) {
    int p = 0;
    if (p_flags & PF_R) p |= PROT_READ;
    if (p_flags & PF_W) p |= PROT_WRITE;
    if (p_flags & PF_X) p |= PROT_EXEC;
    return p;
}

bool pread_exact(int fd, void* dst, size_t len, off_t off) {
    auto* p = static_cast<uint8_t*>(dst);
    size_t got = 0;
    while (got < len) {
        const ssize_t n = pread(fd, p + got, len - got, off + static_cast<off_t>(got));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

struct MappedImage {
    bool ok = false;
    std::string error;
    uintptr_t load_bias = 0;
    uintptr_t entry = 0;
    uintptr_t phdr_addr = 0;
    uint16_t phent = sizeof(Elf64_Phdr);
    uint16_t phnum = 0;
    std::string interp;
};

MappedImage map_elf64_file(const std::string& path) {
    MappedImage res;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        res.error = "open(" + path + ") failed errno=" + std::to_string(errno);
        return res;
    }

    Elf64_Ehdr ehdr{};
    if (!pread_exact(fd, &ehdr, sizeof(ehdr), 0)) {
        close(fd);
        res.error = "Failed to read ELF64 header: " + path;
        return res;
    }

    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        close(fd);
        res.error = "Not a valid 64-bit ELF: " + path;
        return res;
    }

#if defined(__aarch64__)
    constexpr uint16_t kHostMachine = EM_AARCH64;
#elif defined(__x86_64__)
    constexpr uint16_t kHostMachine = EM_X86_64;
#else
    constexpr uint16_t kHostMachine = EM_NONE;
#endif
    if (ehdr.e_machine != kHostMachine) {
        close(fd);
        res.error = "ELF arch mismatch (machine=" + std::to_string(ehdr.e_machine) + "): " + path;
        return res;
    }
    if (ehdr.e_phnum == 0 || ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        close(fd);
        res.error = "Unsupported program header layout: " + path;
        return res;
    }

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    if (!pread_exact(fd, phdrs.data(), phdrs.size() * sizeof(Elf64_Phdr), static_cast<off_t>(ehdr.e_phoff))) {
        close(fd);
        res.error = "Failed to read program headers: " + path;
        return res;
    }

    uintptr_t min_vaddr = std::numeric_limits<uintptr_t>::max();
    uintptr_t max_vaddr = 0;
    for (const auto& ph : phdrs) {
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        min_vaddr = std::min(min_vaddr, page_floor(ph.p_vaddr));
        max_vaddr = std::max(max_vaddr, page_ceil(ph.p_vaddr + ph.p_memsz));
    }
    if (min_vaddr >= max_vaddr) {
        close(fd);
        res.error = "No PT_LOAD segments in ELF: " + path;
        return res;
    }

    const size_t total_span = max_vaddr - min_vaddr;
    void* fixed_hint = (ehdr.e_type == ET_EXEC) ? reinterpret_cast<void*>(min_vaddr) : nullptr;
    const int map_flags = MAP_PRIVATE | MAP_ANONYMOUS | (ehdr.e_type == ET_EXEC ? MAP_FIXED_NOREPLACE : 0);
    void* base_map = mmap(fixed_hint, total_span, PROT_NONE, map_flags, -1, 0);
    if (base_map == MAP_FAILED) {
        close(fd);
        res.error = "mmap reservation failed errno=" + std::to_string(errno) + " span=" + std::to_string(total_span);
        return res;
    }

    const uintptr_t load_bias = reinterpret_cast<uintptr_t>(base_map) - min_vaddr;

    for (const auto& ph : phdrs) {
        if (ph.p_type == PT_INTERP && ph.p_filesz > 1 && ph.p_filesz < 512) {
            std::string interp_buf(static_cast<size_t>(ph.p_filesz), '\0');
            if (pread_exact(fd, interp_buf.data(), interp_buf.size(), static_cast<off_t>(ph.p_offset))) {
                while (!interp_buf.empty() && interp_buf.back() == '\0') interp_buf.pop_back();
                res.interp = std::move(interp_buf);
            }
            continue;
        }
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;

        const uintptr_t seg_start = page_floor(ph.p_vaddr);
        const uintptr_t seg_end = page_ceil(ph.p_vaddr + ph.p_memsz);
        const size_t seg_len = seg_end - seg_start;
        void* seg_ptr = reinterpret_cast<void*>(load_bias + seg_start);

        if (mprotect(seg_ptr, seg_len, PROT_READ | PROT_WRITE) != 0) {
            close(fd);
            res.error = "mprotect RW failed errno=" + std::to_string(errno);
            return res;
        }

        if (ph.p_filesz > 0) {
            if (!pread_exact(fd, reinterpret_cast<void*>(load_bias + ph.p_vaddr), static_cast<size_t>(ph.p_filesz), static_cast<off_t>(ph.p_offset))) {
                close(fd);
                res.error = "pread PT_LOAD payload failed";
                return res;
            }
        }

        const int final_prot = prot_from_flags(ph.p_flags);
        if (mprotect(seg_ptr, seg_len, final_prot) != 0) {
            close(fd);
            res.error = "mprotect final prot=" + std::to_string(final_prot) + " failed errno=" + std::to_string(errno);
            return res;
        }
    }

    close(fd);

    uintptr_t phdr_addr = 0;
    for (const auto& ph : phdrs) {
        if (ph.p_type == PT_PHDR) {
            phdr_addr = load_bias + ph.p_vaddr;
            break;
        }
        if (ph.p_type == PT_LOAD && ehdr.e_phoff >= ph.p_offset && ehdr.e_phoff < ph.p_offset + ph.p_filesz) {
            phdr_addr = load_bias + ph.p_vaddr + (ehdr.e_phoff - ph.p_offset);
        }
    }

    res.ok = true;
    res.load_bias = load_bias;
    res.entry = load_bias + ehdr.e_entry;
    res.phdr_addr = phdr_addr;
    res.phnum = ehdr.e_phnum;
    return res;
}

[[noreturn]] void jump_to_entry(uintptr_t sp, uintptr_t entry) {
#if defined(__aarch64__)
    __asm__ volatile(
        "mov sp, %0\n"
        "mov x0, #0\n"
        "br %1\n"
        :
        : "r"(sp), "r"(entry)
        : "x0", "memory");
#elif defined(__x86_64__)
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "xor %%rdx, %%rdx\n"
        "jmp *%1\n"
        :
        : "r"(sp), "r"(entry)
        : "rdx", "memory");
#endif
    __builtin_unreachable();
}

}  // namespace

std::string resolve_in_rootfs(const std::string& rootfs_dir, const std::string& guest_path) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(rootfs_dir, ec);
    if (ec) return {};

    auto normalize_under_root = [&](std::string rel) -> fs::path {
        while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
        return (root / rel).lexically_normal();
    };

    fs::path cur = normalize_under_root(guest_path);
    for (int hop = 0; hop < 8; ++hop) {
        const auto cur_str = cur.string();
        const auto root_str = root.string();
        if (cur_str.rfind(root_str, 0) != 0) return {};
        if (!fs::is_symlink(cur, ec)) {
            if (fs::exists(cur, ec)) return cur_str;
            return {};
        }
        const fs::path target = fs::read_symlink(cur, ec);
        if (ec) return {};
        if (target.is_absolute()) {
            cur = normalize_under_root(target.string());
        } else {
            cur = (cur.parent_path() / target).lexically_normal();
        }
    }
    return {};
}

std::string load_and_exec_elf64(const ElfExecRequest& req) {
    if (req.rootfs_dir.empty() || req.guest_binary.empty()) {
        return "rootfs_dir and guest_binary are required";
    }
    const std::string host_bin = resolve_in_rootfs(req.rootfs_dir, req.guest_binary);
    if (host_bin.empty()) {
        return "Guest binary not found inside rootfs: " + req.guest_binary;
    }

    const MappedImage target = map_elf64_file(host_bin);
    if (!target.ok) {
        return target.error;
    }

    uintptr_t at_base = 0;
    uintptr_t entry_pc = target.entry;

    if (!target.interp.empty()) {
        std::string interp_host = resolve_in_rootfs(req.rootfs_dir, target.interp);
        if (interp_host.empty()) {
            // Fall back to bootstrap linker64 inside prebuilt rootfs if main linker symlink is absent
            interp_host = resolve_in_rootfs(req.rootfs_dir, "/system/bin/bootstrap/linker64");
        }
        if (interp_host.empty()) {
            return "PT_INTERP not found inside rootfs: " + target.interp;
        }
        const MappedImage interp = map_elf64_file(interp_host);
        if (!interp.ok) {
            return "Failed to map PT_INTERP (" + interp_host + "): " + interp.error;
        }
        at_base = interp.load_bias;
        entry_pc = interp.entry;
    }

    constexpr size_t kStackSize = 2 * 1024 * 1024;
    void* stack_mem = mmap(nullptr, kStackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack_mem == MAP_FAILED) {
        return "Failed to allocate guest stack errno=" + std::to_string(errno);
    }

    uint8_t* sp = static_cast<uint8_t*>(stack_mem) + kStackSize;
    auto push_bytes = [&](const void* src, size_t len) -> uintptr_t {
        sp -= len;
        std::memcpy(sp, src, len);
        return reinterpret_cast<uintptr_t>(sp);
    };
    auto push_str = [&](const std::string& s) -> uintptr_t {
        return push_bytes(s.c_str(), s.size() + 1);
    };

    uint8_t rand_bytes[16]{};
    const int urfd = open("/dev/urandom", O_RDONLY);
    if (urfd >= 0) {
        (void)read(urfd, rand_bytes, sizeof(rand_bytes));
        close(urfd);
    }
    const uintptr_t at_random = push_bytes(rand_bytes, sizeof(rand_bytes));
    const uintptr_t at_execfn = push_str(req.guest_binary);

    std::vector<uintptr_t> arg_ptrs;
    std::vector<std::string> argv_copy = req.argv;
    if (argv_copy.empty()) argv_copy.push_back(req.guest_binary);
    arg_ptrs.reserve(argv_copy.size());
    for (const auto& a : argv_copy) arg_ptrs.push_back(push_str(a));

    std::vector<uintptr_t> env_ptrs;
    env_ptrs.reserve(req.envp.size());
    for (const auto& e : req.envp) env_ptrs.push_back(push_str(e));

    // Align top before placing pointer/auxv words cleanly onto a 16-byte boundary
    sp = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(sp) & ~static_cast<uintptr_t>(15));

    std::vector<Elf64_auxv_t> auxv = {
        {AT_PHDR, {target.phdr_addr}},
        {AT_PHENT, {target.phent}},
        {AT_PHNUM, {target.phnum}},
        {AT_PAGESZ, {kPageSize}},
        {AT_BASE, {at_base}},
        {AT_FLAGS, {0}},
        {AT_ENTRY, {target.entry}},
        {AT_UID, {0}},
        {AT_EUID, {0}},
        {AT_GID, {0}},
        {AT_EGID, {0}},
        {AT_SECURE, {0}},
        {AT_CLKTCK, {static_cast<uint64_t>(getauxval(AT_CLKTCK))}},
        {AT_HWCAP, {static_cast<uint64_t>(getauxval(AT_HWCAP))}},
        {AT_HWCAP2, {static_cast<uint64_t>(getauxval(AT_HWCAP2))}},
        {AT_SYSINFO_EHDR, {static_cast<uint64_t>(getauxval(AT_SYSINFO_EHDR))}},
        {AT_RANDOM, {at_random}},
        {AT_EXECFN, {at_execfn}},
        {AT_NULL, {0}},
    };

    const size_t word_count =
        1 /* argc */ + arg_ptrs.size() + 1 /* null */ + env_ptrs.size() + 1 /* null */ + auxv.size() * 2;
    const size_t table_bytes = word_count * sizeof(uint64_t);
    uintptr_t table_addr = (reinterpret_cast<uintptr_t>(sp) - table_bytes) & ~static_cast<uintptr_t>(15);
    auto* words = reinterpret_cast<uint64_t*>(table_addr);

    size_t idx = 0;
    words[idx++] = static_cast<uint64_t>(arg_ptrs.size());
    for (uintptr_t p : arg_ptrs) words[idx++] = static_cast<uint64_t>(p);
    words[idx++] = 0;
    for (uintptr_t p : env_ptrs) words[idx++] = static_cast<uint64_t>(p);
    words[idx++] = 0;
    for (const auto& av : auxv) {
        words[idx++] = static_cast<uint64_t>(av.a_type);
        words[idx++] = static_cast<uint64_t>(av.a_un.a_val);
    }

    jump_to_entry(table_addr, entry_pc);
}

}  // namespace karyon::elf
