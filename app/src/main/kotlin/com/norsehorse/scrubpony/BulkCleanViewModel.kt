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
 * same ScrubEngine the single-file path uses, so the scan is a real scrub to
 * the cache (that is also what produces the clean copy the clean step writes).
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
            val images = withContext(Dispatchers.IO) {
                BulkFiles.listImages(getApplication(), treeUri)
            }
            _state.value = BulkState.Scanning(0, images.size)
            val items = ArrayList<BulkItem>(images.size)
            for ((i, img) in images.withIndex()) {
                val r = withContext(Dispatchers.IO) {
                    engine.scrubOne(img.file.uri, strict, keepOrientation)
                }
                items.add(BulkItem(img, r.outcome, r.outputFile, r.bytesRemoved))
                _state.value = BulkState.Scanning(i + 1, images.size)
            }
            _state.value = BulkState.Reviewed(items)
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
            _state.value = BulkState.Cleaning(0, dirty.size)
            val folder = if (toSubfolder && tree != null) {
                withContext(Dispatchers.IO) { BulkFiles.cleanedFolder(getApplication(), tree) }
            } else null
            var ok = 0
            var failed = 0
            for ((i, item) in dirty.withIndex()) {
                val src = item.cleaned ?: continue
                val done = withContext(Dispatchers.IO) {
                    if (toSubfolder) {
                        folder != null && BulkFiles.copyInto(
                            getApplication(), folder, item.image.name, item.image.mime, src,
                        )
                    } else {
                        BulkFiles.replaceInPlace(getApplication(), item.image, src)
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
