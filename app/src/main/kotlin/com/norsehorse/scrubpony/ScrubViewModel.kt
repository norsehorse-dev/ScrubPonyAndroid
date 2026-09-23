package com.norsehorse.scrubpony

import android.app.Application
import android.content.Context
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

    private val prefs = application.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // Settings toggles are saved so they survive the app being closed (issue #1).
    private var _strict by mutableStateOf(prefs.getBoolean(KEY_STRICT, false))
    var strict: Boolean
        get() = _strict
        set(value) {
            _strict = value
            prefs.edit().putBoolean(KEY_STRICT, value).apply()
        }

    private var _keepOrientation by mutableStateOf(prefs.getBoolean(KEY_KEEP_ORIENTATION, true))
    var keepOrientation: Boolean
        get() = _keepOrientation
        set(value) {
            _keepOrientation = value
            prefs.edit().putBoolean(KEY_KEEP_ORIENTATION, value).apply()
        }

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

// Same file MainActivity uses for the onboarding flag.
private const val PREFS = "scrubpony_prefs"
private const val KEY_STRICT = "strict_mode"
private const val KEY_KEEP_ORIENTATION = "keep_orientation"
