package com.norsehorse.scrubpony

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.MediaStore
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import java.io.File

/** Gets scrubbed copies out of the app's cache and onto the device: either
 *  into the gallery, or back out through the share sheet. Never touches the
 *  original files the user picked or shared in. */
object SaveExporter {

    /** The output files this app ever produces are named by ScrubEngine
     *  after the format the native scrubber actually detected (see
     *  ScrubEngine.scrubOne), so the extension is trustworthy here in a way
     *  an arbitrary input's extension would not be. */
    private fun mimeTypeFor(file: File): String =
        when (file.extension.lowercase()) {
            "png" -> "image/png"
            "webp" -> "image/webp"
            "heic", "heif" -> "image/heic"
            else -> "image/jpeg"
        }

    /** Copies [file] into the Pictures/ScrubPony collection via MediaStore.
     *  Needs no storage permission — minSdk 29 means every device here uses
     *  scoped storage, where an app can always write into MediaStore
     *  collections it created entries in. */
    fun saveToGallery(context: Context, file: File, displayName: String): Uri? {
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
            put(MediaStore.Images.Media.MIME_TYPE, mimeTypeFor(file))
            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/ScrubPony")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
        val resolver = context.contentResolver
        val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) ?: return null

        val wrote = resolver.openOutputStream(uri)?.use { out ->
            file.inputStream().use { it.copyTo(out) }
        }
        if (wrote == null) {
            resolver.delete(uri, null, null)
            return null
        }

        values.clear()
        values.put(MediaStore.Images.Media.IS_PENDING, 0)
        resolver.update(uri, values, null, null)
        return uri
    }

    /** Copies [files] into a folder the user picked themselves through the
     *  system's folder picker (Storage Access Framework), as opposed to
     *  [saveToGallery]'s fixed Pictures/ScrubPony collection. [treeUri] is
     *  whatever ActivityResultContracts.OpenDocumentTree() returned. Returns
     *  how many files copied successfully, so the caller can tell a partial
     *  failure from a clean run. */
    fun saveToTree(context: Context, treeUri: Uri, files: List<File>): Int {
        val treeDoc = DocumentFile.fromTreeUri(context, treeUri) ?: return 0
        var copied = 0
        for (file in files) {
            val doc = treeDoc.createFile(mimeTypeFor(file), file.name) ?: continue
            val wrote = context.contentResolver.openOutputStream(doc.uri)?.use { out ->
                file.inputStream().use { it.copyTo(out) }
            }
            if (wrote != null) {
                copied++
            } else {
                doc.delete()
            }
        }
        return copied
    }

    private fun shareUriFor(context: Context, file: File): Uri =
        FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)

    /** Builds a SEND / SEND_MULTIPLE intent carrying [files] as FileProvider
     *  content:// URIs, ready to hand to Intent.createChooser. */
    fun buildShareIntent(context: Context, files: List<File>): Intent {
        val uris = ArrayList(files.map { shareUriFor(context, it) })
        // A single file gets its real MIME type; a mixed JPEG+PNG batch has
        // no single correct type, so it falls back to the generic image/*
        // ACTION_SEND_MULTIPLE is documented to accept.
        val types = files.map { mimeTypeFor(it) }.distinct()
        return Intent().apply {
            action = if (uris.size == 1) Intent.ACTION_SEND else Intent.ACTION_SEND_MULTIPLE
            type = types.singleOrNull() ?: "image/*"
            if (uris.size == 1) {
                putExtra(Intent.EXTRA_STREAM, uris.first())
            } else {
                putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
            }
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }
}
