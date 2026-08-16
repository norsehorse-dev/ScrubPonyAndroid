package com.norsehorse.scrubpony

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.UUID

enum class FileOutcome { SCRUBBED, ALREADY_CLEAN, NOT_JPEG, FAILED }

data class ScrubItemResult(
    val displayName: String,
    val outcome: FileOutcome,
    val message: String,
    val outputFile: File?,
    val bytesRemoved: Long,
    val hasGps: Boolean,
    val orientationKept: Boolean,
)

data class BatchSummary(val results: List<ScrubItemResult>) {
    val scrubbed get() = results.count { it.outcome == FileOutcome.SCRUBBED }
    val alreadyClean get() = results.count { it.outcome == FileOutcome.ALREADY_CLEAN }
    val notJpeg get() = results.count { it.outcome == FileOutcome.NOT_JPEG }
    val failed get() = results.count { it.outcome == FileOutcome.FAILED }
    val bytesRemoved get() = results.sumOf { it.bytesRemoved }
    val withGps get() = results.count { it.hasGps }
    val orientationPreserved get() = results.count { it.orientationKept }
}

/**
 * Stages content:// input into real files, calls the native scrubber, and
 * tallies results the same way the desktop CLI's summary line does (see
 * summarise() in ScrubPonyDesktop/src/main.c).
 *
 * The C core writes through a temp-file-then-rename, exactly like the
 * desktop tool — that guarantee is preserved here because it runs against
 * this app's private, real POSIX filesystem (cacheDir), not a content://
 * URI. Nothing is ever written back over the caller's original.
 */
class ScrubEngine(private val context: Context) {

    private val inputDir = File(context.cacheDir, "inputs").apply { mkdirs() }
    private val outputDir = File(context.cacheDir, "outputs").apply { mkdirs() }

    /** Clears staged input and previously produced output files. Call this
     *  when starting a new batch so old scrubbed copies don't accumulate in
     *  the cache indefinitely. */
    fun clearWorkingFiles() {
        inputDir.listFiles()?.forEach { it.delete() }
        outputDir.listFiles()?.forEach { it.delete() }
    }

    fun scrubOne(uri: Uri, strict: Boolean, keepOrientation: Boolean): ScrubItemResult {
        val resolver = context.contentResolver
        val displayName = queryDisplayName(resolver, uri) ?: "image-${UUID.randomUUID()}.jpg"
        val stagedInput = File(inputDir, "${UUID.randomUUID()}-$displayName")

        val staged = try {
            resolver.openInputStream(uri)?.use { input ->
                stagedInput.outputStream().use { output -> input.copyTo(output) }
            }
            true
        } catch (e: Exception) {
            stagedInput.delete()
            return ScrubItemResult(displayName, FileOutcome.FAILED, e.message ?: "read failed", null, 0, false, false)
        }

        if (!staged) {
            return ScrubItemResult(displayName, FileOutcome.FAILED, "could not open input", null, 0, false, false)
        }

        val baseName = displayName.substringBeforeLast('.', displayName).ifBlank { "photo" }
        val suffix = UUID.randomUUID().toString().take(8)
        val outputFile = File(outputDir, "$baseName-scrubbed-$suffix.jpg")

        val raw = NativeScrubber.scrubFile(
            stagedInput.absolutePath,
            outputFile.absolutePath,
            strict,
            /* noOrientation = */ !keepOrientation,
        )
        val stats = decodeScrubStats(raw)
        stagedInput.delete()

        if (stats.status != NativeScrubber.SP_OK) {
            outputFile.delete()
            val outcome = if (stats.status == NativeScrubber.SP_ERR_NOT_JPEG) {
                FileOutcome.NOT_JPEG
            } else {
                FileOutcome.FAILED
            }
            val message = if (stats.status == NativeScrubber.SP_ERR_NOT_JPEG) {
                "not a JPEG, skipped"
            } else {
                NativeScrubber.statusMessage(stats.status)
            }
            return ScrubItemResult(displayName, outcome, message, null, 0, false, false)
        }

        val removed = (stats.inSize - stats.outSize).coerceAtLeast(0)
        val outcome = if (stats.dropped == 0L) FileOutcome.ALREADY_CLEAN else FileOutcome.SCRUBBED
        val message = if (outcome == FileOutcome.ALREADY_CLEAN) {
            "already clean"
        } else {
            "removed ${stats.dropped} segment${if (stats.dropped == 1L) "" else "s"}, $removed bytes"
        }

        return ScrubItemResult(displayName, outcome, message, outputFile, removed, stats.hasGps, stats.orientationKept)
    }

    private fun queryDisplayName(resolver: ContentResolver, uri: Uri): String? {
        if (uri.scheme == "file") return uri.path?.let { File(it).name }
        return resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && cursor.moveToFirst()) cursor.getString(idx) else null
        }
    }
}
