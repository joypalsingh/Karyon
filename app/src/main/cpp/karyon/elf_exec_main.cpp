#include <cstdio>
#include <string>
#include <vector>

#include "elf_loader.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: libkaryon_elf_exec.so <rootfs_dir> <guest_binary> [args...]\n");
        return 2;
    }

    karyon::elf::ElfExecRequest req;
    req.rootfs_dir = argv[1];
    req.guest_binary = argv[2];
    req.argv.push_back(argv[2]);
    for (int i = 3; i < argc; ++i) {
        req.argv.emplace_back(argv[i]);
    }

    const std::string& r = req.rootfs_dir;
    req.envp = {
        "PATH=/system/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin",
        "ANDROID_ROOT=" + r + "/system",
        "ANDROID_DATA=" + r + "/../overlay/data",
        "ANDROID_ART_ROOT=" + r + "/apex/com.android.art",
        "ANDROID_I18N_ROOT=" + r + "/apex/com.android.i18n",
        "ANDROID_RUNTIME_ROOT=" + r + "/apex/com.android.runtime",
        "LD_LIBRARY_PATH=" + r + "/system/lib64:" + r + "/system/lib64/bootstrap:" +
            r + "/apex/com.android.runtime/lib64:" + r + "/apex/com.android.runtime/lib64/bionic:" +
            r + "/apex/com.android.art/lib64:" + r + "/apex/com.android.i18n/lib64",
    };

    const std::string err = karyon::elf::load_and_exec_elf64(req);
    std::fprintf(stderr, "KARYON_ELF_EXEC ERROR: %s\n", err.c_str());
    return 126;
}
