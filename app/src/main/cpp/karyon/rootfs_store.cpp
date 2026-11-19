#include "rootfs_store.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace karyon::storage {

namespace {

bool is_safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string detect_elf_arch(const fs::path& binary) {
    std::ifstream in(binary, std::ios::binary);
    if (!in) return {};
    std::array<uint8_t, 20> hdr{};
    in.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    if (in.gcount() < 20) return {};
    if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') return "not-elf";
    const uint16_t machine = static_cast<uint16_t>(hdr[18]) | (static_cast<uint16_t>(hdr[19]) << 8);
    switch (machine) {
        case 183: return "aarch64";
        case 62:  return "x86_64";
        case 40:  return "arm";
        case 3:   return "x86";
        default:  return "elf-machine-" + std::to_string(machine);
    }
}

bool contains_any_case_insensitive(const fs::path& dir, const std::vector<std::string>& needles) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    auto to_lower = [](std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    };
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = to_lower(entry.path().filename().string());
        for (const auto& n : needles) {
            if (name.find(n) != std::string::npos) return true;
        }
    }
    return false;
}

}  // namespace

RootfsStore::RootfsStore(std::string root_dir)
    : root_dir_(std::move(root_dir)) {}

std::string RootfsStore::instances_root() const {
    return root_dir_ + "/karyon_v1/instances";
}

std::string RootfsStore::instance_dir(const std::string& instance_id) const {
    return instances_root() + "/" + instance_id;
}

std::string RootfsStore::rootfs_dir(const std::string& instance_id) const {
    return instance_dir(instance_id) + "/rootfs";
}

std::string RootfsStore::overlay_dir(const std::string& instance_id) const {
    return instance_dir(instance_id) + "/overlay";
}

std::string RootfsStore::snapshots_dir(const std::string& instance_id) const {
    return instance_dir(instance_id) + "/snapshots";
}

std::string RootfsStore::sockets_dir(const std::string& instance_id) const {
    return instance_dir(instance_id) + "/sockets";
}

bool RootfsStore::ensure_instance(const std::string& instance_id, int32_t api_level, bool gapps_enabled, bool container_su) {
    if (!is_safe_id(instance_id)) return false;

    std::error_code ec;
    const fs::path rootfs = rootfs_dir(instance_id);
    const fs::path overlay = overlay_dir(instance_id);
    const fs::path snaps = snapshots_dir(instance_id);
    const fs::path socks = sockets_dir(instance_id);

    fs::create_directories(rootfs, ec);
    fs::create_directories(overlay / "data" / "app", ec);
    fs::create_directories(overlay / "data" / "data", ec);
    fs::create_directories(overlay / "system" / "etc", ec);
    fs::create_directories(snaps, ec);
    fs::create_directories(socks, ec);
    if (ec) return false;

    const fs::path conf = overlay / "system" / "etc" / "karyon_vm.conf";
    if (!fs::exists(conf, ec)) {
        std::ofstream out(conf);
        out << "karyon.instance_id=" << instance_id << "\n"
            << "karyon.target_api=" << api_level << "\n"
            << "karyon.gapps_requested=" << (gapps_enabled ? "1" : "0") << "\n"
            << "karyon.container_su_requested=" << (container_su ? "1" : "0") << "\n";
    }
    return true;
}

bool RootfsStore::clone_instance(const std::string& src_id, const std::string& dst_id) {
    if (!is_safe_id(src_id) || !is_safe_id(dst_id) || src_id == dst_id) return false;
    std::error_code ec;
    const fs::path src = instance_dir(src_id);
    const fs::path dst = instance_dir(dst_id);
    if (!fs::exists(src, ec) || fs::exists(dst, ec)) return false;

    fs::create_directories(dst / "sockets", ec);
    if (fs::exists(src / "rootfs", ec)) {
        fs::copy(src / "rootfs", dst / "rootfs", fs::copy_options::recursive, ec);
        if (ec) return false;
    }
    if (fs::exists(src / "overlay", ec)) {
        fs::copy(src / "overlay", dst / "overlay", fs::copy_options::recursive, ec);
        if (ec) return false;
    }
    fs::create_directories(dst / "snapshots", ec);
    return !ec;
}

bool RootfsStore::create_snapshot(const std::string& instance_id, const std::string& snapshot_id) {
    if (!is_safe_id(instance_id) || !is_safe_id(snapshot_id)) return false;
    std::error_code ec;
    const fs::path ovl = overlay_dir(instance_id);
    if (!fs::exists(ovl, ec)) return false;

    const fs::path snap = fs::path(snapshots_dir(instance_id)) / snapshot_id;
    fs::remove_all(snap, ec);
    fs::create_directories(snap, ec);
    fs::copy(ovl, snap, fs::copy_options::recursive, ec);
    return !ec;
}

bool RootfsStore::restore_snapshot(const std::string& instance_id, const std::string& snapshot_id) {
    if (!is_safe_id(instance_id) || !is_safe_id(snapshot_id)) return false;
    std::error_code ec;
    const fs::path snap = fs::path(snapshots_dir(instance_id)) / snapshot_id;
    const fs::path ovl = overlay_dir(instance_id);
    if (!fs::exists(snap, ec)) return false;

    fs::remove_all(ovl, ec);
    fs::create_directories(ovl, ec);
    fs::copy(snap, ovl, fs::copy_options::recursive, ec);
    return !ec;
}

bool RootfsStore::delete_instance(const std::string& instance_id) {
    if (!is_safe_id(instance_id)) return false;
    std::error_code ec;
    fs::remove_all(instance_dir(instance_id), ec);
    return !ec;
}

RootfsInspection RootfsStore::inspect_instance(const std::string& instance_id) const {
    RootfsInspection out;
    if (!is_safe_id(instance_id)) return out;

    std::error_code ec;
    const fs::path rfs = rootfs_dir(instance_id);
    const fs::path ovl = overlay_dir(instance_id);

    auto resolve_first = [&](const std::string& rel) -> fs::path {
        const fs::path in_ovl = ovl / rel;
        if (fs::exists(in_ovl, ec)) return in_ovl;
        const fs::path in_rfs = rfs / rel;
        if (fs::exists(in_rfs, ec)) return in_rfs;
        return {};
    };

    const fs::path init_path = resolve_first("system/bin/init");
    if (!init_path.empty()) {
        out.init_elf_arch = detect_elf_arch(init_path);
        out.has_init_binary = (out.init_elf_arch == "aarch64" || out.init_elf_arch == "x86_64");
    }

    out.has_services_jar = !resolve_first("system/framework/services.jar").empty();
    out.has_framework_jar = !resolve_first("system/framework/framework.jar").empty();
    out.has_container_su = !resolve_first("system/xbin/su").empty() || !resolve_first("system/bin/su").empty();

    out.has_gapps_gms = contains_any_case_insensitive(rfs / "system" / "priv-app", {"gmscore", "prebuiltgms"}) ||
                        contains_any_case_insensitive(rfs / "product" / "priv-app", {"gmscore", "prebuiltgms"}) ||
                        contains_any_case_insensitive(rfs / "system_ext" / "priv-app", {"gmscore", "prebuiltgms"}) ||
                        contains_any_case_insensitive(ovl / "system" / "priv-app", {"gmscore", "prebuiltgms"}) ||
                        contains_any_case_insensitive(ovl / "product" / "priv-app", {"gmscore", "prebuiltgms"});
    out.has_gapps_play_store = contains_any_case_insensitive(rfs / "system" / "priv-app", {"phonesky"}) ||
                               contains_any_case_insensitive(rfs / "product" / "priv-app", {"phonesky"}) ||
                               contains_any_case_insensitive(rfs / "system_ext" / "priv-app", {"phonesky"}) ||
                               contains_any_case_insensitive(ovl / "system" / "priv-app", {"phonesky"}) ||
                               contains_any_case_insensitive(ovl / "product" / "priv-app", {"phonesky"});

    out.is_bootable_aosp = out.has_init_binary && out.has_services_jar && out.has_framework_jar;

    auto count_tree = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec)) return;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            if (e.is_regular_file(ec)) {
                ++out.total_files;
                out.total_bytes += e.file_size(ec);
            }
        }
    };
    count_tree(rfs);
    count_tree(ovl);

    out.snapshot_count = static_cast<uint32_t>(list_snapshots(instance_id).size());

    const fs::path prop = resolve_first("system/build.prop");
    if (!prop.empty()) {
        std::ifstream in(prop);
        std::ostringstream ss;
        std::string line;
        int kept = 0;
        while (std::getline(in, line) && kept < 8) {
            if (line.empty() || line[0] == '#') continue;
            ss << line << "\n";
            ++kept;
        }
        out.build_prop_summary = ss.str();
    }
    return out;
}

std::vector<std::string> RootfsStore::list_snapshots(const std::string& instance_id) const {
    std::vector<std::string> snaps;
    if (!is_safe_id(instance_id)) return snaps;
    std::error_code ec;
    const fs::path dir = snapshots_dir(instance_id);
    if (!fs::exists(dir, ec)) return snaps;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) {
            snaps.push_back(entry.path().filename().string());
        }
    }
    return snaps;
}

}  // namespace karyon::storage
