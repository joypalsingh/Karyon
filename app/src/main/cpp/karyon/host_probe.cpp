#include "host_probe.h"

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#include <string>

namespace karyon::host {

HostCapabilityProbe probe_host_runtime(const std::string& native_lib_dir, const std::string& files_dir, int32_t sdk_int) {
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

    void* exec_page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (exec_page != MAP_FAILED) {
        if (mprotect(exec_page, 4096, PROT_READ | PROT_EXEC) == 0) {
            probe.execmem_supported = true;
        }
        munmap(exec_page, 4096);
    }

    if (!files_dir.empty()) {
        const std::string probe_file = files_dir + "/.karyon_mmap_probe.bin";
        const int fd = open(probe_file.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (fd >= 0) {
            uint8_t zero = 0;
            (void)lseek(fd, 4095, SEEK_SET);
            (void)write(fd, &zero, 1);
            void* mapped = mmap(nullptr, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                probe.file_exec_mmap_supported = true;
                munmap(mapped, 4096);
            }
            close(fd);
            unlink(probe_file.c_str());
        }
    }

    probe.kvm_accessible = (access("/dev/kvm", R_OK | W_OK) == 0);

    if (!native_lib_dir.empty()) {
        probe.init_binary_path = native_lib_dir + "/libkaryon_init.so";
        probe.init_binary_executable = (access(probe.init_binary_path.c_str(), X_OK) == 0);
    }

    return probe;
}

}  // namespace karyon::host
