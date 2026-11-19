package com.karyon.app.ui

import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.Apps
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Dns
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.Terminal
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface as ComposeSurface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.karyon.app.model.ShellLine
import com.karyon.app.model.VmInstance
import com.karyon.app.model.VmLifecycleState
import com.karyon.app.model.VmRuntimeStatus

private enum class ViewportDrawer {
    NONE,
    SHELL,
    LOGS
}

@Composable
fun ViewportScreen(
    vmInstance: VmInstance?,
    lifecycleState: VmLifecycleState,
    runtimeStatus: VmRuntimeStatus,
    brokerLogs: List<String>,
    shellHistory: List<ShellLine>,
    onBackToDashboard: () -> Unit,
    onBootVm: (VmInstance) -> Unit,
    onStopVm: () -> Unit,
    onSendCommand: (String) -> Unit,
    onAttachSurface: (Surface) -> Unit,
    onDetachSurface: () -> Unit,
    onRunShellCommand: (String) -> Unit
) {
    var activeDrawer by remember { mutableStateOf(ViewportDrawer.NONE) }
    var commandInput by remember { mutableStateOf("") }

    DisposableEffect(Unit) {
        onDispose { onDetachSurface() }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        // Top HUD Strip
        ComposeSurface(
            color = MaterialTheme.colorScheme.surface,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                IconButton(onClick = onBackToDashboard) {
                    Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back")
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = vmInstance?.name ?: "No VM Selected",
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = if (runtimeStatus.running) {
                            "${lifecycleState.name} • PID ${runtimeStatus.childPid} • ${runtimeStatus.framesRendered} frames"
                        } else {
                            "STOPPED • ${lifecycleState.name}"
                        },
                        style = MaterialTheme.typography.labelMedium,
                        color = if (runtimeStatus.running) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                IconButton(onClick = {
                    activeDrawer = if (activeDrawer == ViewportDrawer.SHELL) ViewportDrawer.NONE else ViewportDrawer.SHELL
                }) {
                    Icon(Icons.Rounded.Terminal, contentDescription = "Container Shell")
                }
                IconButton(onClick = {
                    activeDrawer = if (activeDrawer == ViewportDrawer.LOGS) ViewportDrawer.NONE else ViewportDrawer.LOGS
                }) {
                    Icon(Icons.Rounded.Dns, contentDescription = "Broker Logs")
                }
                if (runtimeStatus.running) {
                    IconButton(onClick = onStopVm) {
                        Icon(Icons.Rounded.Stop, contentDescription = "Stop VM", tint = MaterialTheme.colorScheme.error)
                    }
                } else if (vmInstance != null) {
                    Button(
                        onClick = { onBootVm(vmInstance) },
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Start")
                    }
                }
            }
        }

        // Main Display Surface + Drawers
        Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { ctx ->
                    SurfaceView(ctx).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                onAttachSurface(holder.surface)
                            }

                            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                                onAttachSurface(holder.surface)
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                onDetachSurface()
                            }
                        })
                    }
                }
            )

            if (!runtimeStatus.running) {
                ComposeSurface(
                    modifier = Modifier
                        .align(Alignment.Center)
                        .padding(24.dp),
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.94f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)
                ) {
                    Column(
                        modifier = Modifier.padding(20.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Text("Container Supervisor Idle", style = MaterialTheme.typography.titleMedium)
                        Text(
                            text = "Start the VM supervisor to spawn libkaryon_init.so, verify AOSP rootfs components, and stream live frames into ANativeWindow.",
                            fontSize = 12.5.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        if (vmInstance != null) {
                            Button(onClick = { onBootVm(vmInstance) }) {
                                Text("Launch Supervisor")
                            }
                        }
                    }
                }
            }

            if (activeDrawer != ViewportDrawer.NONE) {
                ComposeSurface(
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .height(300.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)
                ) {
                    Column(modifier = Modifier.fillMaxSize()) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 12.dp, vertical = 6.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = if (activeDrawer == ViewportDrawer.SHELL) "Container Shell / Diagnostics" else "Supervisor & AF_UNIX Broker Logs",
                                style = MaterialTheme.typography.titleMedium
                            )
                            IconButton(onClick = { activeDrawer = ViewportDrawer.NONE }) {
                                Icon(Icons.Rounded.Close, contentDescription = "Close Drawer")
                            }
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

                        if (activeDrawer == ViewportDrawer.SHELL) {
                            val shellListState = rememberLazyListState()
                            LaunchedEffect(shellHistory.size) {
                                if (shellHistory.isNotEmpty()) {
                                    shellListState.animateScrollToItem(shellHistory.lastIndex)
                                }
                            }
                            val submitCmd = {
                                if (commandInput.isNotBlank()) {
                                    onRunShellCommand(commandInput)
                                    commandInput = ""
                                }
                            }
                            LazyColumn(
                                state = shellListState,
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxWidth()
                                    .padding(horizontal = 12.dp, vertical = 6.dp),
                                verticalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                items(shellHistory) { item ->
                                    Text(
                                        text = item.text,
                                        fontFamily = FontFamily.Monospace,
                                        fontSize = 11.5.sp,
                                        color = if (item.isCommand) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                                    )
                                }
                            }
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(8.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                OutlinedTextField(
                                    value = commandInput,
                                    onValueChange = { commandInput = it },
                                    placeholder = { Text("help, services, elf-exec system/bin/toybox uname -a") },
                                    singleLine = true,
                                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                                    keyboardActions = KeyboardActions(onSend = { submitCmd() }),
                                    modifier = Modifier.weight(1f)
                                )
                                IconButton(onClick = submitCmd) {
                                    Icon(Icons.AutoMirrored.Rounded.Send, contentDescription = "Run")
                                }
                            }
                        } else {
                            LazyColumn(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .padding(12.dp),
                                verticalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                items(brokerLogs) { line ->
                                    Text(
                                        text = line,
                                        fontFamily = FontFamily.Monospace,
                                        fontSize = 11.sp,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }

        // Bottom Virtual Nav Strip
        ComposeSurface(
            color = MaterialTheme.colorScheme.surface,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedButton(onClick = { onSendCommand("KEY:BACK") }, enabled = runtimeStatus.running) {
                    Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back", modifier = Modifier.size(16.dp))
                }
                OutlinedButton(onClick = { onSendCommand("KEY:HOME") }, enabled = runtimeStatus.running) {
                    Icon(Icons.Rounded.Home, contentDescription = "Home", modifier = Modifier.size(16.dp))
                }
                OutlinedButton(onClick = { onSendCommand("KEY:RECENTS") }, enabled = runtimeStatus.running) {
                    Icon(Icons.Rounded.Apps, contentDescription = "Recents", modifier = Modifier.size(16.dp))
                }
                Button(
                    onClick = { onSendCommand("PING") },
                    enabled = runtimeStatus.running,
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Text(
                        "Ping",
                        color = if (runtimeStatus.running) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}
