@file:OptIn(ExperimentalMaterial3Api::class)

package com.norsehorse.scrubpony.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.norsehorse.scrubpony.BatchSummary
import com.norsehorse.scrubpony.FileOutcome
import com.norsehorse.scrubpony.ScrubItemResult
import com.norsehorse.scrubpony.ScrubViewModel
import com.norsehorse.scrubpony.UiState
import java.io.File

@Composable
fun ScrubScreen(
    viewModel: ScrubViewModel,
    onPickImages: () -> Unit,
    onShareResults: (List<File>) -> Unit,
    onSaveResults: (List<File>) -> Unit,
    onSaveToFiles: (List<File>) -> Unit,
) {
    val state by viewModel.uiState.collectAsState()

    Scaffold(
        topBar = { TopAppBar(title = { Text("ScrubPony") }) },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(16.dp),
        ) {
            OptionsRow(
                label = "Keep orientation",
                checked = viewModel.keepOrientation,
                onCheckedChange = { viewModel.keepOrientation = it },
            )
            OptionsRow(
                label = "Strict (also drop the colour profile)",
                checked = viewModel.strict,
                onCheckedChange = { viewModel.strict = it },
            )

            Spacer(Modifier.height(16.dp))

            when (val s = state) {
                is UiState.Idle -> IdleContent(onPickImages)
                is UiState.Processing -> ProcessingContent(s)
                is UiState.Done -> DoneContent(
                    summary = s.summary,
                    onReset = viewModel::reset,
                    onShareResults = onShareResults,
                    onSaveResults = onSaveResults,
                    onSaveToFiles = onSaveToFiles,
                )
            }
        }
    }
}

@Composable
private fun OptionsRow(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun IdleContent(onPickImages: () -> Unit) {
    Text(
        "Pick photos to strip GPS, timestamps, device info and embedded " +
            "thumbnails — pixels untouched. Or share photos into ScrubPony " +
            "from any other app's share sheet.",
    )
    Spacer(Modifier.height(16.dp))
    Button(onClick = onPickImages) { Text("Choose photos") }
}

@Composable
private fun ProcessingContent(state: UiState.Processing) {
    val fraction = if (state.total == 0) 0f else state.done / state.total.toFloat()
    LinearProgressIndicator(progress = { fraction }, modifier = Modifier.fillMaxWidth())
    Spacer(Modifier.height(8.dp))
    Text("Scrubbing ${state.done} of ${state.total}…")
}

@Composable
private fun DoneContent(
    summary: BatchSummary,
    onReset: () -> Unit,
    onShareResults: (List<File>) -> Unit,
    onSaveResults: (List<File>) -> Unit,
    onSaveToFiles: (List<File>) -> Unit,
) {
    val outputs = summary.results.mapNotNull { it.outputFile }

    SummaryCard(summary)
    Spacer(Modifier.height(12.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Button(onClick = { onSaveResults(outputs) }, enabled = outputs.isNotEmpty()) {
            Text("Save to Pictures")
        }
        OutlinedButton(onClick = { onSaveToFiles(outputs) }, enabled = outputs.isNotEmpty()) {
            Text("Save to files")
        }
    }
    Spacer(Modifier.height(8.dp))
    OutlinedButton(
        onClick = { onShareResults(outputs) },
        enabled = outputs.isNotEmpty(),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text("Share clean copies")
    }
    Spacer(Modifier.height(8.dp))
    TextButton(onClick = onReset) { Text("Start over") }
    Spacer(Modifier.height(8.dp))
    LazyColumn {
        items(summary.results) { ResultRow(it) }
    }
}

@Composable
private fun SummaryCard(summary: BatchSummary) {
    Card {
        Column(Modifier.padding(16.dp)) {
            Text(
                "${summary.scrubbed} scrubbed, ${summary.alreadyClean} already clean, " +
                    "${summary.unsupported} unsupported, ${summary.failed} failed",
                style = MaterialTheme.typography.titleMedium,
            )
            if (summary.bytesRemoved > 0) {
                Text("${summary.bytesRemoved} bytes of metadata removed")
            }
            if (summary.withGps > 0) {
                Text("${summary.withGps} carried GPS coordinates")
            }
            if (summary.orientationPreserved > 0) {
                Text("${summary.orientationPreserved} kept their upright orientation")
            }
        }
    }
}

@Composable
private fun ResultRow(result: ScrubItemResult) {
    ListItem(
        headlineContent = { Text(result.displayName) },
        supportingContent = { Text(result.message) },
        trailingContent = {
            val label = when (result.outcome) {
                FileOutcome.SCRUBBED -> "Scrubbed"
                FileOutcome.ALREADY_CLEAN -> "Clean"
                FileOutcome.UNSUPPORTED -> "Not supported"
                FileOutcome.FAILED -> "Failed"
            }
            Text(label)
        },
    )
}
