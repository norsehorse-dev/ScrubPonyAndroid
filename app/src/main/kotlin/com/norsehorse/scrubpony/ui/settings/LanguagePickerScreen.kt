@file:OptIn(ExperimentalMaterial3Api::class)

package com.norsehorse.scrubpony.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.ScrubPonyTheme
import com.norsehorse.scrubpony.i18n.SupportedLanguage

/**
 * Language chooser, presented as a bottom sheet so it floats above whatever
 * the Settings tab is showing without depending on that screen's layout.
 * Picking a language applies it immediately (see LanguageManager); the
 * activity usually recreates itself right after to pick up the new locale,
 * which is why we close the sheet as soon as a row is tapped.
 */
@Composable
fun LanguagePickerScreen(
    current: SupportedLanguage,
    onPick: (SupportedLanguage) -> Unit,
    onClose: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    ModalBottomSheet(
        onDismissRequest = onClose,
        sheetState = sheetState,
        containerColor = ScrubPonyTheme.panelHigh,
        dragHandle = null,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(start = 20.dp, end = 20.dp, top = 12.dp, bottom = 28.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                stringResource(R.string.language_sheet_title),
                style = MaterialTheme.typography.titleLarge,
                color = ScrubPonyTheme.ink,
                modifier = Modifier.padding(bottom = 8.dp),
            )
            SupportedLanguage.entries.forEach { lang ->
                LanguageRow(
                    label = lang.nativeName,
                    selected = lang == current,
                    onClick = {
                        onPick(lang)
                        onClose()
                    },
                )
            }
        }
    }
}

@Composable
private fun LanguageRow(label: String, selected: Boolean, onClick: () -> Unit) {
    val background = if (selected) ScrubPonyTheme.field else ScrubPonyTheme.panelHigh
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .background(background, RoundedCornerShape(12.dp))
            .padding(horizontal = 14.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            color = if (selected) ScrubPonyTheme.ink else ScrubPonyTheme.dim,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
            modifier = Modifier.weight(1f),
        )
        if (selected) {
            Text("✓", color = ScrubPonyTheme.accentBright)
        } else {
            Spacer(Modifier.size(1.dp))
        }
    }
}
