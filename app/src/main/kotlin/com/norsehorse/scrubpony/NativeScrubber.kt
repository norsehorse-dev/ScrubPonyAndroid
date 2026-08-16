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
    // The NOT_* format codes were inserted in the order support was added
    // (PNG, then WebP, then HEIC) and SP_ERR_UNSUPPORTED after MALFORMED,
    // each shifting every value after it; these are the current 1.3 values,
    // not the 1.0/1.1 ones.
    const val SP_OK = 0
    const val SP_ERR_IO = 1
    const val SP_ERR_NOT_JPEG = 2
    const val SP_ERR_NOT_PNG = 3
    const val SP_ERR_NOT_WEBP = 4
    const val SP_ERR_NOT_HEIF = 5
    const val SP_ERR_NOT_REGULAR = 6
    const val SP_ERR_TRUNCATED = 7
    const val SP_ERR_MALFORMED = 8
    const val SP_ERR_UNSUPPORTED = 9
    const val SP_ERR_EXISTS = 10
    const val SP_ERR_OUTPUT_GREW = 11
    const val SP_ERR_USAGE = 12

    /**
     * Scrubs [inputPath] to [outputPath], both real filesystem paths on this
     * device (never content:// URIs — stage those to a file first, e.g. via
     * ScrubEngine). Detects JPEG, PNG, WebP or HEIC automatically, the same
     * way the desktop CLI's detect_format() does; always writes a fresh file,
     * never edits in place.
     *
     * Returns a packed long[9]:
     *   [0] sp_status                    [5] has_gps (0/1)
     *   [1] segments/chunks/items dropped [6] orientation_matters (0/1)
     *   [2] segments/chunks/items kept    [7] orientation_kept (0/1)
     *   [3] in_size                      [8] format (0=JPEG 1=PNG 2=WebP 3=HEIC)
     *   [4] out_size
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
