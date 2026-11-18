package dev.dfd.fractal.graphicpacks

import dev.dfd.fractal.BuildConfig
import dev.dfd.fractal.common.io.unzip
import dev.dfd.fractal.nativeinterface.NativeGraphicPacks
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import kotlin.io.path.div
import kotlin.io.path.readText
import kotlin.time.Clock

enum class GraphicPacksDownloadStatus {
    CHECKING_VERSION,
    NO_UPDATES_AVAILABLE,
    DOWNLOADING,
    EXTRACTING,
    FINISHED_DOWNLOADING,
    ERROR,
}

@Serializable
data class Release(
    val name: String,
    val assets: List<Asset>
)

@Serializable
data class Asset(
    @SerialName("browser_download_url")
    val browserDownloadUrl: String
)

class GraphicPacksDownloader {
    private val json = Json {
        ignoreUnknownKeys = true
    }

    private class Response(val code: Int, val body: ByteArray) {
        val isSuccess get() = code in 200..299
        fun text() = body.decodeToString()
    }

    /** GET [url]; the body of an error status is read too. */
    private suspend fun get(url: String): Response = withContext(Dispatchers.IO) {
        val connection = (URL(url).openConnection() as HttpURLConnection).apply {
            instanceFollowRedirects = true
            setRequestProperty("User-Agent", "Fractal-emulator")
        }
        try {
            val code = connection.responseCode
            val stream = if (code in 200..299) connection.inputStream else connection.errorStream
            Response(code, stream?.use { it.readBytes() } ?: ByteArray(0))
        } finally {
            connection.disconnect()
        }
    }

    private fun getCurrentVersion(graphicPacksDir: File): String? {
        val graphicPacksVersionFile =
            graphicPacksDir.toPath() / "downloadedGraphicPacks" / "version.txt"
        return try {
            graphicPacksVersionFile.readText()
        } catch (_: IOException) {
            null
        }
    }

    private suspend fun getUpdateUrl(): String {
        val queryUrl = "https://cemu.info/api2/query_graphicpack_url.php?" +
                "version=${BuildConfig.VERSION_NAME}" +
                "&t=${Clock.System.now().toEpochMilliseconds()}"

        val response = get(queryUrl)
        val body = response.text().trim()

        if (response.isSuccess && body.startsWith("http")) {
            return body
        }

        return "https://api.github.com/repos/cemu-project/cemu_graphic_packs/releases/latest"
    }

    private suspend fun fetchLatestRelease(): Release? {
        val response = get(getUpdateUrl())

        if (!response.isSuccess) {
            return null
        }

        return try {
            json.decodeFromString<Release>(response.text())
        } catch (_: Exception) {
            null
        }
    }

    private fun isUpToDate(dir: File, newVersion: String): Boolean {
        return getCurrentVersion(dir) == newVersion
    }

    private suspend fun downloadZip(url: String): ByteArray? {
        val response = get(url)

        if (!response.isSuccess) {
            return null
        }

        return response.body
    }

    private suspend fun extractAndInstall(
        zipBytes: ByteArray,
        graphicPacksDir: File,
        version: String
    ) = withContext(Dispatchers.IO) {

        val tempDir = graphicPacksDir.resolve("downloadedGraphicPacksTemp")
        val finalDir = graphicPacksDir.resolve("downloadedGraphicPacks")

        tempDir.deleteRecursively()

        unzip(zipBytes.inputStream(), tempDir.path)

        tempDir.resolve("version.txt").writeText(version)

        finalDir.deleteRecursively()
        tempDir.renameTo(finalDir)

        NativeGraphicPacks.refreshGraphicPacks()
    }

    fun download(graphicPacksRootDir: File): Flow<GraphicPacksDownloadStatus> = flow {
        val graphicPacksDir = graphicPacksRootDir.resolve("graphicPacks")

        emit(GraphicPacksDownloadStatus.CHECKING_VERSION)

        val release = fetchLatestRelease() ?: run {
            emit(GraphicPacksDownloadStatus.ERROR)
            return@flow
        }

        val version = release.name

        if (isUpToDate(graphicPacksDir, version)) {
            emit(GraphicPacksDownloadStatus.NO_UPDATES_AVAILABLE)
            return@flow
        }

        val downloadUrl = release.assets.firstOrNull()?.browserDownloadUrl ?: run {
            emit(GraphicPacksDownloadStatus.ERROR)
            return@flow
        }

        emit(GraphicPacksDownloadStatus.DOWNLOADING)

        val zipBytes = downloadZip(downloadUrl) ?: run {
            emit(GraphicPacksDownloadStatus.ERROR)
            return@flow
        }

        emit(GraphicPacksDownloadStatus.EXTRACTING)

        extractAndInstall(zipBytes, graphicPacksDir, version)

        emit(GraphicPacksDownloadStatus.FINISHED_DOWNLOADING)
    }
}
