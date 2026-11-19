#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace karyon::storage {

struct RootfsInspection {
    bool has_init_binary = false;
    bool has_services_jar = false;
    bool has_framework_jar = false;
    bool has_gapps_gms = false;
    bool has_gapps_play_store = false;
    bool has_container_su = false;
    bool is_bootable_aosp = false;
    std::string init_elf_arch;
    uint64_t total_files = 0;
    uint64_t total_bytes = 0;
    uint32_t snapshot_count = 0;
    std::string build_prop_summary;
};

/**
 * Manages per-VM AOSP rootfs, writable delta overlay, snapshot deltas, and Unix socket directories.
 *
 * Layout under <filesDir>/karyon_v1/instances/<id>/:
 *   rootfs/     Read-mostly extracted AOSP rootfs (system/, vendor/, apex/)
 *   overlay/    Writable per-instance delta (/data, /system overrides)
 *   snapshots/  Point-in-time overlay snapshots
 *   sockets/    AF_UNIX broker & framebuffer sockets
 */
class RootfsStore {
public:
    explicit RootfsStore(std::string root_dir);

    std::string instances_root() const;
    std::string instance_dir(const std::string& instance_id) const;
    std::string rootfs_dir(const std::string& instance_id) const;
    std::string overlay_dir(const std::string& instance_id) const;
    std::string snapshots_dir(const std::string& instance_id) const;
    std::string sockets_dir(const std::string& instance_id) const;

    bool ensure_instance(const std::string& instance_id, int32_t api_level, bool gapps_enabled, bool container_su);
    bool clone_instance(const std::string& src_id, const std::string& dst_id);
    bool create_snapshot(const std::string& instance_id, const std::string& snapshot_id);
    bool restore_snapshot(const std::string& instance_id, const std::string& snapshot_id);
    bool delete_instance(const std::string& instance_id);

    RootfsInspection inspect_instance(const std::string& instance_id) const;
    std::vector<std::string> list_snapshots(const std::string& instance_id) const;

private:
    std::string root_dir_;
};

}  // namespace karyon::storage
