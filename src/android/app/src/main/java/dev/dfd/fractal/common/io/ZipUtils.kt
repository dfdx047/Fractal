package dev.dfd.fractal.common.io

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.nio.file.Paths
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

fun unzip(stream: InputStream, targetDir: String) {
    ZipInputStream(stream).use { zipInputStream ->
        val buffer = ByteArray(8192)

        var zipEntry: ZipEntry? = zipInputStream.nextEntry

        while (zipEntry != null) {
            extractZipEntry(zipInputStream, zipEntry, buffer, targetDir)
            zipEntry = zipInputStream.nextEntry
        }
    }
}

/** Every regular file under [dir] as (zip entry name, file): names relative to [dir], "/" separated, after [prefix]. */
fun filesForZip(dir: File, prefix: String = ""): List<Pair<String, File>> {
    if (!dir.isDirectory) return emptyList()
    return dir.walkTopDown()
        .filter { it.isFile }
        .map { prefix + it.relativeTo(dir).invariantSeparatorsPath to it }
        .sortedBy { it.first }
        .toList()
}

/**
 * Writes [entries] (zip entry name to file) and the small [textEntries] (name to content) as a zip
 * into [output], which is closed afterwards. [onProgress] gets the number of files written so far.
 */
fun zipFiles(
    output: OutputStream,
    entries: List<Pair<String, File>>,
    textEntries: Map<String, String> = emptyMap(),
    onProgress: (Int) -> Unit = {},
) {
    ZipOutputStream(output.buffered()).use { zip ->
        for ((name, text) in textEntries) {
            zip.putNextEntry(ZipEntry(name))
            zip.write(text.encodeToByteArray())
            zip.closeEntry()
        }
        entries.forEachIndexed { index, (name, file) ->
            zip.putNextEntry(ZipEntry(name).apply { time = file.lastModified() })
            file.inputStream().use { it.copyTo(zip) }
            zip.closeEntry()
            onProgress(index + 1)
        }
    }
}

/** Throws if [name] would land outside [root] once resolved (zip slip). */
fun safeZipTarget(root: File, name: String): File {
    val canonicalRoot = root.canonicalFile
    val file = File(canonicalRoot, name).canonicalFile
    if (file != canonicalRoot && !file.path.startsWith(canonicalRoot.path + File.separator))
        throw java.io.IOException("Zip entry outside of the target directory: $name")
    return file
}

private fun extractZipEntry(
    zipInputStream: ZipInputStream,
    zipEntry: ZipEntry,
    buffer: ByteArray,
    targetDir: String,
) {
    val root = Paths.get(targetDir).toFile().canonicalFile
    val file = Paths.get(targetDir, zipEntry.name).toFile().canonicalFile
    // zip slip: entries like "../x" must not escape the target directory (driver zips come from the internet)
    if (file != root && !file.path.startsWith(root.path + java.io.File.separator))
        throw java.io.IOException("Zip entry outside of the target directory: ${zipEntry.name}")
    if (zipEntry.isDirectory) {
        file.apply { if (!isDirectory) mkdirs() }
        return
    }
    file.parentFile?.mkdirs()
    FileOutputStream(file).use { fileOutputStream ->
        var bytesRead: Int
        while ((zipInputStream.read(buffer).also { bytesRead = it }) > 0) {
            fileOutputStream.write(buffer, 0, bytesRead)
        }
    }
}
