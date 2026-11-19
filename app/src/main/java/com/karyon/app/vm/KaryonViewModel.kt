package com.karyon.app.vm

import android.app.Application
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.karyon.app.model.HostAppEntry
import com.karyon.app.model.HostCapabilityReport
import com.karyon.app.model.ParsedApkInfo
import com.karyon.app.model.RootfsSummary
import com.karyon.app.model.ShellLine
import com.karyon.app.model.VfsEntryItem
import com.karyon.app.model.VmInstance
import com.karyon.app.model.VmLifecycleState
import com.karyon.app.model.VmRuntimeStatus
import com.karyon.app.nativebridge.KaryonNative
import com.karyon.app.runtime.ContainerArtifacts
import com.karyon.app.service.KaryonSupervisorService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream

class KaryonViewModel(application: Application) : AndroidViewModel(application) {

    private val appContext = application.applicationContext
    private val rootDirPath = appContext.filesDir.absolutePath

    private val _engineVersion = MutableStateFlow("Loading native core...")
    val engineVersion: StateFlow<String> = _engineVersion.asStateFlow()

    private val _hostProbe = MutableStateFlow(HostCapabilityReport())
    val hostProbe: StateFlow<HostCapabilityReport> = _hostProbe.asStateFlow()

    private val _instances = MutableStateFlow<List<VmInstance>>(emptyList())
    val instances: StateFlow<List<VmInstance>> = _instances.asStateFlow()

    private val _selectedVmId = MutableStateFlow<String?>(null)
    val selectedVmId: StateFlow<String?> = _selectedVmId.asStateFlow()

    private val _runtimeStatus = MutableStateFlow(VmRuntimeStatus())
    val runtimeStatus: StateFlow<VmRuntimeStatus> = _runtimeStatus.asStateFlow()

    private val _lifecycleState = MutableStateFlow(VmLifecycleState.STOPPED)
    val lifecycleState: StateFlow<VmLifecycleState> = _lifecycleState.asStateFlow()

    private val _brokerLogs = MutableStateFlow<List<String>>(emptyList())
    val brokerLogs: StateFlow<List<String>> = _brokerLogs.asStateFlow()

    private val _shellHistory = MutableStateFlow<List<ShellLine>>(
        listOf(
            ShellLine(
                isCommand = false,
                text = "Karyon Container Shell ready. Commands: help, services, elf-exec <bin> [args], rootfs, ls [path], cat <path>"
            )
        )
    )
    val shellHistory: StateFlow<List<ShellLine>> = _shellHistory.asStateFlow()

    private val _statusBanner = MutableStateFlow<String?>(null)
    val statusBanner: StateFlow<String?> = _statusBanner.asStateFlow()

    private val _hostApps = MutableStateFlow<List<HostAppEntry>>(emptyList())
    val hostApps: StateFlow<List<HostAppEntry>> = _hostApps.asStateFlow()

    private val _inspectedApk = MutableStateFlow<ParsedApkInfo?>(null)
    val inspectedApk: StateFlow<ParsedApkInfo?> = _inspectedApk.asStateFlow()

    private val _vfsEntries = MutableStateFlow<List<VfsEntryItem>>(emptyList())
    val vfsEntries: StateFlow<List<VfsEntryItem>> = _vfsEntries.asStateFlow()

    private val _bundledRootfsAsset = MutableStateFlow<String?>(null)
    val bundledRootfsAsset: StateFlow<String?> = _bundledRootfsAsset.asStateFlow()

    private var pollJob: Job? = null
    private var provisionJob: Job? = null

    init {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                _engineVersion.value = KaryonNative.getEngineVersion()
                val probeRaw = KaryonNative.probeHostRuntime(
                    appContext.applicationInfo.nativeLibraryDir.orEmpty(),
                    rootDirPath,
                    Build.VERSION.SDK_INT
                )
                val probe = KaryonNative.parseHostProbe(probeRaw)
                _hostProbe.value = probe
                _bundledRootfsAsset.value = findBundledRootfsAsset(probe.abi)
                loadOrSeedInstances()
                loadInstalledHostApps()
            }
            refreshVfsEntries()
            val primary = _instances.value.firstOrNull { it.id == "aosp14_primary" }
            val assetName = _bundledRootfsAsset.value
            if (primary != null && !primary.rootfsSummary.isBootableAosp && assetName != null) {
                installBundledRootfs(primary.id, assetName)
            }
        }
    }

    private fun findBundledRootfsAsset(hostAbi: String): String? {
        val list = runCatching { appContext.assets.list("rootfs")?.toList().orEmpty() }.getOrDefault(emptyList())
        if (list.isEmpty()) return null
        val tars = list.filter { it.endsWith(".tar") || it.endsWith(".tar.gz") || it.endsWith(".tgz") }
        val normalizedAbi = hostAbi.lowercase()
        return tars.firstOrNull { it.lowercase().contains(normalizedAbi) }
            ?: if (normalizedAbi.contains("arm64")) tars.firstOrNull { it.lowercase().contains("arm64") } else null
    }

    private fun metadataFile(): File {
        val dir = File(rootDirPath, "karyon_v1")
        dir.mkdirs()
        return File(dir, "instances.json")
    }

    private fun loadOrSeedInstances() {
        val file = metadataFile()
        val parsed = mutableListOf<VmInstance>()
        if (file.exists()) {
            runCatching {
                val arr = JSONArray(file.readText())
                for (i in 0 until arr.length()) {
                    val obj = arr.getJSONObject(i)
                    val id = obj.getString("id")
                    val api = obj.optInt("targetApi", 34)
                    val gapps = obj.optBoolean("gappsRequested", true)
                    val su = obj.optBoolean("containerSuRequested", true)
                    KaryonNative.ensureVmInstance(rootDirPath, id, api, gapps, su)
                    val summary = KaryonNative.parseRootfsInspection(
                        KaryonNative.inspectRootfs(rootDirPath, id)
                    )
                    parsed += VmInstance(
                        id = id,
                        name = obj.optString("name", id),
                        targetApi = api,
                        gappsRequested = gapps,
                        containerSuRequested = su,
                        createdAtMs = obj.optLong("createdAtMs", System.currentTimeMillis()),
                        rootfsSummary = summary
                    )
                }
            }
        }
        if (parsed.isEmpty()) {
            val defaultId = "aosp14_primary"
            KaryonNative.ensureVmInstance(rootDirPath, defaultId, 34, true, true)
            val summary = KaryonNative.parseRootfsInspection(
                KaryonNative.inspectRootfs(rootDirPath, defaultId)
            )
            parsed += VmInstance(
                id = defaultId,
                name = "AOSP 14 Primary",
                targetApi = 34,
                gappsRequested = true,
                containerSuRequested = true,
                rootfsSummary = summary
            )
            saveInstancesLocked(parsed)
        }
        _instances.value = parsed
        if (_selectedVmId.value == null) {
            _selectedVmId.value = parsed.firstOrNull()?.id
        }
    }

    private fun saveInstancesLocked(list: List<VmInstance>) {
        val arr = JSONArray()
        for (vm in list) {
            arr.put(
                JSONObject().apply {
                    put("id", vm.id)
                    put("name", vm.name)
                    put("targetApi", vm.targetApi)
                    put("gappsRequested", vm.gappsRequested)
                    put("containerSuRequested", vm.containerSuRequested)
                    put("createdAtMs", vm.createdAtMs)
                }
            )
        }
        metadataFile().writeText(arr.toString(2))
    }

    fun selectVm(id: String) {
        _selectedVmId.value = id
        refreshVfsEntries()
    }

    fun dismissBanner() {
        _statusBanner.value = null
    }

    fun createVm(name: String, targetApi: Int, gappsRequested: Boolean, suRequested: Boolean) {
        viewModelScope.launch {
            val created = withContext(Dispatchers.IO) {
                val baseId = ContainerArtifacts.sanitizeId(name)
                val existingIds = _instances.value.map { it.id }.toSet()
                var candidate = baseId
                var suffix = 2
                while (candidate in existingIds) {
                    candidate = "${baseId.take(34)}_$suffix"
                    suffix++
                }
                if (!KaryonNative.ensureVmInstance(rootDirPath, candidate, targetApi, gappsRequested, suRequested)) {
                    return@withContext null
                }
                val summary = KaryonNative.parseRootfsInspection(
                    KaryonNative.inspectRootfs(rootDirPath, candidate)
                )
                VmInstance(
                    id = candidate,
                    name = name.ifBlank { candidate },
                    targetApi = targetApi,
                    gappsRequested = gappsRequested,
                    containerSuRequested = suRequested,
                    rootfsSummary = summary
                )
            }
            if (created != null) {
                val updated = _instances.value + created
                _instances.value = updated
                _selectedVmId.value = created.id
                withContext(Dispatchers.IO) { saveInstancesLocked(updated) }
                refreshVfsEntries()
                _statusBanner.value = "Created VM '${created.name}'"
            } else {
                _statusBanner.value = "Failed to initialize VM storage"
            }
        }
    }

    fun cloneVm(src: VmInstance) {
        viewModelScope.launch {
            val cloned = withContext(Dispatchers.IO) {
                val existingIds = _instances.value.map { it.id }.toSet()
                var idx = 2
                var newId = "${src.id.take(32)}_clone"
                while (newId in existingIds) {
                    newId = "${src.id.take(30)}_clone$idx"
                    idx++
                }
                if (!KaryonNative.cloneVmInstance(rootDirPath, src.id, newId)) {
                    return@withContext null
                }
                val summary = KaryonNative.parseRootfsInspection(
                    KaryonNative.inspectRootfs(rootDirPath, newId)
                )
                src.copy(
                    id = newId,
                    name = "${src.name} (Clone)",
                    createdAtMs = System.currentTimeMillis(),
                    rootfsSummary = summary
                )
            }
            if (cloned != null) {
                val updated = _instances.value + cloned
                _instances.value = updated
                withContext(Dispatchers.IO) { saveInstancesLocked(updated) }
                _statusBanner.value = "Cloned '${src.name}' -> '${cloned.name}'"
            } else {
                _statusBanner.value = "Failed to clone VM '${src.name}'"
            }
        }
    }

    fun snapshotVm(vm: VmInstance) {
        viewModelScope.launch {
            val snapId = "snap_${System.currentTimeMillis()}"
            val ok = withContext(Dispatchers.IO) {
                KaryonNative.snapshotVm(rootDirPath, vm.id, snapId)
            }
            refreshInstanceSummary(vm.id)
            _statusBanner.value = if (ok) "Snapshot saved ($snapId)" else "Snapshot failed"
        }
    }

    fun restoreLatestSnapshot(vm: VmInstance) {
        viewModelScope.launch {
            val restored = withContext(Dispatchers.IO) {
                val snapsDir = File(rootDirPath, "karyon_v1/instances/${vm.id}/snapshots")
                val latest = snapsDir.listFiles()?.filter { it.isDirectory }?.maxByOrNull { it.name }
                    ?: return@withContext null
                if (KaryonNative.restoreSnapshot(rootDirPath, vm.id, latest.name)) latest.name else null
            }
            refreshInstanceSummary(vm.id)
            refreshVfsEntries()
            _statusBanner.value = if (restored != null) {
                "Restored snapshot $restored"
            } else {
                "No snapshot available for ${vm.name}"
            }
        }
    }

    fun deleteVm(vm: VmInstance) {
        viewModelScope.launch {
            if (_runtimeStatus.value.running && _runtimeStatus.value.instanceId == vm.id) {
                stopVm()
            }
            withContext(Dispatchers.IO) {
                KaryonNative.deleteVmInstance(rootDirPath, vm.id)
            }
            val updated = _instances.value.filterNot { it.id == vm.id }
            _instances.value = updated
            if (_selectedVmId.value == vm.id) {
                _selectedVmId.value = updated.firstOrNull()?.id
            }
            withContext(Dispatchers.IO) { saveInstancesLocked(updated) }
            refreshVfsEntries()
        }
    }

    fun importRootfsArchive(vmId: String, uri: Uri) {
        viewModelScope.launch {
            _statusBanner.value = "Extracting RootFS archive..."
            val outcome = withContext(Dispatchers.IO) {
                runCatching {
                    val rootfsDir = File(rootDirPath, "karyon_v1/instances/$vmId/rootfs")
                    appContext.contentResolver.openInputStream(uri)?.use { stream ->
                        ContainerArtifacts.extractTarStream(stream, rootfsDir)
                    } ?: throw IllegalStateException("Cannot open selected archive URI")
                }
            }
            refreshInstanceSummary(vmId)
            refreshVfsEntries()
            _statusBanner.value = outcome.fold(
                onSuccess = { r ->
                    "Imported ${r.filesExtracted} files, ${r.symlinksCreated} links (${r.totalBytes / 1024} KB)."
                },
                onFailure = { e ->
                    "RootFS import failed: ${e.message ?: "invalid TAR/TAR.GZ archive"}"
                }
            )
        }
    }

    fun installBundledRootfs(vmId: String, assetOverride: String? = null) {
        val assetName = assetOverride ?: _bundledRootfsAsset.value ?: run {
            _statusBanner.value = "No bundled RootFS matching ABI ${_hostProbe.value.abi}"
            return
        }
        provisionJob = viewModelScope.launch {
            _statusBanner.value = "Extracting bundled RootFS ($assetName)..."
            val outcome = withContext(Dispatchers.IO) {
                runCatching {
                    val rootfsDir = File(rootDirPath, "karyon_v1/instances/$vmId/rootfs")
                    appContext.assets.open("rootfs/$assetName").use { stream ->
                        ContainerArtifacts.extractTarStream(stream, rootfsDir)
                    }
                }
            }
            refreshInstanceSummary(vmId)
            refreshVfsEntries()
            _statusBanner.value = outcome.fold(
                onSuccess = { r ->
                    "Bundled RootFS ready: ${r.filesExtracted} files, ${r.symlinksCreated} links (${r.totalBytes / 1024} KB)."
                },
                onFailure = { e ->
                    "Bundled RootFS failed: ${e.message ?: "invalid asset archive"}"
                }
            )
        }
    }

    fun bootVm(vm: VmInstance, onReadyNavigateViewport: () -> Unit) {
        viewModelScope.launch {
            provisionJob?.join()
            _selectedVmId.value = vm.id
            _lifecycleState.value = VmLifecycleState.BOOTING_SUPERVISOR
            val initPath = _hostProbe.value.initBinaryPath.ifBlank {
                File(appContext.applicationInfo.nativeLibraryDir, "libkaryon_init.so").absolutePath
            }
            val started = withContext(Dispatchers.IO) {
                KaryonNative.ensureVmInstance(
                    rootDirPath,
                    vm.id,
                    vm.targetApi,
                    vm.gappsRequested,
                    vm.containerSuRequested
                )
                KaryonNative.startVmRuntime(rootDirPath, vm.id, initPath)
            }
            if (started) {
                KaryonSupervisorService.start(appContext, vm.name)
                refreshInstanceSummary(vm.id)
                val currentSummary = _instances.value.firstOrNull { it.id == vm.id }?.rootfsSummary ?: vm.rootfsSummary
                _lifecycleState.value = if (currentSummary.isBootableAosp) {
                    VmLifecycleState.RUNNING_CONTAINER
                } else {
                    VmLifecycleState.AWAITING_ROOTFS
                }
                startPollingLoop()
                onReadyNavigateViewport()
            } else {
                _lifecycleState.value = VmLifecycleState.STOPPED
                _statusBanner.value = "Failed to exec libkaryon_init.so ($initPath)"
            }
        }
    }

    fun stopVm() {
        viewModelScope.launch {
            pollJob?.cancel()
            pollJob = null
            withContext(Dispatchers.IO) {
                KaryonNative.stopVmRuntime()
            }
            KaryonSupervisorService.stop(appContext)
            _runtimeStatus.value = VmRuntimeStatus()
            _lifecycleState.value = VmLifecycleState.STOPPED
        }
    }

    fun sendViewportCommand(cmd: String) {
        viewModelScope.launch(Dispatchers.IO) {
            KaryonNative.sendVmCommand(cmd)
        }
    }

    fun attachSurface(surface: Surface) {
        KaryonNative.attachSurface(surface)
    }

    fun detachSurface() {
        KaryonNative.detachSurface()
    }

    private fun startPollingLoop() {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            while (isActive) {
                val (status, newLogs) = withContext(Dispatchers.IO) {
                    KaryonNative.parseStatusAndLogs(KaryonNative.pollVmStatusAndLogs())
                }
                _runtimeStatus.value = status
                if (newLogs.isNotEmpty()) {
                    val combined = (_brokerLogs.value + newLogs).takeLast(200)
                    _brokerLogs.value = combined
                }
                if (!status.running && _lifecycleState.value != VmLifecycleState.STOPPED) {
                    _lifecycleState.value = VmLifecycleState.STOPPED
                    KaryonSupervisorService.stop(appContext)
                    break
                }
                delay(250L)
            }
        }
    }

    fun runShellCommand(rawInput: String) {
        val cmd = rawInput.trim()
        if (cmd.isEmpty()) return
        val vm = _instances.value.firstOrNull { it.id == _selectedVmId.value } ?: return

        viewModelScope.launch {
            val outLines = mutableListOf(ShellLine(isCommand = true, text = "$ $cmd"))
            val resultText = withContext(Dispatchers.IO) {
                executeDiagnosticCommand(vm, cmd)
            }
            if (resultText.isNotEmpty()) {
                outLines += ShellLine(isCommand = false, text = resultText)
            }
            _shellHistory.value = (_shellHistory.value + outLines).takeLast(120)
        }
    }

    private fun executeDiagnosticCommand(vm: VmInstance, cmd: String): String {
        val instanceDir = File(rootDirPath, "karyon_v1/instances/${vm.id}")
        val rootfsDir = File(instanceDir, "rootfs")
        val overlayDir = File(instanceDir, "overlay")
        val tokens = cmd.split(Regex("\\s+"))
        return when (tokens.firstOrNull()) {
            "help" -> "Commands: help, services, elf-exec <bin> [args...], rootfs, ls [rel], cat <rel>, uname, id, env, ping"
            "services" -> KaryonNative.listConsolidatedServices().trimEnd()
            "elf-exec" -> {
                val guestBin = tokens.getOrNull(1) ?: return "Usage: elf-exec <rootfs_rel_bin> [args...]"
                val args = if (tokens.size > 2) tokens.drop(2).toTypedArray() else emptyArray()
                val elfExecPath = File(appContext.applicationInfo.nativeLibraryDir, "libkaryon_elf_exec.so").absolutePath
                KaryonNative.runGuestElf(elfExecPath, rootDirPath, vm.id, guestBin, args).trimEnd()
            }
            "ping" -> {
                val ok = KaryonNative.sendVmCommand("PING")
                if (ok) "Sent PING to child pid=${_runtimeStatus.value.childPid}" else "Child supervisor not running"
            }
            "rootfs" -> {
                val info = KaryonNative.parseRootfsInspection(KaryonNative.inspectRootfs(rootDirPath, vm.id))
                buildString {
                    appendLine("Instance: ${vm.id} (target API ${vm.targetApi})")
                    appendLine("init=${info.hasInitBinary} (${info.initElfArch.ifEmpty { "missing" }})")
                    appendLine("services.jar=${info.hasServicesJar} framework.jar=${info.hasFrameworkJar}")
                    appendLine("GApps GMS=${info.hasGappsGms} PlayStore=${info.hasGappsPlayStore} su=${info.hasContainerSu}")
                    appendLine("Files=${info.totalFiles} Bytes=${info.totalBytes} Snapshots=${info.snapshotCount}")
                }.trimEnd()
            }
            "uname", "id", "env" -> {
                val proc = ProcessBuilder("/system/bin/sh", "-c", cmd)
                    .directory(overlayDir)
                    .redirectErrorStream(true)
                    .start()
                proc.inputStream.bufferedReader().readText().trim().ifEmpty { "(no output)" }
            }
            "ls" -> {
                val rel = tokens.getOrNull(1)?.trimStart('/') ?: ""
                val target = resolveSafeInside(instanceDir, if (rel.isEmpty()) "overlay" else rel)
                    ?: return "Path outside VM instance blocked."
                val list = target.listFiles()?.sortedBy { it.name } ?: return "Directory not found: $rel"
                if (list.isEmpty()) "(empty)" else list.joinToString("\n") {
                    if (it.isDirectory) "${it.name}/" else "${it.name} (${it.length()} B)"
                }
            }
            "cat" -> {
                val rel = tokens.getOrNull(1)?.trimStart('/') ?: return "Usage: cat <overlay/... or rootfs/...>"
                val target = resolveSafeInside(instanceDir, rel) ?: return "Path outside VM instance blocked."
                if (!target.isFile) return "Not a regular file: $rel"
                target.readText().take(4000)
            }
            else -> {
                val suBin = listOf(File(overlayDir, "system/xbin/su"), File(rootfsDir, "system/xbin/su"))
                    .firstOrNull { it.exists() && it.canExecute() }
                if (suBin != null) {
                    "Guest root binary detected at ${suBin.absolutePath}; interactive container sh/su attaches after Phase 2 rootfs boot."
                } else {
                    "Unknown diagnostic command '$cmd'. Type 'help'. (Note: full container shell requires importing an AOSP 14 rootfs bundle.)"
                }
            }
        }
    }

    private fun resolveSafeInside(base: File, rel: String): File? {
        val candidate = File(base, rel)
        val baseCan = base.canonicalPath
        val candCan = candidate.canonicalPath
        return if (candCan == baseCan || candCan.startsWith(baseCan + File.separator)) candidate else null
    }

    private fun refreshInstanceSummary(vmId: String) {
        val summary = KaryonNative.parseRootfsInspection(KaryonNative.inspectRootfs(rootDirPath, vmId))
        _instances.value = _instances.value.map {
            if (it.id == vmId) it.copy(rootfsSummary = summary) else it
        }
    }

    fun refreshVfsEntries() {
        val vmId = _selectedVmId.value ?: return
        viewModelScope.launch {
            val entries = withContext(Dispatchers.IO) {
                val base = File(rootDirPath, "karyon_v1/instances/$vmId")
                if (!base.exists()) return@withContext emptyList()
                val out = mutableListOf<VfsEntryItem>()
                base.walkTopDown()
                    .maxDepth(4)
                    .filter { it != base }
                    .take(120)
                    .forEach { f ->
                        val rel = f.relativeTo(base).path
                        out += VfsEntryItem(rel, f.isDirectory, if (f.isFile) f.length() else 0L)
                    }
                out
            }
            _vfsEntries.value = entries
        }
    }

    private fun loadInstalledHostApps() {
        val pm = appContext.packageManager
        val apps = runCatching {
            pm.getInstalledApplications(PackageManager.GET_META_DATA)
                .asSequence()
                .filter { (it.flags and ApplicationInfo.FLAG_SYSTEM) == 0 && it.packageName != appContext.packageName }
                .mapNotNull { info ->
                    val src = info.publicSourceDir ?: return@mapNotNull null
                    val file = File(src)
                    HostAppEntry(
                        packageName = info.packageName,
                        label = info.loadLabel(pm).toString(),
                        apkPath = src,
                        apkSizeBytes = if (file.exists()) file.length() else 0L
                    )
                }
                .sortedBy { it.label.lowercase() }
                .take(40)
                .toList()
        }.getOrDefault(emptyList())
        _hostApps.value = apps
    }

    fun inspectHostApp(app: HostAppEntry) {
        inspectApkFile(File(app.apkPath))
    }

    fun inspectApkUri(uri: Uri) {
        viewModelScope.launch {
            val temp = File(appContext.cacheDir, "inspect_target.apk")
            val copied = withContext(Dispatchers.IO) {
                runCatching {
                    appContext.contentResolver.openInputStream(uri)?.use { input ->
                        FileOutputStream(temp).use { output -> input.copyTo(output) }
                    }
                    temp.exists() && temp.length() > 0L
                }.getOrDefault(false)
            }
            if (copied) {
                inspectApkFile(temp)
            }
        }
    }

    private fun inspectApkFile(apkFile: File) {
        viewModelScope.launch {
            val info = withContext(Dispatchers.IO) {
                runCatching {
                    val entries = ContainerArtifacts.readApkEntries(apkFile)
                    val manifestRaw = entries.manifestBytes?.let { KaryonNative.parseApkManifest(it) }
                        ?: "valid=0\nerror=Missing AndroidManifest.xml\n"
                    val dexSummary = entries.primaryDexBytes?.let { KaryonNative.disassembleDexSummary(it, 24) }
                        ?: "No classes.dex found"
                    KaryonNative.parseManifestResult(manifestRaw, dexSummary)
                }.getOrElse { e ->
                    ParsedApkInfo(valid = false, error = e.message ?: "Failed to inspect APK")
                }
            }
            _inspectedApk.value = info
        }
    }

    override fun onCleared() {
        super.onCleared()
        pollJob?.cancel()
        KaryonNative.detachSurface()
        KaryonNative.stopVmRuntime()
        KaryonSupervisorService.stop(appContext)
    }
}
