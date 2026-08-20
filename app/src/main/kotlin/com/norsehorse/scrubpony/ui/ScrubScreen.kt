package com.norsehorse.scrubpony.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.norsehorse.scrubpony.BatchSummary
import com.norsehorse.scrubpony.FileOutcome
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.MetaKey
import com.norsehorse.scrubpony.ScrubItemResult
import com.norsehorse.scrubpony.ScrubPonySerifItalic
import com.norsehorse.scrubpony.ScrubPonyTheme
import com.norsehorse.scrubpony.ScrubViewModel
import com.norsehorse.scrubpony.UiState
import androidx.compose.ui.res.stringResource
import java.io.File

/**
 * The Scrub tab. The bottom nav and the scrub options now live elsewhere
 * (MainActivity owns the nav; the toggles moved to Settings), so this screen
 * is just the three states of a batch: pick, progress, results. It fills the
 * content area MainActivity hands it through [modifier].
 */
@Composable
fun ScrubScreen(
    viewModel: ScrubViewModel,
    onPickImages: () -> Unit,
    onPickFiles: () -> Unit,
    onShareResults: (List<File>) -> Unit,
    onSaveResults: (List<File>) -> Unit,
    onSaveToFiles: (List<File>) -> Unit,
    onCleanFolder: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val state by viewModel.uiState.collectAsState()

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(ScrubPonyTheme.background),
    ) {
        when (val s = state) {
            is UiState.Idle -> IdleContent(onPickImages, onPickFiles, onCleanFolder)
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

@Composable
private fun Wordmark() {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            "Scrub",
            style = MaterialTheme.typography.displaySmall,
            color = ScrubPonyTheme.ink,
        )
        Text(
            "Pony",
            style = MaterialTheme.typography.displaySmall.merge(ScrubPonySerifItalic),
            color = ScrubPonyTheme.accent,
        )
    }
}

@Composable
private fun IdleContent(onPickImages: () -> Unit, onPickFiles: () -> Unit, onCleanFolder: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(8.dp))
        Wordmark()
        Spacer(Modifier.height(10.dp))
        Text(
            stringResource(R.string.scrub_tagline),
            style = MaterialTheme.typography.bodyLarge,
            color = ScrubPonyTheme.dim,
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.weight(1f))

        Box(
            modifier = Modifier
                .size(120.dp)
                .background(ScrubPonyTheme.panel, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text("🐴", style = MaterialTheme.typography.displaySmall)
        }

        Spacer(Modifier.height(20.dp))
        Text(
            stringResource(R.string.scrub_headline),
            style = MaterialTheme.typography.titleLarge,
            color = ScrubPonyTheme.ink,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(R.string.scrub_pick_hint),
            style = MaterialTheme.typography.bodyMedium,
            color = ScrubPonyTheme.dim,
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.weight(1f))

        PrimaryButton(
            text = stringResource(R.string.scrub_choose_photos),
            onClick = onPickImages,
        )
        Spacer(Modifier.height(10.dp))
        SecondaryButton(
            text = stringResource(R.string.scrub_pick_from_files),
            onClick = onPickFiles,
        )
        Spacer(Modifier.height(10.dp))
        SecondaryButton(
            text = stringResource(R.string.scrub_clean_folder),
            onClick = onCleanFolder,
        )
        Spacer(Modifier.height(14.dp))
        Text(
            stringResource(R.string.scrub_files_note),
            style = MaterialTheme.typography.bodySmall,
            color = ScrubPonyTheme.dim,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun ProcessingContent(state: UiState.Processing) {
    val fraction = if (state.total == 0) 0f else state.done / state.total.toFloat()
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            stringResource(R.string.scrub_working),
            style = MaterialTheme.typography.titleLarge,
            color = ScrubPonyTheme.ink,
        )
        Spacer(Modifier.height(20.dp))
        LinearProgressIndicator(
            progress = { fraction },
            modifier = Modifier.fillMaxWidth().height(6.dp),
            color = ScrubPonyTheme.accent,
            trackColor = ScrubPonyTheme.line,
        )
        Spacer(Modifier.height(12.dp))
        Text(
            stringResource(R.string.scrub_progress, state.done, state.total),
            style = MaterialTheme.typography.bodyMedium,
            color = ScrubPonyTheme.dim,
        )
    }
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

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item { Spacer(Modifier.height(8.dp)) }
        item { SummaryPanel(summary) }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
                PrimaryButton(
                    text = stringResource(R.string.scrub_save_pictures),
                    onClick = { onSaveResults(outputs) },
                    enabled = outputs.isNotEmpty(),
                    modifier = Modifier.weight(1f),
                )
                SecondaryButton(
                    text = stringResource(R.string.scrub_save_files),
                    onClick = { onSaveToFiles(outputs) },
                    enabled = outputs.isNotEmpty(),
                    modifier = Modifier.weight(1f),
                )
            }
        }
        item {
            SecondaryButton(
                text = stringResource(R.string.scrub_share),
                onClick = { onShareResults(outputs) },
                enabled = outputs.isNotEmpty(),
            )
        }
        item {
            Text(
                stringResource(R.string.scrub_start_over),
                color = ScrubPonyTheme.accent,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onReset)
                    .padding(vertical = 8.dp),
            )
        }
        item {
            Text(
                stringResource(R.string.scrub_results_header).uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = ScrubPonyTheme.dim,
                modifier = Modifier.padding(start = 4.dp, top = 4.dp),
            )
        }
        items(summary.results) { ResultRow(it) }
        item { Spacer(Modifier.height(24.dp)) }
    }
}

@Composable
private fun SummaryPanel(summary: BatchSummary) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ScrubPonyTheme.panel, RoundedCornerShape(16.dp))
            .padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            stringResource(
                R.string.scrub_summary_counts,
                summary.scrubbed,
                summary.alreadyClean,
                summary.unsupported,
                summary.failed,
            ),
            style = MaterialTheme.typography.titleMedium,
            color = ScrubPonyTheme.ink,
        )
        if (summary.bytesRemoved > 0) {
            SummaryLine(stringResource(R.string.scrub_summary_bytes, summary.bytesRemoved))
        }
        if (summary.withGps > 0) {
            SummaryLine(stringResource(R.string.scrub_summary_gps, summary.withGps))
        }
        if (summary.orientationPreserved > 0) {
            SummaryLine(stringResource(R.string.scrub_summary_orientation, summary.orientationPreserved))
        }
    }
}

@Composable
private fun SummaryLine(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = ScrubPonyTheme.dim)
}

@Composable
private fun ResultRow(result: ScrubItemResult) {
    val (label, tint) = when (result.outcome) {
        FileOutcome.SCRUBBED -> stringResource(R.string.outcome_scrubbed) to ScrubPonyTheme.success
        FileOutcome.ALREADY_CLEAN -> stringResource(R.string.outcome_clean) to ScrubPonyTheme.accentBright
        FileOutcome.UNSUPPORTED -> stringResource(R.string.outcome_unsupported) to ScrubPonyTheme.dim
        FileOutcome.FAILED -> stringResource(R.string.outcome_failed) to ScrubPonyTheme.danger
    }
    val hasDetails = result.metadata.isNotEmpty()
    var expanded by remember { mutableStateOf(false) }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ScrubPonyTheme.panel, RoundedCornerShape(12.dp))
            .then(if (hasDetails) Modifier.clickable { expanded = !expanded } else Modifier)
            .padding(14.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(result.displayName, color = ScrubPonyTheme.ink, maxLines = 1)
                Text(
                    result.message,
                    style = MaterialTheme.typography.bodySmall,
                    color = ScrubPonyTheme.dim,
                )
            }
            Spacer(Modifier.size(10.dp))
            Text(label, color = tint, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Medium)
        }
        if (hasDetails) {
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(if (expanded) R.string.meta_hide else R.string.meta_show),
                style = MaterialTheme.typography.labelSmall,
                color = ScrubPonyTheme.accent,
                fontWeight = FontWeight.Medium,
            )
            if (expanded) {
                Spacer(Modifier.height(8.dp))
                result.metadata.forEach { field ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                        verticalAlignment = Alignment.Top,
                    ) {
                        Text(
                            stringResource(metaLabel(field.key)),
                            style = MaterialTheme.typography.bodySmall,
                            color = ScrubPonyTheme.dim,
                            modifier = Modifier.width(104.dp),
                        )
                        Text(
                            field.value,
                            style = MaterialTheme.typography.bodySmall,
                            color = ScrubPonyTheme.ink,
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
            }
        }
    }
}

private fun metaLabel(key: MetaKey): Int = when (key) {
    MetaKey.LOCATION -> R.string.meta_location
    MetaKey.DATE -> R.string.meta_date
    MetaKey.CAMERA -> R.string.meta_camera
    MetaKey.LENS -> R.string.meta_lens
    MetaKey.SOFTWARE -> R.string.meta_software
    MetaKey.ARTIST -> R.string.meta_artist
    MetaKey.COPYRIGHT -> R.string.meta_copyright
    MetaKey.DESCRIPTION -> R.string.meta_description
    MetaKey.COMMENT -> R.string.meta_comment
}

@Composable
private fun PrimaryButton(
    text: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.fillMaxWidth().height(52.dp),
        shape = RoundedCornerShape(14.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = ScrubPonyTheme.accent,
            contentColor = ScrubPonyTheme.onAccent,
            disabledContainerColor = ScrubPonyTheme.line,
            disabledContentColor = ScrubPonyTheme.dim,
        ),
    ) {
        Text(text, style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun SecondaryButton(
    text: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
) {
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.fillMaxWidth().height(52.dp),
        shape = RoundedCornerShape(14.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, SolidColor(ScrubPonyTheme.line)),
        colors = ButtonDefaults.outlinedButtonColors(
            contentColor = ScrubPonyTheme.ink,
            disabledContentColor = ScrubPonyTheme.dim,
        ),
    ) {
        Text(text, style = MaterialTheme.typography.titleMedium)
    }
}
