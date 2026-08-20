package com.norsehorse.scrubpony

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.UUID

enum class FileOutcome { SCRUBBED, ALREADY_CLEAN, UNSUPPORTED, FAILED }

data class ScrubItemResult(
    val displayName: String,
    val outcome: FileOutcome,
    val message: String,
    val outputFile: File?,
    val bytesRemoved: Long,
    val hasGps: Boolean,
    val orientationKept: Boolean,
    val metadata: List<MetaField> = emptyList(),
)

data class BatchSummary(val results: List<ScrubItemResult>) {
    val scrubbed get() = results.count { it.outcome == FileOutcome.SCRUBBED }
    val alreadyClean get() = results.count { it.outcome == FileOutcome.ALREADY_CLEAN }
    val unsupported get() = results.count { it.outcome == FileOutcome.UNSUPPORTED }
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

        // Read the original's metadata before the core strips it, so the result
        // can show what was removed. Read-only and best-effort.
        val meta = MetadataReader.read(stagedInput)

        val baseName = displayName.substringBeforeLast('.', displayName).ifBlank { "photo" }
        val suffix = UUID.randomUUID().toString().take(8)
        // The extension has to be a guess before the native side has looked
        // at the bytes — it renames the output below once the real format is
        // known, rather than trusting the input's claimed extension the way
        // the desktop CLI's probe deliberately never does either.
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
            val unrecognisedFormat = stats.status == NativeScrubber.SP_ERR_NOT_JPEG ||
                stats.status == NativeScrubber.SP_ERR_NOT_PNG ||
                stats.status == NativeScrubber.SP_ERR_NOT_WEBP ||
                stats.status == NativeScrubber.SP_ERR_NOT_HEIF
            // SP_ERR_UNSUPPORTED is different: the format WAS recognised (a
            // valid HEIC), but its layout is one the rewriter will not touch,
            // so the original is left exactly as it was.
            val unsupportedLayout = stats.status == NativeScrubber.SP_ERR_UNSUPPORTED
            val skipped = unrecognisedFormat || unsupportedLayout
            val outcome = if (skipped) FileOutcome.UNSUPPORTED else FileOutcome.FAILED
            val message = when {
                unrecognisedFormat -> "not a JPEG, PNG, WebP, or HEIC, skipped"
                unsupportedLayout -> "unsupported HEIC layout, left unchanged"
                else -> NativeScrubber.statusMessage(stats.status)
            }
            return ScrubItemResult(displayName, outcome, message, null, 0, false, false)
        }

        // Now that the native side knows what it actually wrote, rename the
        // output to match — a PNG named "photo-scrubbed-xxxx.jpg" would still
        // decode fine (the bytes are what they are), but it is the kind of
        // mismatch that confuses a gallery app or a person double-checking
        // the file later.
        val ext = when (stats.format) {
            ImageFormat.PNG -> "png"
            ImageFormat.WEBP -> "webp"
            ImageFormat.HEIC -> "heic"
            ImageFormat.JPEG -> null
        }
        val finalFile = if (ext != null) {
            File(outputDir, "$baseName-scrubbed-$suffix.$ext").also { outputFile.renameTo(it) }
        } else {
            outputFile
        }
        val unit = when (stats.format) {
            ImageFormat.JPEG -> "segment"
            ImageFormat.HEIC -> "item"
            else -> "chunk"
        }

        val removed = (stats.inSize - stats.outSize).coerceAtLeast(0)
        val outcome = if (stats.dropped == 0L) FileOutcome.ALREADY_CLEAN else FileOutcome.SCRUBBED
        val message = if (outcome == FileOutcome.ALREADY_CLEAN) {
            "already clean"
        } else {
            "removed ${stats.dropped} $unit${if (stats.dropped == 1L) "" else "s"}, $removed bytes"
        }

        return ScrubItemResult(displayName, outcome, message, finalFile, removed, stats.hasGps, stats.orientationKept, metadata = meta)
    }

    private fun queryDisplayName(resolver: ContentResolver, uri: Uri): String? {
        if (uri.scheme == "file") return uri.path?.let { File(it).name }
        return resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && cursor.moveToFirst()) cursor.getString(idx) else null
        }
    }
}
