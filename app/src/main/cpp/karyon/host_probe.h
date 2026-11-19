#pragma once

#include <cstdint>
#include <string>

namespace karyon::host {

struct HostCapabilityProbe {
    uint32_t host_uid = 0;
    int32_t host_sdk_int = 0;
    std::string kernel_release;
    std::string abi;
    std::string selinux_mode;
    bool memfd_supported = false;
    bool execmem_supported = false;
    bool file_exec_mmap_supported = false;
    bool kvm_accessible = false;
    bool init_binary_executable = false;
    bool phantom_process_killer_risk = false;
    std::string init_binary_path;
};

HostCapabilityProbe probe_host_runtime(const std::string& native_lib_dir, const std::string& files_dir, int32_t sdk_int);

}  // namespace karyon::host
