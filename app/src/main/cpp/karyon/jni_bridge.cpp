#include <android/native_window_jni.h>
#include <jni.h>

#include <sstream>
#include <string>
#include <vector>

#include "axml_parser.h"
#include "binder_broker.h"
#include "dex/dex_file.h"
#include "dex/disassembler.h"
#include "host_probe.h"
#include "rootfs_store.h"
#include "vm_supervisor.h"

namespace {

std::string jstr_to_std(JNIEnv* env, jstring jstr) {
    if (!jstr) return {};
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return out;
}

std::vector<uint8_t> jbytes_to_vec(JNIEnv* env, jbyteArray arr) {
    if (!arr) return {};
    const jsize len = env->GetArrayLength(arr);
    if (len <= 0) return {};
    std::vector<uint8_t> out(static_cast<size_t>(len));
    env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

std::string escape_kv(std::string s) {
    for (char& ch : s) {
        if (ch == '\n' || ch == '\r' || ch == '|') ch = ' ';
    }
    return s;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_getEngineVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("Karyon Container Supervisor 0.1.0 (C++20 / Dexe-Core)");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_probeHostRuntime(
    JNIEnv* env,
    jobject,
    jstring native_lib_dir,
    jstring files_dir,
    jint sdk_int) {
    const auto probe = karyon::host::probe_host_runtime(
        jstr_to_std(env, native_lib_dir),
        jstr_to_std(env, files_dir),
        static_cast<int32_t>(sdk_int));
    std::ostringstream ss;
    ss << "uid=" << probe.host_uid << "\n"
       << "sdk=" << probe.host_sdk_int << "\n"
       << "kernel=" << escape_kv(probe.kernel_release) << "\n"
       << "abi=" << escape_kv(probe.abi) << "\n"
       << "selinux=" << escape_kv(probe.selinux_mode) << "\n"
       << "memfd=" << (probe.memfd_supported ? 1 : 0) << "\n"
       << "execmem=" << (probe.execmem_supported ? 1 : 0) << "\n"
       << "file_exec_mmap=" << (probe.file_exec_mmap_supported ? 1 : 0) << "\n"
       << "kvm=" << (probe.kvm_accessible ? 1 : 0) << "\n"
       << "init_exec=" << (probe.init_binary_executable ? 1 : 0) << "\n"
       << "phantom_risk=" << (probe.phantom_process_killer_risk ? 1 : 0) << "\n"
       << "init_path=" << escape_kv(probe.init_binary_path) << "\n";
    return env->NewStringUTF(ss.str().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_ensureVmInstance(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id,
    jint api_level,
    jboolean gapps_enabled,
    jboolean container_su) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    return store.ensure_instance(
               jstr_to_std(env, instance_id),
               static_cast<int32_t>(api_level),
               gapps_enabled == JNI_TRUE,
               container_su == JNI_TRUE)
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_cloneVmInstance(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring src_id,
    jstring dst_id) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    return store.clone_instance(jstr_to_std(env, src_id), jstr_to_std(env, dst_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_snapshotVm(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id,
    jstring snapshot_id) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    return store.create_snapshot(jstr_to_std(env, instance_id), jstr_to_std(env, snapshot_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_restoreSnapshot(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id,
    jstring snapshot_id) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    return store.restore_snapshot(jstr_to_std(env, instance_id), jstr_to_std(env, snapshot_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_deleteVmInstance(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    return store.delete_instance(jstr_to_std(env, instance_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_inspectRootfs(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    const auto info = store.inspect_instance(jstr_to_std(env, instance_id));
    std::ostringstream ss;
    ss << "init=" << (info.has_init_binary ? 1 : 0) << "\n"
       << "init_arch=" << escape_kv(info.init_elf_arch) << "\n"
       << "services=" << (info.has_services_jar ? 1 : 0) << "\n"
       << "framework=" << (info.has_framework_jar ? 1 : 0) << "\n"
       << "gapps_gms=" << (info.has_gapps_gms ? 1 : 0) << "\n"
       << "gapps_store=" << (info.has_gapps_play_store ? 1 : 0) << "\n"
       << "su=" << (info.has_container_su ? 1 : 0) << "\n"
       << "bootable=" << (info.is_bootable_aosp ? 1 : 0) << "\n"
       << "files=" << info.total_files << "\n"
       << "bytes=" << info.total_bytes << "\n"
       << "snapshots=" << info.snapshot_count << "\n";
    return env->NewStringUTF(ss.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_parseApkManifest(
    JNIEnv* env,
    jobject,
    jbyteArray axml_bytes) {
    const auto bytes = jbytes_to_vec(env, axml_bytes);
    const auto parsed = karyon::apk::parse_binary_manifest(bytes);
    std::ostringstream ss;
    ss << "valid=" << (parsed.valid ? 1 : 0) << "\n"
       << "package=" << escape_kv(parsed.package_name) << "\n"
       << "version=" << escape_kv(parsed.version_name) << "\n"
       << "min_sdk=" << parsed.min_sdk << "\n"
       << "target_sdk=" << parsed.target_sdk << "\n"
       << "main_activity=" << escape_kv(parsed.main_activity) << "\n"
       << "perm_count=" << parsed.permissions.size() << "\n"
       << "error=" << escape_kv(parsed.error) << "\n";
    for (size_t i = 0; i < parsed.permissions.size() && i < 20; ++i) {
        ss << "perm=" << escape_kv(parsed.permissions[i]) << "\n";
    }
    return env->NewStringUTF(ss.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_disassembleDexSummary(
    JNIEnv* env,
    jobject,
    jbyteArray dex_bytes,
    jint max_classes) {
    auto bytes = jbytes_to_vec(env, dex_bytes);
    if (bytes.empty()) {
        return env->NewStringUTF("ERROR: Empty DEX buffer");
    }
    try {
        auto dex = dexe::dex::DexFile::parse(std::move(bytes), "classes.dex");
        const auto& defs = dex->class_defs();
        std::ostringstream ss;
        ss << "version=" << dex->version()
           << " checksum=" << (dex->checksum_ok() ? "ok" : "bad")
           << " classes=" << defs.size()
           << " methods=" << dex->num_methods()
           << " strings=" << dex->num_strings() << "\n";
        const size_t limit = std::min<size_t>(defs.size(), static_cast<size_t>(std::max<jint>(0, max_classes)));
        for (size_t i = 0; i < limit; ++i) {
            ss << dex->type_descriptor(defs[i].class_idx) << "\n";
        }
        return env->NewStringUTF(ss.str().c_str());
    } catch (const std::exception& e) {
        const std::string err = std::string("ERROR: ") + e.what();
        return env->NewStringUTF(err.c_str());
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_startVmRuntime(
    JNIEnv* env,
    jobject,
    jstring root_dir,
    jstring instance_id,
    jstring init_binary_path) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    const std::string id = jstr_to_std(env, instance_id);
    return karyon::supervisor::global_supervisor().start_vm(
               id,
               store.instance_dir(id),
               jstr_to_std(env, init_binary_path))
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_stopVmRuntime(JNIEnv*, jobject) {
    karyon::supervisor::global_supervisor().stop_vm();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_sendVmCommand(JNIEnv* env, jobject, jstring cmd) {
    return karyon::supervisor::global_supervisor().send_command(jstr_to_std(env, cmd)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_attachSurface(JNIEnv* env, jobject, jobject surface) {
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    karyon::supervisor::global_supervisor().attach_surface(win);
    if (win) {
        ANativeWindow_release(win);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_detachSurface(JNIEnv*, jobject) {
    karyon::supervisor::global_supervisor().detach_surface();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_pollVmStatusAndLogs(JNIEnv* env, jobject) {
    auto& sup = karyon::supervisor::global_supervisor();
    const auto st = sup.status();
    const auto logs = sup.drain_logs(80);
    std::ostringstream ss;
    ss << "running=" << (st.running ? 1 : 0) << "\n"
       << "pid=" << st.child_pid << "\n"
       << "frames=" << st.frames_rendered << "\n"
       << "instance=" << escape_kv(st.instance_id) << "\n"
       << "reply=" << escape_kv(st.last_ctrl_reply) << "\n";
    for (const auto& l : logs) {
        ss << "log=" << escape_kv(l) << "\n";
    }
    return env->NewStringUTF(ss.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_listConsolidatedServices(JNIEnv* env, jobject) {
    const auto services = karyon::ipc::global_binder_registry().list_services();
    std::ostringstream ss;
    ss << "instance=" << karyon::ipc::global_binder_registry().active_instance()
       << " count=" << services.size() << "\n";
    for (const auto& s : services) {
        ss << "[" << s.handle << "] " << s.name
           << " (" << s.descriptor << ") pid=" << s.owner_pid
           << " tx=" << s.tx_count
           << (s.in_process_consolidated ? " [consolidated]" : "") << "\n";
    }
    return env->NewStringUTF(ss.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_karyon_app_nativebridge_KaryonNative_runGuestElf(
    JNIEnv* env,
    jobject,
    jstring elf_exec_binary_path,
    jstring root_dir,
    jstring instance_id,
    jstring guest_binary,
    jobjectArray args_arr) {
    karyon::storage::RootfsStore store(jstr_to_std(env, root_dir));
    const std::string id = jstr_to_std(env, instance_id);
    std::vector<std::string> args;
    if (args_arr) {
        const jsize n = env->GetArrayLength(args_arr);
        args.reserve(static_cast<size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            auto jarg = static_cast<jstring>(env->GetObjectArrayElement(args_arr, i));
            args.push_back(jstr_to_std(env, jarg));
            if (jarg) env->DeleteLocalRef(jarg);
        }
    }
    const auto out = karyon::supervisor::global_supervisor().run_guest_elf(
        jstr_to_std(env, elf_exec_binary_path),
        store.rootfs_dir(id),
        jstr_to_std(env, guest_binary),
        args);
    std::ostringstream ss;
    ss << "exit=" << out.exit_code << "\n" << out.output;
    return env->NewStringUTF(ss.str().c_str());
}
