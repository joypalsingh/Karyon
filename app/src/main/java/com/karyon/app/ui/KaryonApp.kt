package com.karyon.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Dns
import androidx.compose.material.icons.rounded.PhoneAndroid
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import com.karyon.app.vm.KaryonViewModel

private enum class KaryonTab(val label: String) {
    VMS("VMs"),
    VIEWPORT("Viewport"),
    INSPECTOR("Inspector")
}

@Composable
fun KaryonApp(vm: KaryonViewModel = viewModel()) {
    var activeTab by remember { mutableStateOf(KaryonTab.VMS) }
    val snackbarHostState = remember { SnackbarHostState() }

    val engineVersion by vm.engineVersion.collectAsState()
    val hostProbe by vm.hostProbe.collectAsState()
    val instances by vm.instances.collectAsState()
    val selectedVmId by vm.selectedVmId.collectAsState()
    val runtimeStatus by vm.runtimeStatus.collectAsState()
    val lifecycleState by vm.lifecycleState.collectAsState()
    val brokerLogs by vm.brokerLogs.collectAsState()
    val shellHistory by vm.shellHistory.collectAsState()
    val statusBanner by vm.statusBanner.collectAsState()
    val hostApps by vm.hostApps.collectAsState()
    val inspectedApk by vm.inspectedApk.collectAsState()
    val vfsEntries by vm.vfsEntries.collectAsState()

    val selectedVm = instances.firstOrNull { it.id == selectedVmId } ?: instances.firstOrNull()

    LaunchedEffect(statusBanner) {
        val msg = statusBanner
        if (!msg.isNullOrBlank()) {
            snackbarHostState.showSnackbar(msg)
            vm.dismissBanner()
        }
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState) { data ->
                Snackbar(
                    snackbarData = data,
                    containerColor = MaterialTheme.colorScheme.surfaceVariant,
                    contentColor = MaterialTheme.colorScheme.onSurface
                )
            }
        },
        bottomBar = {
            if (activeTab != KaryonTab.VIEWPORT) {
                NavigationBar(containerColor = MaterialTheme.colorScheme.surface) {
                    NavigationBarItem(
                        selected = activeTab == KaryonTab.VMS,
                        onClick = { activeTab = KaryonTab.VMS },
                        icon = { Icon(Icons.Rounded.Dns, contentDescription = null) },
                        label = { Text(KaryonTab.VMS.label) }
                    )
                    NavigationBarItem(
                        selected = activeTab == KaryonTab.VIEWPORT,
                        onClick = { activeTab = KaryonTab.VIEWPORT },
                        icon = { Icon(Icons.Rounded.PhoneAndroid, contentDescription = null) },
                        label = { Text(KaryonTab.VIEWPORT.label) }
                    )
                    NavigationBarItem(
                        selected = activeTab == KaryonTab.INSPECTOR,
                        onClick = { activeTab = KaryonTab.INSPECTOR },
                        icon = { Icon(Icons.Rounded.BugReport, contentDescription = null) },
                        label = { Text(KaryonTab.INSPECTOR.label) }
                    )
                }
            }
        }
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            when (activeTab) {
                KaryonTab.VMS -> DashboardScreen(
                    engineVersion = engineVersion,
                    hostProbe = hostProbe,
                    instances = instances,
                    selectedVmId = selectedVm?.id,
                    runtimeStatus = runtimeStatus,
                    onSelectVm = vm::selectVm,
                    onCreateVm = vm::createVm,
                    onBootVm = { instance ->
                        vm.bootVm(instance) {
                            activeTab = KaryonTab.VIEWPORT
                        }
                    },
                    onStopVm = vm::stopVm,
                    onImportRootfs = vm::importRootfsArchive,
                    onCloneVm = vm::cloneVm,
                    onSnapshotVm = vm::snapshotVm,
                    onRestoreSnapshot = vm::restoreLatestSnapshot,
                    onDeleteVm = vm::deleteVm
                )

                KaryonTab.VIEWPORT -> ViewportScreen(
                    vmInstance = selectedVm,
                    lifecycleState = lifecycleState,
                    runtimeStatus = runtimeStatus,
                    brokerLogs = brokerLogs,
                    shellHistory = shellHistory,
                    onBackToDashboard = { activeTab = KaryonTab.VMS },
                    onBootVm = { target -> vm.bootVm(target) {} },
                    onStopVm = vm::stopVm,
                    onSendCommand = vm::sendViewportCommand,
                    onAttachSurface = vm::attachSurface,
                    onDetachSurface = vm::detachSurface,
                    onRunShellCommand = vm::runShellCommand
                )

                KaryonTab.INSPECTOR -> InspectorScreen(
                    selectedVm = selectedVm,
                    hostApps = hostApps,
                    inspectedApk = inspectedApk,
                    vfsEntries = vfsEntries,
                    onInspectHostApp = vm::inspectHostApp,
                    onInspectApkUri = vm::inspectApkUri,
                    onRefreshVfs = vm::refreshVfsEntries
                )
            }
        }
    }
}
