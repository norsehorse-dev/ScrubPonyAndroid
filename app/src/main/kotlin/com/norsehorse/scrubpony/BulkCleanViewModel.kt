package com.norsehorse.scrubpony

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Drives the bulk folder clean: scan a picked folder, review what carries
 * metadata, then clean either in place or into a copies subfolder. Reuses the
 * same ScrubEngine the single-file path uses, so the scan is a real scrub to the
 * cache (that is also what produces the clean copy the clean step writes) unless
 * the scan cache already knows the file is clean, in which case it is skipped.
 */
sealed interface BulkState {
    data object Idle : BulkState
    data class Scanning(val done: Int, val total: Int) : BulkState
    data class Reviewed(val items: List<BulkItem>) : BulkState
    data class Cleaning(val done: Int, val total: Int) : BulkState
    data class Done(val cleaned: Int, val failed: Int, val toSubfolder: Boolean) : BulkState
}

data class BulkItem(
    val image: TreeImage,
    val outcome: FileOutcome,
    val cleaned: File?,        // cached clean copy, present when outcome == SCRUBBED
    val bytesRemoved: Long,
    val metadata: List<MetaField> = emptyList(),
)

class BulkCleanViewModel(application: Application) : AndroidViewModel(application) {

    private val engine = ScrubEngine(application)
    private val _state = MutableStateFlow<BulkState>(BulkState.Idle)
    val state: StateFlow<BulkState> = _state

    private var treeUri: Uri? = null

    fun close() {
        engine.clearWorkingFiles()
        treeUri = null
        _state.value = BulkState.Idle
    }

    fun scan(treeUri: Uri, strict: Boolean, keepOrientation: Boolean) {
        this.treeUri = treeUri
        viewModelScope.launch {
            engine.clearWorkingFiles()
            _state.value = BulkState.Scanning(0, 0)
            val app = getApplication<Application>()
            val images = withContext(Dispatchers.IO) { BulkFiles.listImages(app, treeUri) }
            _state.value = BulkState.Scanning(0, images.size)

            val cache = withContext(Dispatchers.IO) { ScanCache.loadClean(app) }
            val items = ArrayList<BulkItem>(images.size)
            for ((i, img) in images.withIndex()) {
                val key = BulkFiles.cacheKey(img)
                val item = if (key in cache) {
                    // Known clean and unchanged: skip the read entirely.
                    BulkItem(img, FileOutcome.ALREADY_CLEAN, null, 0)
                } else {
                    val r = withContext(Dispatchers.IO) { engine.scrubOne(img.file.uri, strict, keepOrientation) }
                    if (r.outcome == FileOutcome.ALREADY_CLEAN) cache.add(key)
                    BulkItem(img, r.outcome, r.outputFile, r.bytesRemoved, r.metadata)
                }
                items.add(item)
                _state.value = BulkState.Scanning(i + 1, images.size)
            }
            withContext(Dispatchers.IO) { ScanCache.saveClean(app, cache) }

            // Files that carry metadata first, then already clean, then skipped.
            val sorted = items.sortedBy {
                when (it.outcome) {
                    FileOutcome.SCRUBBED -> 0
                    FileOutcome.ALREADY_CLEAN -> 1
                    FileOutcome.UNSUPPORTED -> 2
                    FileOutcome.FAILED -> 3
                }
            }
            _state.value = BulkState.Reviewed(sorted)
        }
    }

    fun cleanInPlace() = clean(toSubfolder = false)

    fun cleanToSubfolder() = clean(toSubfolder = true)

    private fun clean(toSubfolder: Boolean) {
        val reviewed = _state.value as? BulkState.Reviewed ?: return
        val tree = treeUri
        val dirty = reviewed.items.filter { it.outcome == FileOutcome.SCRUBBED && it.cleaned != null }
        if (dirty.isEmpty()) return
        viewModelScope.launch {
            val app = getApplication<Application>()
            _state.value = BulkState.Cleaning(0, dirty.size)
            val folder = if (toSubfolder && tree != null) {
                withContext(Dispatchers.IO) { BulkFiles.cleanedFolder(app, tree) }
            } else null
            var ok = 0
            var failed = 0
            for ((i, item) in dirty.withIndex()) {
                val src = item.cleaned ?: continue
                val done = withContext(Dispatchers.IO) {
                    if (toSubfolder) {
                        folder != null && BulkFiles.copyInto(app, folder, item.image.name, item.image.mime, src)
                    } else {
                        BulkFiles.replaceInPlace(app, item.image, src)
                    }
                }
                if (done) ok++ else failed++
                _state.value = BulkState.Cleaning(i + 1, dirty.size)
            }
            engine.clearWorkingFiles()
            _state.value = BulkState.Done(ok, failed, toSubfolder)
        }
    }
}
