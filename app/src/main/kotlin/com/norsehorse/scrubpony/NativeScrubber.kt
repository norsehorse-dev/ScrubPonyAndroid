package com.norsehorse.scrubpony

/**
 * JNI bridge to the unmodified desktop ScrubPony core (app/src/main/cpp/core,
 * copied verbatim from ScrubPonyDesktop/src). All parsing, policy and
 * atomic-write logic lives there and is unchanged by this app; this object is
 * only the call boundary.
 *
 * Kept free of Android imports on purpose so [ScrubStats] decoding
 * (ScrubStats.kt) stays unit-testable on a plain JVM without touching
 * System.loadLibrary.
 */
object NativeScrubber {
    init {
        System.loadLibrary("scrubpony_jni")
    }

    // Mirrors sp_status in scrubpony.h exactly — keep in sync with that enum.
    const val SP_OK = 0
    const val SP_ERR_IO = 1
    const val SP_ERR_NOT_JPEG = 2
    const val SP_ERR_NOT_REGULAR = 3
    const val SP_ERR_TRUNCATED = 4
    const val SP_ERR_MALFORMED = 5
    const val SP_ERR_EXISTS = 6
    const val SP_ERR_OUTPUT_GREW = 7
    const val SP_ERR_USAGE = 8

    /**
     * Scrubs [inputPath] to [outputPath], both real filesystem paths on this
     * device (never content:// URIs — stage those to a file first, e.g. via
     * ScrubEngine). Always writes a fresh file; never edits in place.
     *
     * Returns a packed long[8]:
     *   [0] sp_status               [4] out_size
     *   [1] segments dropped        [5] has_gps (0/1)
     *   [2] segments kept           [6] orientation_matters (0/1)
     *   [3] in_size                 [7] orientation_kept (0/1)
     */
    external fun scrubFile(
        inputPath: String,
        outputPath: String,
        strict: Boolean,
        noOrientation: Boolean,
    ): LongArray

    /** Human-readable message for a status code, straight from sp_strstatus. */
    external fun statusMessage(status: Int): String
}
