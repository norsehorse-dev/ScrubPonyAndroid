package com.norsehorse.scrubpony.ui.bulk

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.norsehorse.scrubpony.BulkCleanViewModel
import com.norsehorse.scrubpony.BulkItem
import com.norsehorse.scrubpony.BulkState
import com.norsehorse.scrubpony.FileOutcome
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.ScrubPonyTheme
import com.norsehorse.scrubpony.metaLabelRes

/**
 * Full-screen overlay for the bulk folder clean. Shows nothing while the view
 * model is Idle; otherwise walks scan, review (with both clean modes), progress,
 * and a result. Rendered once at the top of the app so it floats over whatever
 * tab is showing.
 */
@Composable
fun BulkCleanOverlay(vm: BulkCleanViewModel) {
    val state by vm.state.collectAsState()
    val s = state
    if (s is BulkState.Idle) return

    val dismissable = s is BulkState.Reviewed || s is BulkState.Done
    Dialog(
        onDismissRequest = { if (dismissable) vm.close() },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(modifier = Modifier.fillMaxSize(), color = ScrubPonyTheme.background) {
            when (s) {
                is BulkState.Scanning -> Progress(
                    title = stringResource(R.string.bulk_title),
                    line = stringResource(R.string.bulk_scanning, s.done, s.total),
                    fraction = frac(s.done, s.total),
                )
                is BulkState.Cleaning -> Progress(
                    title = stringResource(R.string.bulk_title),
                    line = stringResource(R.string.bulk_cleaning, s.done, s.total),
                    fraction = frac(s.done, s.total),
                )
                is BulkState.Reviewed -> Review(s.items, vm)
                is BulkState.Done -> DoneView(s.cleaned, s.failed, s.toSubfolder) { vm.close() }
                BulkState.Idle -> Unit
            }
        }
    }
}

private fun frac(done: Int, total: Int) = if (total == 0) 0f else done / total.toFloat()

@Composable
private fun Progress(title: String, line: String, fraction: Float) {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(title, style = MaterialTheme.typography.titleLarge, color = ScrubPonyTheme.ink)
        Spacer(Modifier.height(20.dp))
        LinearProgressIndicator(
            progress = { fraction },
            modifier = Modifier.fillMaxWidth().height(6.dp),
            color = ScrubPonyTheme.accent,
            trackColor = ScrubPonyTheme.line,
        )
        Spacer(Modifier.height(12.dp))
        Text(line, style = MaterialTheme.typography.bodyMedium, color = ScrubPonyTheme.dim)
    }
}

@Composable
private fun Review(items: List<BulkItem>, vm: BulkCleanViewModel) {
    val dirty = items.count { it.outcome == FileOutcome.SCRUBBED }
    val clean = items.count { it.outcome == FileOutcome.ALREADY_CLEAN }
    val skipped = items.count { it.outcome == FileOutcome.UNSUPPORTED || it.outcome == FileOutcome.FAILED }

    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item { Spacer(Modifier.height(12.dp)) }
        item {
            Text(
                stringResource(R.string.bulk_title),
                style = MaterialTheme.typography.headlineMedium,
                color = ScrubPonyTheme.ink,
            )
        }
        item {
            Text(
                stringResource(R.string.bulk_review_summary, dirty, items.size),
                style = MaterialTheme.typography.bodyLarge,
                color = ScrubPonyTheme.ink,
            )
            Text(
                stringResource(R.string.bulk_review_breakdown, clean, skipped),
                style = MaterialTheme.typography.bodySmall,
                color = ScrubPonyTheme.dim,
            )
        }

        if (dirty == 0) {
            item {
                Text(
                    stringResource(R.string.bulk_none),
                    style = MaterialTheme.typography.bodyMedium,
                    color = ScrubPonyTheme.dim,
                    modifier = Modifier.padding(top = 6.dp),
                )
            }
        } else {
            item {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Primary(stringResource(R.string.bulk_replace)) { vm.cleanInPlace() }
                    Text(
                        stringResource(R.string.bulk_replace_note),
                        style = MaterialTheme.typography.bodySmall,
                        color = ScrubPonyTheme.dim,
                        modifier = Modifier.padding(start = 4.dp),
                    )
                    Spacer(Modifier.height(6.dp))
                    Secondary(stringResource(R.string.bulk_save_copies)) { vm.cleanToSubfolder() }
                    Text(
                        stringResource(R.string.bulk_save_copies_note),
                        style = MaterialTheme.typography.bodySmall,
                        color = ScrubPonyTheme.dim,
                        modifier = Modifier.padding(start = 4.dp),
                    )
                }
            }
        }

        item {
            Text(
                stringResource(R.string.bulk_files_header).uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = ScrubPonyTheme.dim,
                modifier = Modifier.padding(start = 4.dp, top = 6.dp),
            )
        }
        items(items) { FileRow(it) }
        item {
            Text(
                stringResource(R.string.action_close),
                color = ScrubPonyTheme.accent,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { vm.close() }
                    .padding(vertical = 14.dp),
            )
        }
        item { Spacer(Modifier.height(24.dp)) }
    }
}

@Composable
private fun FileRow(item: BulkItem) {
    val (label, tint) = when (item.outcome) {
        FileOutcome.SCRUBBED -> stringResource(R.string.outcome_scrubbed) to ScrubPonyTheme.accentBright
        FileOutcome.ALREADY_CLEAN -> stringResource(R.string.outcome_clean) to ScrubPonyTheme.success
        FileOutcome.UNSUPPORTED -> stringResource(R.string.outcome_unsupported) to ScrubPonyTheme.dim
        FileOutcome.FAILED -> stringResource(R.string.outcome_failed) to ScrubPonyTheme.danger
    }
    val hasDetails = item.metadata.isNotEmpty()
    var expanded by remember { mutableStateOf(false) }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ScrubPonyTheme.panel, RoundedCornerShape(12.dp))
            .then(if (hasDetails) Modifier.clickable { expanded = !expanded } else Modifier)
            .padding(14.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                item.image.name,
                color = ScrubPonyTheme.ink,
                maxLines = 1,
                modifier = Modifier.weight(1f),
            )
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
                item.metadata.forEach { field ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                        verticalAlignment = Alignment.Top,
                    ) {
                        Text(
                            field.rawLabel ?: stringResource(metaLabelRes(field.key)),
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

@Composable
private fun DoneView(cleaned: Int, failed: Int, toSubfolder: Boolean, onClose: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            if (toSubfolder) {
                stringResource(R.string.bulk_done_subfolder, cleaned)
            } else {
                stringResource(R.string.bulk_done_inplace, cleaned)
            },
            style = MaterialTheme.typography.titleLarge,
            color = ScrubPonyTheme.ink,
            textAlign = TextAlign.Center,
        )
        if (failed > 0) {
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(R.string.bulk_done_failed, failed),
                style = MaterialTheme.typography.bodyMedium,
                color = ScrubPonyTheme.danger,
                textAlign = TextAlign.Center,
            )
        }
        Spacer(Modifier.height(24.dp))
        Primary(stringResource(R.string.action_close), onClick = onClose)
    }
}

@Composable
private fun Primary(text: String, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().height(52.dp),
        shape = RoundedCornerShape(14.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = ScrubPonyTheme.accent,
            contentColor = ScrubPonyTheme.onAccent,
        ),
    ) {
        Text(text, style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun Secondary(text: String, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().height(52.dp),
        shape = RoundedCornerShape(14.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, SolidColor(ScrubPonyTheme.line)),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = ScrubPonyTheme.ink),
    ) {
        Text(text, style = MaterialTheme.typography.titleMedium)
    }
}
