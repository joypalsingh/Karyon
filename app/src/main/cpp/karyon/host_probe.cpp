#include "host_probe.h"

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#include <string>

namespace karyon::host {

HostCapabilityProbe probe_host_runtime(const std::string& native_lib_dir, int32_t sdk_int) {
    HostCapabilityProbe probe;
    probe.host_uid = static_cast<uint32_t>(getuid());
    probe.host_sdk_int = sdk_int;
    probe.phantom_process_killer_risk = (sdk_int >= 31);

#if defined(__aarch64__)
    probe.abi = "arm64-v8a";
#elif defined(__x86_64__)
    probe.abi = "x86_64";
#else
    probe.abi = "unknown";
#endif

    struct utsname uts {};
    if (uname(&uts) == 0) {
        probe.kernel_release = uts.release;
    } else {
        probe.kernel_release = "unknown";
    }

    {
        std::ifstream sel("/sys/fs/selinux/enforce");
        if (sel) {
            char ch = 0;
            sel.get(ch);
            if (ch == '1') probe.selinux_mode = "enforcing";
            else if (ch == '0') probe.selinux_mode = "permissive";
            else probe.selinux_mode = "unknown";
        } else {
            probe.selinux_mode = "restricted";
        }
    }

    const long mfd = syscall(__NR_memfd_create, "karyon_probe", MFD_CLOEXEC);
    if (mfd >= 0) {
        probe.memfd_supported = true;
        close(static_cast<int>(mfd));
    }

    probe.kvm_accessible = (access("/dev/kvm", R_OK | W_OK) == 0);

    if (!native_lib_dir.empty()) {
        probe.init_binary_path = native_lib_dir + "/libkaryon_init.so";
        probe.init_binary_executable = (access(probe.init_binary_path.c_str(), X_OK) == 0);
    }

    return probe;
}

}  // namespace karyon::host
