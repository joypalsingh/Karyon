package com.karyon.app.nativebridge

import android.view.Surface
import com.karyon.app.model.HostCapabilityReport
import com.karyon.app.model.ParsedApkInfo
import com.karyon.app.model.RootfsSummary
import com.karyon.app.model.VmRuntimeStatus

object KaryonParsers {
    fun parseHostProbe(raw: String): HostCapabilityReport {
        val map = parseKvMap(raw)
        return HostCapabilityReport(
            hostUid = map["uid"]?.toIntOrNull() ?: 0,
            hostSdkInt = map["sdk"]?.toIntOrNull() ?: 0,
            kernelRelease = map["kernel"].orEmpty(),
            abi = map["abi"].orEmpty(),
            selinuxMode = map["selinux"].orEmpty(),
            memfdSupported = map["memfd"] == "1",
            kvmAccessible = map["kvm"] == "1",
            initBinaryExecutable = map["init_exec"] == "1",
            phantomProcessKillerRisk = map["phantom_risk"] == "1",
            initBinaryPath = map["init_path"].orEmpty()
        )
    }

    fun parseRootfsInspection(raw: String): RootfsSummary {
        val map = parseKvMap(raw)
        return RootfsSummary(
            hasInitBinary = map["init"] == "1",
            initElfArch = map["init_arch"].orEmpty(),
            hasServicesJar = map["services"] == "1",
            hasFrameworkJar = map["framework"] == "1",
            hasGappsGms = map["gapps_gms"] == "1",
            hasGappsPlayStore = map["gapps_store"] == "1",
            hasContainerSu = map["su"] == "1",
            isBootableAosp = map["bootable"] == "1",
            totalFiles = map["files"]?.toLongOrNull() ?: 0L,
            totalBytes = map["bytes"]?.toLongOrNull() ?: 0L,
            snapshotCount = map["snapshots"]?.toIntOrNull() ?: 0
        )
    }

    fun parseManifestResult(raw: String, dexSummary: String): ParsedApkInfo {
        val perms = mutableListOf<String>()
        val map = mutableMapOf<String, String>()
        for (line in raw.lineSequence()) {
            val idx = line.indexOf('=')
            if (idx <= 0) continue
            val k = line.substring(0, idx)
            val v = line.substring(idx + 1)
            if (k == "perm") {
                perms += v
            } else {
                map[k] = v
            }
        }
        return ParsedApkInfo(
            valid = map["valid"] == "1",
            packageName = map["package"].orEmpty(),
            versionName = map["version"].orEmpty(),
            minSdk = map["min_sdk"]?.toIntOrNull() ?: 0,
            targetSdk = map["target_sdk"]?.toIntOrNull() ?: 0,
            mainActivity = map["main_activity"].orEmpty(),
            permissionCount = map["perm_count"]?.toIntOrNull() ?: perms.size,
            permissions = perms,
            dexSummary = dexSummary,
            error = map["error"].orEmpty()
        )
    }

    fun parseStatusAndLogs(raw: String): Pair<VmRuntimeStatus, List<String>> {
        val logs = mutableListOf<String>()
        val map = mutableMapOf<String, String>()
        for (line in raw.lineSequence()) {
            val idx = line.indexOf('=')
            if (idx <= 0) continue
            val k = line.substring(0, idx)
            val v = line.substring(idx + 1)
            if (k == "log") {
                logs += v
            } else {
                map[k] = v
            }
        }
        val status = VmRuntimeStatus(
            running = map["running"] == "1",
            childPid = map["pid"]?.toIntOrNull() ?: -1,
            framesRendered = map["frames"]?.toLongOrNull() ?: 0L,
            instanceId = map["instance"].orEmpty(),
            lastCtrlReply = map["reply"].orEmpty()
        )
        return status to logs
    }

    private fun parseKvMap(raw: String): Map<String, String> {
        val map = mutableMapOf<String, String>()
        for (line in raw.lineSequence()) {
            val idx = line.indexOf('=')
            if (idx > 0) {
                map[line.substring(0, idx)] = line.substring(idx + 1)
            }
        }
        return map
    }
}

object KaryonNative {

    init {
        System.loadLibrary("karyon_core")
    }

    external fun getEngineVersion(): String
    external fun probeHostRuntime(nativeLibDir: String, sdkInt: Int): String

    external fun ensureVmInstance(
        rootDir: String,
        instanceId: String,
        apiLevel: Int,
        gappsEnabled: Boolean,
        containerSu: Boolean
    ): Boolean

    external fun cloneVmInstance(rootDir: String, srcId: String, dstId: String): Boolean
    external fun snapshotVm(rootDir: String, instanceId: String, snapshotId: String): Boolean
    external fun restoreSnapshot(rootDir: String, instanceId: String, snapshotId: String): Boolean
    external fun deleteVmInstance(rootDir: String, instanceId: String): Boolean
    external fun inspectRootfs(rootDir: String, instanceId: String): String

    external fun parseApkManifest(axmlBytes: ByteArray): String
    external fun disassembleDexSummary(dexBytes: ByteArray, maxClasses: Int): String

    external fun startVmRuntime(rootDir: String, instanceId: String, initBinaryPath: String): Boolean
    external fun stopVmRuntime()
    external fun sendVmCommand(cmd: String): Boolean
    external fun attachSurface(surface: Surface?)
    external fun detachSurface()
    external fun pollVmStatusAndLogs(): String

    fun parseHostProbe(raw: String): HostCapabilityReport = KaryonParsers.parseHostProbe(raw)
    fun parseRootfsInspection(raw: String): RootfsSummary = KaryonParsers.parseRootfsInspection(raw)
    fun parseManifestResult(raw: String, dexSummary: String): ParsedApkInfo = KaryonParsers.parseManifestResult(raw, dexSummary)
    fun parseStatusAndLogs(raw: String): Pair<VmRuntimeStatus, List<String>> = KaryonParsers.parseStatusAndLogs(raw)
}
