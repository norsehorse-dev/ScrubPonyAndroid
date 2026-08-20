package com.norsehorse.scrubpony

import androidx.exifinterface.media.ExifInterface
import java.io.File
import java.util.Locale

/** A category of removed metadata, mapped to a display label by the UI. */
enum class MetaKey { LOCATION, DATE, CAMERA, LENS, SOFTWARE, ARTIST, COPYRIGHT, DESCRIPTION, COMMENT }

/** One human-readable metadata value that was present in the original file. */
data class MetaField(val key: MetaKey, val value: String)

/**
 * Reads the human-relevant metadata out of the original file so the app can show
 * what a scrub removed. This is read-only and separate from the C core, which
 * does the actual byte-level stripping; here we only decode EXIF fields for
 * display. Best-effort: an unsupported container or a malformed tag simply
 * yields fewer fields, never a crash.
 */
object MetadataReader {

    fun read(file: File): List<MetaField> = runCatching {
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
}
