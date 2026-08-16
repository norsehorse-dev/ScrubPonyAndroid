package com.norsehorse.scrubpony

data class ScrubStats(
    val status: Int,
    val dropped: Long,
    val kept: Long,
    val inSize: Long,
    val outSize: Long,
    val hasGps: Boolean,
    val orientationMatters: Boolean,
    val orientationKept: Boolean,
)

/** Decodes the packed array [NativeScrubber.scrubFile] returns. Pure Kotlin
 *  so it can be unit-tested without a device or the native library loaded —
 *  see src/test/kotlin/.../NativeScrubberDecodeTest.kt. */
fun decodeScrubStats(raw: LongArray): ScrubStats {
    require(raw.size == 8) { "expected an 8-element result, got ${raw.size}" }
    return ScrubStats(
        status = raw[0].toInt(),
        dropped = raw[1],
        kept = raw[2],
        inSize = raw[3],
        outSize = raw[4],
        hasGps = raw[5] != 0L,
        orientationMatters = raw[6] != 0L,
        orientationKept = raw[7] != 0L,
    )
}
