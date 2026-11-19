package com.karyon.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.CameraAlt
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.karyon.app.model.HostCapabilityReport
import com.karyon.app.model.VmInstance
import com.karyon.app.model.VmRuntimeStatus

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun DashboardScreen(
    engineVersion: String,
    hostProbe: HostCapabilityReport,
    instances: List<VmInstance>,
    selectedVmId: String?,
    runtimeStatus: VmRuntimeStatus,
    onSelectVm: (String) -> Unit,
    onCreateVm: (name: String, targetApi: Int, gapps: Boolean, su: Boolean) -> Unit,
    onBootVm: (VmInstance) -> Unit,
    onStopVm: () -> Unit,
    onImportRootfs: (String, Uri) -> Unit,
    onCloneVm: (VmInstance) -> Unit,
    onSnapshotVm: (VmInstance) -> Unit,
    onRestoreSnapshot: (VmInstance) -> Unit,
    onDeleteVm: (VmInstance) -> Unit
) {
    val context = LocalContext.current
    var showCreateDialog by remember { mutableStateOf(false) }
    var importTargetVmId by remember { mutableStateOf<String?>(null) }

    val rootfsPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        val vmId = importTargetVmId
        if (uri != null && vmId != null) {
            onImportRootfs(vmId, uri)
        }
        importTargetVmId = null
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "karyon.",
                        style = MaterialTheme.typography.headlineSmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = engineVersion,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                Button(onClick = { showCreateDialog = true }) {
                    Icon(Icons.Rounded.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("New VM")
                }
            }
        }

        item {
            HostCapabilityCard(
                probe = hostProbe,
                onCopyAdbCommand = {
                    val cmd = "adb shell \"/system/bin/device_config put activity_manager max_phantom_processes 2147483647\""
                    val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    cm.setPrimaryClip(ClipData.newPlainText("Karyon ADB Phantom Fix", cmd))
                }
            )
        }

        items(instances, key = { it.id }) { vm ->
            val isSelected = vm.id == selectedVmId
            val isRunning = runtimeStatus.running && runtimeStatus.instanceId == vm.id
            val summary = vm.rootfsSummary

            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onSelectVm(vm.id) },
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(
                    width = if (isSelected) 1.5.dp else 1.dp,
                    color = if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline
                )
            ) {
                Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = vm.name,
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = "id=${vm.id} • Android 14 (API ${vm.targetApi})",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Surface(
                            shape = RoundedCornerShape(6.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant
                        ) {
                            Text(
                                text = when {
                                    isRunning -> "SUPERVISOR PID ${runtimeStatus.childPid}"
                                    summary.isBootableAosp -> "AOSP ROOTFS READY"
                                    else -> "NEEDS ROOTFS TAR"
                                },
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                                fontFamily = FontFamily.Monospace,
                                fontSize = 11.sp,
                                color = when {
                                    isRunning || summary.isBootableAosp -> MaterialTheme.colorScheme.primary
                                    else -> MaterialTheme.colorScheme.error
                                }
                            )
                        }
                    }

                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        StatusChip(
                            label = if (summary.hasInitBinary) "init (${summary.initElfArch})" else "init: missing",
                            ok = summary.hasInitBinary
                        )
                        StatusChip(
                            label = if (summary.hasGappsGms && summary.hasGappsPlayStore) "GApps + Play" else "GApps: missing",
                            ok = summary.hasGappsGms
                        )
                        StatusChip(
                            label = if (summary.hasContainerSu) "su: ready" else "su: missing",
                            ok = summary.hasContainerSu
                        )
                        StatusChip(
                            label = "${summary.totalFiles} files • ${summary.snapshotCount} snaps",
                            ok = summary.totalFiles > 0
                        )
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        if (isRunning) {
                            Button(
                                onClick = onStopVm,
                                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
                            ) {
                                Icon(Icons.Rounded.Stop, contentDescription = null, modifier = Modifier.size(16.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("Stop")
                            }
                            OutlinedButton(onClick = { onBootVm(vm) }) {
                                Text("Open Viewport")
                            }
                        } else {
                            Button(onClick = { onBootVm(vm) }) {
                                Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(16.dp))
                                Spacer(Modifier.width(4.dp))
                                Text(if (summary.isBootableAosp) "Boot AOSP" else "Launch Bridge")
                            }
                        }

                        OutlinedButton(onClick = {
                            importTargetVmId = vm.id
                            rootfsPicker.launch(arrayOf("*/*"))
                        }) {
                            Icon(Icons.Rounded.Download, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(4.dp))
                            Text("Import RootFS")
                        }

                        IconButton(onClick = { onSnapshotVm(vm) }) {
                            Icon(Icons.Rounded.CameraAlt, contentDescription = "Snapshot")
                        }
                        IconButton(onClick = { onRestoreSnapshot(vm) }) {
                            Icon(Icons.Rounded.Restore, contentDescription = "Restore")
                        }
                        IconButton(onClick = { onCloneVm(vm) }) {
                            Icon(Icons.Rounded.ContentCopy, contentDescription = "Clone")
                        }
                        IconButton(onClick = { onDeleteVm(vm) }) {
                            Icon(
                                Icons.Rounded.DeleteOutline,
                                contentDescription = "Delete",
                                tint = MaterialTheme.colorScheme.error
                            )
                        }
                    }
                }
            }
        }
    }

    if (showCreateDialog) {
        CreateVmDialog(
            onDismiss = { showCreateDialog = false },
            onConfirm = { name, gapps, su ->
                showCreateDialog = false
                onCreateVm(name, 34, gapps, su)
            }
        )
    }
}

@Composable
private fun HostCapabilityCard(
    probe: HostCapabilityReport,
    onCopyAdbCommand: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(
                    imageVector = if (probe.initBinaryExecutable) Icons.Rounded.CheckCircle else Icons.Rounded.Warning,
                    contentDescription = null,
                    tint = if (probe.initBinaryExecutable) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(18.dp)
                )
                Text("Host Non-Root Runtime Diagnostics", style = MaterialTheme.typography.titleMedium)
            }
            Text(
                text = "ABI=${probe.abi} • SDK=${probe.hostSdkInt} • UID=${probe.hostUid} • SELinux=${probe.selinuxMode}",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = "Kernel=${probe.kernelRelease} • memfd=${probe.memfdSupported} • W^X init exec=${probe.initBinaryExecutable} • /dev/kvm=${probe.kvmAccessible}",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (probe.phantomProcessKillerRisk) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "Android 12+ Phantom Process limit active: disable child-process restriction in Developer Options or copy ADB command for full GMS containers.",
                        modifier = Modifier.weight(1f),
                        fontSize = 11.5.sp,
                        color = MaterialTheme.colorScheme.error
                    )
                    IconButton(onClick = onCopyAdbCommand) {
                        Icon(Icons.Rounded.ContentCopy, contentDescription = "Copy ADB command")
                    }
                }
            }
        }
    }
}

@Composable
private fun StatusChip(label: String, ok: Boolean) {
    Surface(
        shape = RoundedCornerShape(6.dp),
        color = MaterialTheme.colorScheme.surfaceVariant,
        border = BorderStroke(
            1.dp,
            if (ok) MaterialTheme.colorScheme.primary.copy(alpha = 0.5f) else MaterialTheme.colorScheme.outline
        )
    ) {
        Text(
            text = label,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            color = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun CreateVmDialog(
    onDismiss: () -> Unit,
    onConfirm: (name: String, gapps: Boolean, su: Boolean) -> Unit
) {
    var name by remember { mutableStateOf("AOSP 14 Sandbox") }
    var gapps by remember { mutableStateOf(true) }
    var su by remember { mutableStateOf(true) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Create AOSP 14 Container") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("VM Label") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Target GApps + Play Store", fontSize = 13.sp)
                    Switch(checked = gapps, onCheckedChange = { gapps = it })
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Enable Container su", fontSize = 13.sp)
                    Switch(checked = su, onCheckedChange = { su = it })
                }
            }
        },
        confirmButton = {
            Button(onClick = { onConfirm(name, gapps, su) }) {
                Text("Create")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}
