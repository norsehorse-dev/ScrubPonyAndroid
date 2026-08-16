package com.norsehorse.scrubpony

/** The four containers the native core can scrub. Ordinals match the format
 *  code the JNI packs into result[8] (see scrubpony_jni.c): JPEG=0, PNG=1,
 *  WEBP=2, HEIC=3. */
enum class ImageFormat { JPEG, PNG, WEBP, HEIC }

data class ScrubStats(
    val status: Int,
    val dropped: Long,
    val kept: Long,
    val inSize: Long,
    val outSize: Long,
    val hasGps: Boolean,
    val orientationMatters: Boolean,
    val orientationKept: Boolean,
    val format: ImageFormat,
)

/** Decodes the packed array [NativeScrubber.scrubFile] returns. Pure Kotlin
 *  so it can be unit-tested without a device or the native library loaded —
 *  see src/test/kotlin/.../NativeScrubberDecodeTest.kt.
 *
 *  [format] is meaningful whenever the native side got far enough to identify
 *  the container — including on an SP_ERR_UNSUPPORTED HEIC, where it knew the
 *  format but would not rewrite that layout. On the earlier failures (bad
 *  path, unrecognised format) it never identified one and defaults to JPEG
 *  rather than leaving it undefined. */
fun decodeScrubStats(raw: LongArray): ScrubStats {
    require(raw.size == 9) { "expected a 9-element result, got ${raw.size}" }
    val format = when (raw[8]) {
        1L -> ImageFormat.PNG
        2L -> ImageFormat.WEBP
        3L -> ImageFormat.HEIC
        else -> ImageFormat.JPEG
    }
    return ScrubStats(
        status = raw[0].toInt(),
        dropped = raw[1],
        kept = raw[2],
        inSize = raw[3],
        outSize = raw[4],
        hasGps = raw[5] != 0L,
        orientationMatters = raw[6] != 0L,
        orientationKept = raw[7] != 0L,
        format = format,
    )
}
