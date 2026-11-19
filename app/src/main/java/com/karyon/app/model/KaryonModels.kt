package com.karyon.app.model

enum class VmLifecycleState {
    STOPPED,
    BOOTING_SUPERVISOR,
    AWAITING_ROOTFS,
    RUNNING_CONTAINER
}

data class HostCapabilityReport(
    val hostUid: Int = 0,
    val hostSdkInt: Int = 0,
    val kernelRelease: String = "",
    val abi: String = "",
    val selinuxMode: String = "",
    val memfdSupported: Boolean = false,
    val kvmAccessible: Boolean = false,
    val initBinaryExecutable: Boolean = false,
    val phantomProcessKillerRisk: Boolean = false,
    val initBinaryPath: String = ""
)

data class RootfsSummary(
    val hasInitBinary: Boolean = false,
    val initElfArch: String = "",
    val hasServicesJar: Boolean = false,
    val hasFrameworkJar: Boolean = false,
    val hasGappsGms: Boolean = false,
    val hasGappsPlayStore: Boolean = false,
    val hasContainerSu: Boolean = false,
    val isBootableAosp: Boolean = false,
    val totalFiles: Long = 0L,
    val totalBytes: Long = 0L,
    val snapshotCount: Int = 0
)

data class VmInstance(
    val id: String,
    val name: String,
    val targetApi: Int = 34,
    val gappsRequested: Boolean = true,
    val containerSuRequested: Boolean = true,
    val createdAtMs: Long = System.currentTimeMillis(),
    val rootfsSummary: RootfsSummary = RootfsSummary()
)

data class VmRuntimeStatus(
    val running: Boolean = false,
    val childPid: Int = -1,
    val framesRendered: Long = 0L,
    val instanceId: String = "",
    val lastCtrlReply: String = ""
)

data class ParsedApkInfo(
    val valid: Boolean = false,
    val packageName: String = "",
    val versionName: String = "",
    val minSdk: Int = 0,
    val targetSdk: Int = 0,
    val mainActivity: String = "",
    val permissionCount: Int = 0,
    val permissions: List<String> = emptyList(),
    val dexSummary: String = "",
    val error: String = ""
)

data class HostAppEntry(
    val packageName: String,
    val label: String,
    val apkPath: String,
    val apkSizeBytes: Long
)

data class VfsEntryItem(
    val relativePath: String,
    val isDirectory: Boolean,
    val sizeBytes: Long
)

data class ShellLine(
    val isCommand: Boolean,
    val text: String
)
