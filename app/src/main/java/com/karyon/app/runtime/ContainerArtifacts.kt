package com.karyon.app.runtime

import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Paths
import java.util.zip.GZIPInputStream
import java.util.zip.ZipFile

data class TarExtractResult(
    val filesExtracted: Int,
    val dirsCreated: Int,
    val symlinksCreated: Int,
    val totalBytes: Long
)

data class ApkBinaryEntries(
    val manifestBytes: ByteArray?,
    val primaryDexBytes: ByteArray?
)

object ContainerArtifacts {

    fun sanitizeId(raw: String): String {
        val clean = raw.lowercase()
            .replace(Regex("[^a-z0-9_-]"), "_")
            .trim('_')
        return clean.take(40).ifEmpty { "vm_${System.currentTimeMillis() % 100000}" }
    }

    /**
     * Extracts a POSIX USTAR `.tar` or `.tar.gz` rootfs bundle safely into [targetDir],
     * enforcing strict canonical-path containment to prevent path traversal.
     */
    fun extractTarStream(rawInput: InputStream, targetDir: File): TarExtractResult {
        targetDir.mkdirs()
        val buffered = BufferedInputStream(rawInput, 65536)
        buffered.mark(2)
        val magic = ByteArray(2)
        val readMagic = buffered.read(magic)
        buffered.reset()

        val stream: InputStream = if (
            readMagic == 2 &&
            (magic[0].toInt() and 0xFF) == 0x1F &&
            (magic[1].toInt() and 0xFF) == 0x8B
        ) {
            GZIPInputStream(buffered)
        } else {
            buffered
        }

        val rootPath = targetDir.toPath().toAbsolutePath().normalize()
        val header = ByteArray(512)
        var longNameOverride: String? = null
        var longLinkOverride: String? = null
        var filesExtracted = 0
        var dirsCreated = 0
        var symlinksCreated = 0
        var totalBytes = 0L

        while (true) {
            if (!readFullyOrEof(stream, header)) break
            if (header.all { it.toInt() == 0 }) break

            val rawName = readTarCString(header, 0, 100)
            val prefix = readTarCString(header, 345, 155)
            val entryName = longNameOverride ?: if (prefix.isNotEmpty()) "$prefix/$rawName" else rawName
            val rawLink = readTarCString(header, 157, 100)
            val linkTarget = longLinkOverride ?: rawLink
            longNameOverride = null
            longLinkOverride = null

            val size = parseTarOctal(header, 124, 12)
            val mode = parseTarOctal(header, 100, 8).toInt()
            val typeFlag = header[156].toInt().toChar()

            if (typeFlag == 'L') {
                // GNU tar long file name metadata block
                val nameBytes = ByteArray(size.toInt())
                readFullyExact(stream, nameBytes)
                skipPadding(stream, size)
                longNameOverride = String(nameBytes, StandardCharsets.UTF_8).trimEnd('\u0000', '\n')
                continue
            }

            if (typeFlag == 'K') {
                // GNU tar long symlink target metadata block
                val linkBytes = ByteArray(size.toInt())
                readFullyExact(stream, linkBytes)
                skipPadding(stream, size)
                longLinkOverride = String(linkBytes, StandardCharsets.UTF_8).trimEnd('\u0000', '\n')
                continue
            }

            if (typeFlag == 'x' || typeFlag == 'g') {
                // Skip PAX extended header payload
                skipExact(stream, size)
                skipPadding(stream, size)
                continue
            }

            val normalizedRaw = entryName.replace('\\', '/')
            val segments = normalizedRaw.split('/').filter { it.isNotEmpty() && it != "." }
            val hasTraversal = segments.any { it == ".." }
            if (segments.isNotEmpty() && !hasTraversal) {
                val cleanRelative = segments.joinToString("/")
                val destFile = File(targetDir, cleanRelative)
                val destPath = destFile.toPath().toAbsolutePath().normalize()
                if (!destPath.startsWith(rootPath)) {
                    throw SecurityException("Blocked TAR path escape: $entryName")
                }

                // Ensure no existing ancestor inside targetDir is a symlink we would traverse through
                var walk = destPath.parent
                while (walk != null && walk != rootPath) {
                    if (Files.isSymbolicLink(walk)) {
                        throw SecurityException("Blocked write through TAR symlink ancestor: $entryName")
                    }
                    walk = walk.parent
                }

                when (typeFlag) {
                    '5' -> {
                        destFile.mkdirs()
                        dirsCreated++
                    }
                    '2' -> {
                        destFile.parentFile?.mkdirs()
                        Files.deleteIfExists(destPath)
                        if (linkTarget.isNotEmpty()) {
                            runCatching {
                                Files.createSymbolicLink(destPath, Paths.get(linkTarget))
                                symlinksCreated++
                            }
                        }
                    }
                    '0', '\u0000' -> {
                        destFile.parentFile?.mkdirs()
                        Files.deleteIfExists(destPath)
                        FileOutputStream(destFile).use { fos ->
                            copyExact(stream, fos, size)
                        }
                        if ((mode and 0x49) != 0) {
                            destFile.setExecutable(true, false)
                        }
                        filesExtracted++
                        totalBytes += size
                        skipPadding(stream, size)
                        continue
                    }
                    else -> {
                        skipExact(stream, size)
                        skipPadding(stream, size)
                        continue
                    }
                }
            } else {
                skipExact(stream, size)
                skipPadding(stream, size)
                continue
            }

            skipExact(stream, size)
            skipPadding(stream, size)
        }

        return TarExtractResult(filesExtracted, dirsCreated, symlinksCreated, totalBytes)
    }

    fun readApkEntries(apkFile: File): ApkBinaryEntries {
        ZipFile(apkFile).use { zip ->
            val manifest = zip.getEntry("AndroidManifest.xml")?.let { e ->
                zip.getInputStream(e).use { it.readBytes() }
            }
            val dex = zip.getEntry("classes.dex")?.let { e ->
                zip.getInputStream(e).use { it.readBytes() }
            }
            return ApkBinaryEntries(manifest, dex)
        }
    }

    private fun readFullyOrEof(input: InputStream, dst: ByteArray): Boolean {
        var off = 0
        while (off < dst.size) {
            val n = input.read(dst, off, dst.size - off)
            if (n < 0) return false
            off += n
        }
        return true
    }

    private fun readFullyExact(input: InputStream, dst: ByteArray) {
        var off = 0
        while (off < dst.size) {
            val n = input.read(dst, off, dst.size - off)
            if (n < 0) throw IllegalStateException("Unexpected EOF reading TAR stream")
            off += n
        }
    }

    private fun copyExact(input: InputStream, output: FileOutputStream, size: Long) {
        val buf = ByteArray(16384)
        var remaining = size
        while (remaining > 0) {
            val toRead = minOf(buf.size.toLong(), remaining).toInt()
            val n = input.read(buf, 0, toRead)
            if (n < 0) throw IllegalStateException("Unexpected EOF inside TAR file payload")
            output.write(buf, 0, n)
            remaining -= n
        }
    }

    private fun skipExact(input: InputStream, size: Long) {
        var remaining = size
        val buf = ByteArray(8192)
        while (remaining > 0) {
            val n = input.read(buf, 0, minOf(buf.size.toLong(), remaining).toInt())
            if (n < 0) break
            remaining -= n
        }
    }

    private fun skipPadding(input: InputStream, size: Long) {
        val rem = (size % 512L)
        if (rem != 0L) {
            skipExact(input, 512L - rem)
        }
    }

    private fun readTarCString(buf: ByteArray, offset: Int, maxLen: Int): String {
        var end = offset
        val limit = offset + maxLen
        while (end < limit && buf[end].toInt() != 0) end++
        return String(buf, offset, end - offset, StandardCharsets.UTF_8)
    }

    private fun parseTarOctal(buf: ByteArray, offset: Int, len: Int): Long {
        var result = 0L
        val end = offset + len
        for (i in offset until end) {
            val b = buf[i].toInt() and 0xFF
            if (b == 0 || b == ' '.code) continue
            if (b in '0'.code..'7'.code) {
                result = (result shl 3) or (b - '0'.code).toLong()
            }
        }
        return result
    }
}
