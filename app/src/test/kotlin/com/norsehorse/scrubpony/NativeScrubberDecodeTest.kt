package com.norsehorse.scrubpony

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pure-JVM test: exercises decodeScrubStats() only, never NativeScrubber
 *  itself, so it runs under plain `./gradlew test` with no device, emulator,
 *  or native library on the classpath. */
class NativeScrubberDecodeTest {

    @Test
    fun decodesASuccessfulScrubWithGpsAndOrientation() {
        val raw = longArrayOf(0, 3, 5, 12000, 8000, 1, 1, 1)
        val stats = decodeScrubStats(raw)

        assertEquals(NativeScrubber.SP_OK, stats.status)
        assertEquals(3L, stats.dropped)
        assertEquals(5L, stats.kept)
        assertEquals(12000L, stats.inSize)
        assertEquals(8000L, stats.outSize)
        assertTrue(stats.hasGps)
        assertTrue(stats.orientationMatters)
        assertTrue(stats.orientationKept)
    }

    @Test
    fun decodesAnAlreadyCleanFileWithNoGps() {
        val raw = longArrayOf(0, 0, 6, 8000, 8000, 0, 0, 0)
        val stats = decodeScrubStats(raw)

        assertEquals(0L, stats.dropped)
        assertFalse(stats.hasGps)
        assertFalse(stats.orientationKept)
    }

    @Test
    fun decodesAnErrorStatus() {
        val raw = longArrayOf(2, 0, 0, 0, 0, 0, 0, 0)
        val stats = decodeScrubStats(raw)

        assertEquals(NativeScrubber.SP_ERR_NOT_JPEG, stats.status)
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsWrongShapedArrays() {
        decodeScrubStats(longArrayOf(0, 1))
    }
}
