package com.norsehorse.scrubpony

import androidx.exifinterface.media.ExifInterface
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.Locale
import java.util.zip.Inflater

/** A category of removed metadata, mapped to a display label by the UI. OTHER
 *  carries its own label (see MetaField.rawLabel), used for PNG text keywords
 *  that do not map onto a fixed category. */
enum class MetaKey { LOCATION, DATE, CAMERA, LENS, SOFTWARE, ARTIST, COPYRIGHT, DESCRIPTION, COMMENT, OTHER }

/** One human-readable metadata value that was present in the original file.
 *  [rawLabel] overrides the category label when set (PNG keyword names). */
data class MetaField(val key: MetaKey, val value: String, val rawLabel: String? = null)

/**
 * Reads the human-relevant metadata out of the original file so the app can show
 * what a scrub removed. Read-only and separate from the C core, which does the
 * actual byte-level stripping. Covers EXIF (JPEG, WebP, HEIC, and PNG's eXIf
 * chunk) via ExifInterface, plus PNG text chunks (tEXt / zTXt / iTXt), which is
 * where PNGs usually keep their metadata and which ExifInterface does not read.
 * Best-effort: an unsupported container or a malformed tag simply yields fewer
 * fields, never a crash.
 */
object MetadataReader {

    fun read(file: File): List<MetaField> {
        val out = ArrayList<MetaField>()
        out.addAll(readExif(file))
        out.addAll(readPngText(file)) // self-guards: returns empty for non-PNG
        // Drop exact duplicates (same label and value) that both readers found.
        return out.distinctBy { (it.rawLabel ?: it.key.name) + "=" + it.value }
    }

    private fun readExif(file: File): List<MetaField> = runCatching {
        val exif = ExifInterface(file.absolutePath)
        val out = ArrayList<MetaField>()
        exif.getLatLong()?.let { ll ->
            out.add(MetaField(MetaKey.LOCATION, String.format(Locale.US, "%.5f, %.5f", ll[0], ll[1])))
        }
        val date = exif.getAttribute(ExifInterface.TAG_DATETIME_ORIGINAL)
            ?: exif.getAttribute(ExifInterface.TAG_DATETIME)
        date.clean()?.let { out.add(MetaField(MetaKey.DATE, it)) }
        val make = exif.getAttribute(ExifInterface.TAG_MAKE)?.trim().orEmpty()
        val model = exif.getAttribute(ExifInterface.TAG_MODEL)?.trim().orEmpty()
        val camera = listOf(make, model).filter { it.isNotEmpty() }.joinToString(" ")
        if (camera.isNotEmpty()) out.add(MetaField(MetaKey.CAMERA, camera))
        exif.getAttribute(ExifInterface.TAG_LENS_MODEL).clean()?.let { out.add(MetaField(MetaKey.LENS, it)) }
        exif.getAttribute(ExifInterface.TAG_SOFTWARE).clean()?.let { out.add(MetaField(MetaKey.SOFTWARE, it)) }
        exif.getAttribute(ExifInterface.TAG_ARTIST).clean()?.let { out.add(MetaField(MetaKey.ARTIST, it)) }
        exif.getAttribute(ExifInterface.TAG_COPYRIGHT).clean()?.let { out.add(MetaField(MetaKey.COPYRIGHT, it)) }
        exif.getAttribute(ExifInterface.TAG_IMAGE_DESCRIPTION).clean()?.let { out.add(MetaField(MetaKey.DESCRIPTION, it)) }
        exif.getAttribute(ExifInterface.TAG_USER_COMMENT).clean()?.let { out.add(MetaField(MetaKey.COMMENT, it)) }
        out
    }.getOrDefault(emptyList())

    // --- PNG text chunks -----------------------------------------------------

    private val PNG_SIG = byteArrayOf(137.toByte(), 80, 78, 71, 13, 10, 26, 10)

    private fun readPngText(file: File): List<MetaField> = runCatching {
        if (file.length() > 30_000_000L) return emptyList()
        val b = file.readBytes()
        if (b.size < 8 || !b.copyOfRange(0, 8).contentEquals(PNG_SIG)) return emptyList()
        val out = ArrayList<MetaField>()
        var pos = 8
        while (pos + 8 <= b.size) {
            val len = int32(b, pos); pos += 4
            if (len < 0 || pos + len + 4 > b.size) break
            val type = String(b, pos, 4, Charsets.US_ASCII); pos += 4
            if (type == "IEND") break
            if (type == "tEXt" || type == "zTXt" || type == "iTXt") {
                parseTextChunk(type, b.copyOfRange(pos, pos + len))?.let { out.add(it) }
            }
            pos += len + 4 // chunk data + CRC
        }
        out
    }.getOrDefault(emptyList())

    private fun parseTextChunk(type: String, data: ByteArray): MetaField? {
        val sep = data.indexOf(0)
        if (sep <= 0) return null
        val keyword = String(data, 0, sep, Charsets.ISO_8859_1)
        val text: String = when (type) {
            "tEXt" -> String(data, sep + 1, data.size - sep - 1, Charsets.ISO_8859_1)
            "zTXt" -> {
                if (sep + 2 > data.size) return null
                inflate(data.copyOfRange(sep + 2, data.size)).let { String(it, Charsets.ISO_8859_1) }
            }
            "iTXt" -> {
                if (sep + 3 > data.size) return null
                val compFlag = data[sep + 1].toInt()
                // skip: compFlag(1) compMethod(1) langTag \0 transKeyword \0
                var p = sep + 3
                p = data.indexOf(0, p) + 1; if (p <= 0) return null
                p = data.indexOf(0, p) + 1; if (p <= 0 || p > data.size) return null
                val body = data.copyOfRange(p, data.size)
                if (compFlag == 1) inflate(body).let { String(it, Charsets.UTF_8) }
                else String(body, Charsets.UTF_8)
            }
            else -> return null
        }
        return mapKeyword(keyword, text)
    }

    private fun mapKeyword(keyword: String, rawValue: String): MetaField? {
        val value = rawValue.replace('\n', ' ').replace('\r', ' ').trim().take(140)
        if (value.isEmpty()) return null
        if (keyword.startsWith("XML", ignoreCase = true)) return null // XMP blob, not useful as text
        return when (keyword.lowercase(Locale.US)) {
            "software" -> MetaField(MetaKey.SOFTWARE, value)
            "author", "artist" -> MetaField(MetaKey.ARTIST, value)
            "copyright" -> MetaField(MetaKey.COPYRIGHT, value)
            "description" -> MetaField(MetaKey.DESCRIPTION, value)
            "comment" -> MetaField(MetaKey.COMMENT, value)
            else -> MetaField(MetaKey.OTHER, value, rawLabel = keyword)
        }
    }

    private fun inflate(data: ByteArray): ByteArray {
        val inf = Inflater()
        inf.setInput(data)
        val out = ByteArrayOutputStream()
        val buf = ByteArray(4096)
        while (!inf.finished()) {
            val n = inf.inflate(buf)
            if (n == 0 && (inf.needsInput() || inf.needsDictionary())) break
            out.write(buf, 0, n)
        }
        inf.end()
        return out.toByteArray()
    }

    private fun ByteArray.indexOf(b: Int, from: Int = 0): Int {
        var i = from
        while (i < size) { if (this[i].toInt() == b) return i; i++ }
        return -1
    }

    private fun int32(b: ByteArray, o: Int): Int =
        ((b[o].toInt() and 0xff) shl 24) or ((b[o + 1].toInt() and 0xff) shl 16) or
            ((b[o + 2].toInt() and 0xff) shl 8) or (b[o + 3].toInt() and 0xff)

    private fun String?.clean(): String? = this?.trim()?.takeIf { it.isNotEmpty() }
}

fun metaLabelRes(key: MetaKey): Int = when (key) {
    MetaKey.LOCATION -> R.string.meta_location
    MetaKey.DATE -> R.string.meta_date
    MetaKey.CAMERA -> R.string.meta_camera
    MetaKey.LENS -> R.string.meta_lens
    MetaKey.SOFTWARE -> R.string.meta_software
    MetaKey.ARTIST -> R.string.meta_artist
    MetaKey.COPYRIGHT -> R.string.meta_copyright
    MetaKey.DESCRIPTION -> R.string.meta_description
    MetaKey.COMMENT -> R.string.meta_comment
    MetaKey.OTHER -> R.string.meta_other
}
