#include "binder_broker.h"

namespace karyon::ipc {

BinderBrokerRegistry& global_binder_registry() {
    static BinderBrokerRegistry instance;
    return instance;
}

BinderBrokerRegistry::BinderBrokerRegistry() {
    reset_for_instance("idle", -1);
}

void BinderBrokerRegistry::reset_for_instance(const std::string& instance_id, int32_t supervisor_pid) {
    std::lock_guard<std::mutex> lock(mu_);
    active_instance_ = instance_id;
    services_.clear();
    next_handle_ = 1;

    auto add = [&](const char* name, const char* desc) {
        services_.push_back(ConsolidatedServiceEntry{
            next_handle_++,
            name,
            desc,
            supervisor_pid,
            true,
            0,
        });
    };

    add("karyon.supervisor", "com.karyon.os.ISupervisor");
    add("karyon.binder", "com.karyon.os.IBinderBroker");
    add("karyon.su", "com.karyon.os.IContainerRoot");
    add("SurfaceFlinger", "android.ui.ISurfaceComposer");
    add("package", "android.content.pm.IPackageManager");
    add("activity", "android.app.IActivityManager");
    add("window", "android.view.IWindowManager");
    add("input", "android.hardware.input.IInputManager");
}

uint32_t BinderBrokerRegistry::register_service(
    const std::string& name,
    const std::string& descriptor,
    int32_t owner_pid,
    bool in_process) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& s : services_) {
        if (s.name == name) {
            s.descriptor = descriptor;
            s.owner_pid = owner_pid;
            s.in_process_consolidated = in_process;
            return s.handle;
        }
    }
    const uint32_t h = next_handle_++;
    services_.push_back(ConsolidatedServiceEntry{h, name, descriptor, owner_pid, in_process, 0});
    return h;
}

uint32_t BinderBrokerRegistry::check_service(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& s : services_) {
        if (s.name == name) {
            ++s.tx_count;
            return s.handle;
        }
    }
    return 0;
}

bool BinderBrokerRegistry::record_transaction(uint32_t handle) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& s : services_) {
        if (s.handle == handle) {
            ++s.tx_count;
            return true;
        }
    }
    return false;
}

std::vector<ConsolidatedServiceEntry> BinderBrokerRegistry::list_services() const {
    std::lock_guard<std::mutex> lock(mu_);
    return services_;
}

std::string BinderBrokerRegistry::active_instance() const {
    std::lock_guard<std::mutex> lock(mu_);
    return active_instance_;
}

}  // namespace karyon::ipc
