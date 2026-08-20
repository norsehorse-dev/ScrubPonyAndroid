package com.norsehorse.scrubpony

import android.content.Context

/**
 * A tiny persisted cache of files known to be already clean, keyed by document
 * id plus size and modified time (see BulkFiles.cacheKey). On a re-scan of a
 * folder, a file whose key is in the cache is taken as clean without being read
 * again, which is where the speed-up comes from: hashing or scrubbing a file
 * means reading the whole thing, but size and modified time come for free from
 * the file listing. A file that has changed gets a new key and is scanned
 * normally, so the cache can never mark a modified file as clean.
 */
object ScanCache {

    private const val PREFS = "scrubpony_scan_cache"
    private const val KEY = "clean"
    private const val CAP = 50000

    /** Loads a mutable snapshot of the clean-key set. */
    fun loadClean(context: Context): MutableSet<String> =
        HashSet(prefs(context).getStringSet(KEY, emptySet()) ?: emptySet())

    /** Writes the clean-key set back, trimming if it has grown past the cap. */
    fun saveClean(context: Context, keys: Set<String>) {
        val toStore = if (keys.size > CAP) emptySet() else keys
        prefs(context).edit().putStringSet(KEY, HashSet(toStore)).apply()
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
