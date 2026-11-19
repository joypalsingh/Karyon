#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace karyon::ipc {

struct ConsolidatedServiceEntry {
    uint32_t handle = 0;
    std::string name;
    std::string descriptor;
    int32_t owner_pid = 0;
    bool in_process_consolidated = true;
    uint64_t tx_count = 0;
};

/**
 * Consolidated in-process Userspace Binder Service Registry.
 * Consolidates core container services (Supervisor, PackageManager, ActivityManager,
 * Window/Input, SurfaceFlinger bridge, Container su) into Karyon's minimal daemon footprint
 * to avoid spawning excess child processes under Android 12+ Phantom Process limits.
 */
class BinderBrokerRegistry {
public:
    BinderBrokerRegistry();

    void reset_for_instance(const std::string& instance_id, int32_t supervisor_pid);
    uint32_t register_service(const std::string& name, const std::string& descriptor, int32_t owner_pid, bool in_process);
    uint32_t check_service(const std::string& name);
    bool record_transaction(uint32_t handle);
    std::vector<ConsolidatedServiceEntry> list_services() const;
    std::string active_instance() const;

private:
    mutable std::mutex mu_;
    std::string active_instance_;
    uint32_t next_handle_ = 1;
    std::vector<ConsolidatedServiceEntry> services_;
};

BinderBrokerRegistry& global_binder_registry();

}  // namespace karyon::ipc
