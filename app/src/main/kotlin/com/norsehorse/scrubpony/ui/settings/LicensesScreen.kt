package com.norsehorse.scrubpony.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.ScrubPonyTheme

/**
 * Third-party notices, shown as a full-screen dialog. ScrubPony's own image
 * core is the project's, under the same license as the two repositories the
 * Settings screen links to; the entries below are the libraries the Android
 * shell links against, each under the Apache License 2.0.
 */
private data class Notice(val name: String, val copyright: String, val license: String)

private val NOTICES = listOf(
    Notice("Jetpack Compose", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("AndroidX Core, Activity, Lifecycle", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("AndroidX AppCompat", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("AndroidX Browser (Custom Tabs)", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("AndroidX DocumentFile", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("Material Components / Material 3", "Copyright The Android Open Source Project", "Apache License 2.0"),
    Notice("Kotlin Standard Library & Coroutines", "Copyright JetBrains s.r.o.", "Apache License 2.0"),
)

@Composable
fun LicensesScreen(onClose: () -> Unit) {
    Dialog(
        onDismissRequest = onClose,
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = ScrubPonyTheme.background,
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        stringResource(R.string.settings_licenses),
                        style = MaterialTheme.typography.headlineMedium,
                        color = ScrubPonyTheme.ink,
                        modifier = Modifier.weight(1f),
                    )
                    Text(
                        stringResource(R.string.action_close),
                        color = ScrubPonyTheme.accent,
                        modifier = Modifier
                            .clickable(onClick = onClose)
                            .padding(8.dp),
                    )
                }

                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(start = 16.dp, end = 16.dp, bottom = 32.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Text(
                        stringResource(R.string.licenses_intro),
                        style = MaterialTheme.typography.bodyMedium,
                        color = ScrubPonyTheme.dim,
                        modifier = Modifier.padding(bottom = 4.dp),
                    )
                    NOTICES.forEach { NoticeCard(it) }
                    Text(
                        stringResource(R.string.licenses_apache_note),
                        style = MaterialTheme.typography.bodySmall,
                        color = ScrubPonyTheme.dim,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun NoticeCard(notice: Notice) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ScrubPonyTheme.panel, RoundedCornerShape(12.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        Text(notice.name, color = ScrubPonyTheme.ink)
        Text(notice.copyright, style = MaterialTheme.typography.bodySmall, color = ScrubPonyTheme.dim)
        Text(notice.license, style = MaterialTheme.typography.bodySmall, color = ScrubPonyTheme.accent)
    }
}
