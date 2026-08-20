package com.norsehorse.scrubpony

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * One image found under a picked folder tree. The parent is kept so an in-place
 * replace can create its temporary file in the same directory before swapping.
 */
data class TreeImage(
    val parent: DocumentFile,
    val file: DocumentFile,
    val name: String,
    val mime: String,
)

/**
 * Folder-tree scanning and writing for the bulk clean, all through the Storage
 * Access Framework grant the user hands over via OpenDocumentTree. No broad
 * storage permission is involved: the app only ever touches the one folder the
 * user pointed it at, and only for the length of this operation.
 */
object BulkFiles {

    const val CLEANED_DIR = "ScrubPony cleaned"

    private val IMAGE_EXTS = setOf("jpg", "jpeg", "png", "webp", "heic", "heif")

    private fun looksLikeImage(name: String?, type: String?): Boolean {
        if (type != null && type.startsWith("image/")) return true
        val ext = name?.substringAfterLast('.', "")?.lowercase()
        return ext != null && ext in IMAGE_EXTS
    }

    private fun mimeFor(name: String): String =
        when (name.substringAfterLast('.', "").lowercase()) {
            "png" -> "image/png"
            "webp" -> "image/webp"
            "heic", "heif" -> "image/heic"
            else -> "image/jpeg"
        }

    /**
     * Walks the tree depth-first and returns every image under it, up to [cap] so
     * a very large tree cannot make the scan run away. Skips the app's own
     * "ScrubPony cleaned" output folder so a re-scan does not pick up copies it
     * just wrote.
     */
    fun listImages(context: Context, treeUri: Uri, cap: Int = 4000): List<TreeImage> {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: return emptyList()
        val out = ArrayList<TreeImage>()
        val stack = ArrayDeque<DocumentFile>()
        stack.addLast(root)
        while (stack.isNotEmpty() && out.size < cap) {
            val dir = stack.removeLast()
            for (child in dir.listFiles()) {
                if (out.size >= cap) break
                if (child.isDirectory) {
                    if (child.name != CLEANED_DIR) stack.addLast(child)
                } else {
                    val name = child.name ?: continue
                    if (looksLikeImage(name, child.type)) {
                        out.add(TreeImage(dir, child, name, mimeFor(name)))
                    }
                }
            }
        }
        return out
    }

    /**
     * Replaces [img] with the cleaned bytes in [src], safely. It writes a temp
     * document in the same folder first, deletes the original only once the temp
     * is fully written, then renames the temp to the original name. A failure or
     * crash mid-way leaves either the untouched original or a fully-written clean
     * copy under a temp name, never a truncated file.
     */
    fun replaceInPlace(context: Context, img: TreeImage, src: File): Boolean {
        val resolver = context.contentResolver
        val tmpName = ".scrubpony-" + System.nanoTime() + "-" + img.name
        val tmp = img.parent.createFile(img.mime, tmpName) ?: return false
        val wrote = runCatching {
            resolver.openOutputStream(tmp.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            } != null
        }.getOrDefault(false)
        if (!wrote) {
            runCatching { tmp.delete() }
            return false
        }
        val deleted = runCatching { img.file.delete() }.getOrDefault(false)
        if (!deleted) {
            runCatching { tmp.delete() }
            return false
        }
        val renamed = runCatching {
            DocumentsContract.renameDocument(resolver, tmp.uri, img.name)
        }.getOrNull()
        return renamed != null
    }

    /** Finds or creates a "ScrubPony cleaned" folder directly under the tree. */
    fun cleanedFolder(context: Context, treeUri: Uri): DocumentFile? {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: return null
        return root.findFile(CLEANED_DIR) ?: root.createDirectory(CLEANED_DIR)
    }

    /** Writes [src] into [folder] under [name], leaving the original untouched. */
    fun copyInto(context: Context, folder: DocumentFile, name: String, mime: String, src: File): Boolean {
        val doc = folder.createFile(mime, name) ?: return false
        val wrote = runCatching {
            context.contentResolver.openOutputStream(doc.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            } != null
        }.getOrDefault(false)
        if (!wrote) runCatching { doc.delete() }
        return wrote
    }
}
