package com.karyon.app

import com.karyon.app.nativebridge.KaryonParsers
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KaryonNativeParseTest {

    @Test
    fun parsesHostProbeCorrectly() {
        val raw = """
            uid=10241
            sdk=34
            kernel=6.1.57-android14
            abi=arm64-v8a
            selinux=enforcing
            memfd=1
            execmem=1
            file_exec_mmap=0
            kvm=0
            init_exec=1
            phantom_risk=1
            init_path=/data/app/lib/arm64/libkaryon_init.so
        """.trimIndent()
        val parsed = KaryonParsers.parseHostProbe(raw)
        assertEquals(10241, parsed.hostUid)
        assertEquals(34, parsed.hostSdkInt)
        assertEquals("arm64-v8a", parsed.abi)
        assertTrue(parsed.memfdSupported)
        assertTrue(parsed.execmemSupported)
        assertFalse(parsed.fileExecMmapSupported)
        assertFalse(parsed.kvmAccessible)
        assertTrue(parsed.initBinaryExecutable)
        assertTrue(parsed.phantomProcessKillerRisk)
    }

    @Test
    fun parsesRootfsInspectionAndStatusLogs() {
        val rootfs = KaryonParsers.parseRootfsInspection(
            """
            init=1
            init_arch=aarch64
            services=1
            framework=1
            gapps_gms=1
            gapps_store=1
            su=1
            bootable=1
            files=412
            bytes=1048576
            snapshots=2
            """.trimIndent()
        )
        assertTrue(rootfs.isBootableAosp)
        assertTrue(rootfs.hasGappsGms)
        assertTrue(rootfs.hasGappsPlayStore)
        assertTrue(rootfs.hasContainerSu)
        assertEquals("aarch64", rootfs.initElfArch)
        assertEquals(2, rootfs.snapshotCount)

        val (status, logs) = KaryonParsers.parseStatusAndLogs(
            """
            running=1
            pid=9876
            frames=14
            instance=aosp14_primary
            reply=ACK PING
            log=[child] KARYON_INIT: started
            log=[ctrl] READY pid=9876
            """.trimIndent()
        )
        assertTrue(status.running)
        assertEquals(9876, status.childPid)
        assertEquals(14L, status.framesRendered)
        assertEquals(2, logs.size)
    }
}
