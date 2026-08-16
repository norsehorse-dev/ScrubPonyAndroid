package com.norsehorse.scrubpony

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

sealed interface UiState {
    data object Idle : UiState
    data class Processing(val done: Int, val total: Int) : UiState
    data class Done(val summary: BatchSummary) : UiState
}

class ScrubViewModel(application: Application) : AndroidViewModel(application) {

    private val engine = ScrubEngine(application)

    private val _uiState = MutableStateFlow<UiState>(UiState.Idle)
    val uiState: StateFlow<UiState> = _uiState

    var strict by mutableStateOf(false)
    var keepOrientation by mutableStateOf(true)

    /** Clears the working cache and returns to the picker screen. */
    fun reset() {
        engine.clearWorkingFiles()
        _uiState.value = UiState.Idle
    }

    fun scrub(uris: List<Uri>) {
        if (uris.isEmpty()) return
        engine.clearWorkingFiles()
        viewModelScope.launch {
            val results = mutableListOf<ScrubItemResult>()
            _uiState.value = UiState.Processing(0, uris.size)
            for ((index, uri) in uris.withIndex()) {
                val result = withContext(Dispatchers.IO) {
                    engine.scrubOne(uri, strict, keepOrientation)
                }
                results += result
                _uiState.value = UiState.Processing(index + 1, uris.size)
            }
            _uiState.value = UiState.Done(BatchSummary(results))
        }
    }
}
