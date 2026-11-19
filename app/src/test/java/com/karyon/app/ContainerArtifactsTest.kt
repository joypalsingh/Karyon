package com.karyon.app.runtime

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.charset.StandardCharsets
import java.util.zip.GZIPOutputStream

class ContainerArtifactsTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun sanitizeIdNormalizesSpecialCharacters() {
        assertEquals("aosp_14_test", ContainerArtifacts.sanitizeId("AOSP 14 Test!!"))
        assertEquals("my-vm_1", ContainerArtifacts.sanitizeId("my-vm_1"))
    }

    @Test
    fun extractsUstarTarAndGzipArchivesCleanly() {
        val tarBytes = buildTar(
            listOf(
                TarSpec("system/", null, true),
                TarSpec("system/build.prop", "ro.build.version.sdk=34\n".toByteArray(StandardCharsets.UTF_8), false),
                TarSpec("system/xbin/su", "ELF".toByteArray(StandardCharsets.UTF_8), false)
            )
        )

        val outPlain = tempFolder.newFolder("plain_out")
        val resPlain = ContainerArtifacts.extractTarStream(ByteArrayInputStream(tarBytes), outPlain)
        assertEquals(2, resPlain.filesExtracted)
        assertEquals(1, resPlain.dirsCreated)
        assertEquals("ro.build.version.sdk=34\n", File(outPlain, "system/build.prop").readText())

        val gzOut = ByteArrayOutputStream()
        GZIPOutputStream(gzOut).use { it.write(tarBytes) }
        val outGz = tempFolder.newFolder("gz_out")
        val resGz = ContainerArtifacts.extractTarStream(ByteArrayInputStream(gzOut.toByteArray()), outGz)
        assertEquals(2, resGz.filesExtracted)
        assertTrue(File(outGz, "system/xbin/su").exists())
    }

    @Test
    fun skipsTraversalTarEntriesSafely() {
        val tarBytes = buildTar(
            listOf(
                TarSpec("../../outside.txt", "bad".toByteArray(StandardCharsets.UTF_8), false),
                TarSpec("system/ok.txt", "good".toByteArray(StandardCharsets.UTF_8), false)
            )
        )
        val out = tempFolder.newFolder("safe_out")
        val res = ContainerArtifacts.extractTarStream(ByteArrayInputStream(tarBytes), out)
        assertEquals(1, res.filesExtracted)
        assertTrue(File(out, "system/ok.txt").exists())
        assertTrue(!File(tempFolder.root, "outside.txt").exists())
    }

    private data class TarSpec(val name: String, val data: ByteArray?, val isDir: Boolean)

    private fun buildTar(entries: List<TarSpec>): ByteArray {
        val out = ByteArrayOutputStream()
        for (entry in entries) {
            val hdr = ByteArray(512)
            val nameBytes = entry.name.toByteArray(StandardCharsets.UTF_8)
            System.arraycopy(nameBytes, 0, hdr, 0, minOf(nameBytes.size, 100))

            writeOctal(hdr, 100, 8, if (entry.isDir) 493 else 420)
            writeOctal(hdr, 108, 8, 0)
            writeOctal(hdr, 116, 8, 0)
            val payload = entry.data ?: ByteArray(0)
            writeOctal(hdr, 124, 12, payload.size.toLong())
            writeOctal(hdr, 136, 12, 1700000000L)
            for (i in 148 until 156) hdr[i] = ' '.code.toByte()
            hdr[156] = if (entry.isDir) '5'.code.toByte() else '0'.code.toByte()
            "ustar".toByteArray(StandardCharsets.US_ASCII).copyInto(hdr, 257)

            val sum = hdr.sumOf { it.toInt() and 0xFF }
            writeOctal(hdr, 148, 7, sum.toLong())
            hdr[155] = ' '.code.toByte()

            out.write(hdr)
            if (!entry.isDir && payload.isNotEmpty()) {
                out.write(payload)
                val rem = payload.size % 512
                if (rem != 0) {
                    out.write(ByteArray(512 - rem))
                }
            }
        }
        out.write(ByteArray(1024))
        return out.toByteArray()
    }

    private fun writeOctal(dst: ByteArray, offset: Int, width: Int, value: Long) {
        val oct = java.lang.Long.toOctalString(value).padStart(width - 1, '0')
        val bytes = oct.toByteArray(StandardCharsets.US_ASCII)
        val start = maxOf(0, bytes.size - (width - 1))
        bytes.copyInto(dst, offset, start, bytes.size)
        dst[offset + width - 1] = 0
    }
}
