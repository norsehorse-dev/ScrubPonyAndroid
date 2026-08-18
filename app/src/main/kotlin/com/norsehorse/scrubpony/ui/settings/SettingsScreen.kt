package com.norsehorse.scrubpony.ui.settings

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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.ScrubPonyTheme
import com.norsehorse.scrubpony.i18n.LanguageManager

/**
 * The Settings tab. Same shape as the sibling apps' settings: stacked labelled
 * panels, every external link taken from the current PGPony set and pointed at
 * ScrubPony. Legal and support links open in a Custom Tab; the toggles drive
 * the same scrub options the main screen exposes.
 */
@Composable
fun SettingsScreen(
    keepOrientation: Boolean,
    onKeepOrientationChange: (Boolean) -> Unit,
    strict: Boolean,
    onStrictChange: (Boolean) -> Unit,
    onReplayOnboarding: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var showLanguagePicker by remember { mutableStateOf(false) }
    var showLicenses by remember { mutableStateOf(false) }
    val version = remember { appVersionName(context) }
    val language = LanguageManager.current()

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ScrubPonyTheme.background)
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp),
    ) {
        ScreenTitle(stringResource(R.string.settings_title))

        // How it works
        SectionPanel(stringResource(R.string.settings_how_header)) {
            BodyText(stringResource(R.string.settings_how_body))
            DimText(stringResource(R.string.settings_limits_body))
        }

        // Scrub options
        SectionPanel(stringResource(R.string.settings_options_header)) {
            ToggleRow(
                title = stringResource(R.string.option_keep_orientation),
                subtitle = stringResource(R.string.option_keep_orientation_sub),
                checked = keepOrientation,
                onCheckedChange = onKeepOrientationChange,
            )
            Divider()
            ToggleRow(
                title = stringResource(R.string.option_strict),
                subtitle = stringResource(R.string.option_strict_sub),
                checked = strict,
                onCheckedChange = onStrictChange,
            )
        }

        // Appearance
        SectionPanel(stringResource(R.string.settings_appearance_header)) {
            ActionRow(
                title = stringResource(R.string.settings_language),
                value = language.nativeName,
                onClick = { showLanguagePicker = true },
            )
        }

        // More from NorseHorse
        SectionPanel(stringResource(R.string.settings_more_header)) {
            Links.PONY_APPS.forEachIndexed { i, pony ->
                if (i > 0) Divider()
                TwoLineRow(
                    title = pony.name,
                    subtitle = stringResource(pony.subtitleRes),
                    external = true,
                    onClick = { openUrl(context, pony.url) },
                )
            }
            Divider()
            TwoLineRow(
                title = stringResource(R.string.settings_app_source),
                subtitle = "github.com/norsehorse-dev/ScrubPonyAndroid",
                external = true,
                onClick = { openUrl(context, Links.APP_SOURCE) },
            )
            Divider()
            TwoLineRow(
                title = stringResource(R.string.settings_core_source),
                subtitle = "github.com/norsehorse-dev/ScrubPony",
                external = true,
                onClick = { openUrl(context, Links.CORE_SOURCE) },
            )
        }

        // Support
        SectionPanel(stringResource(R.string.settings_support_header)) {
            LinkRow(stringResource(R.string.settings_website)) { openUrl(context, Links.WEBSITE) }
            Divider()
            LinkRow(stringResource(R.string.settings_guides)) { openUrl(context, Links.GUIDES) }
            Divider()
            LinkRow(stringResource(R.string.settings_feedback)) { sendFeedback(context, version) }
            Divider()
            LinkRow(stringResource(R.string.settings_rate)) { rateApp(context) }
            Divider()
            LinkRow(stringResource(R.string.settings_privacy)) { openUrl(context, Links.PRIVACY) }
            Divider()
            LinkRow(stringResource(R.string.settings_terms)) { openUrl(context, Links.TERMS) }
        }

        // About
        SectionPanel(stringResource(R.string.settings_about_header)) {
            Row(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                Text(stringResource(R.string.settings_version), color = ScrubPonyTheme.ink)
                Spacer(Modifier.weight(1f))
                Text(version, style = ScrubPonyMonoStyle(), color = ScrubPonyTheme.dim)
            }
            Divider()
            LinkRow(stringResource(R.string.settings_licenses), accent = false) { showLicenses = true }
            Divider()
            LinkRow(stringResource(R.string.settings_replay_tour), accent = false) { onReplayOnboarding() }
        }

        Text(
            stringResource(R.string.settings_family_footer),
            style = MaterialTheme.typography.bodySmall,
            color = ScrubPonyTheme.dim,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
        )
        Spacer(Modifier.padding(8.dp))
    }

    if (showLanguagePicker) {
        LanguagePickerScreen(
            current = language,
            onPick = { LanguageManager.setLanguage(it) },
            onClose = { showLanguagePicker = false },
        )
    }
    if (showLicenses) {
        LicensesScreen(onClose = { showLicenses = false })
    }
}

// --- shared building blocks (kept private to the settings package) --------

@Composable
private fun ScreenTitle(text: String) {
    Text(text, style = MaterialTheme.typography.headlineMedium, color = ScrubPonyTheme.ink)
}

@Composable
internal fun SectionPanel(header: String?, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (header != null) {
            Text(
                header.uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = ScrubPonyTheme.dim,
                modifier = Modifier.padding(start = 6.dp),
            )
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(ScrubPonyTheme.panel, RoundedCornerShape(14.dp))
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) { content() }
    }
}

@Composable
private fun Divider() {
    Spacer(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp)
            .height(1.dp)
            .background(ScrubPonyTheme.line),
    )
}

@Composable
private fun BodyText(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = ScrubPonyTheme.ink)
}

@Composable
private fun DimText(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = ScrubPonyTheme.dim)
}

@Composable
private fun ScrubPonyMonoStyle() =
    MaterialTheme.typography.bodySmall.copy(fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)

@Composable
private fun LinkRow(text: String, accent: Boolean = true, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, color = if (accent) ScrubPonyTheme.accent else ScrubPonyTheme.ink)
        Spacer(Modifier.weight(1f))
        Text("↗", color = ScrubPonyTheme.dim)
    }
}

@Composable
private fun TwoLineRow(title: String, subtitle: String, external: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, color = ScrubPonyTheme.ink)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = ScrubPonyTheme.dim)
        }
        if (external) Text("↗", color = ScrubPonyTheme.dim)
    }
}

@Composable
private fun ActionRow(title: String, value: String, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, color = ScrubPonyTheme.ink)
        Spacer(Modifier.weight(1f))
        Text(value, color = ScrubPonyTheme.dim)
        Spacer(Modifier.padding(2.dp))
        Text("›", color = ScrubPonyTheme.dim)
    }
}

@Composable
private fun ToggleRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, color = ScrubPonyTheme.ink)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = ScrubPonyTheme.dim)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
