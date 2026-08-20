@file:OptIn(ExperimentalMaterial3Api::class)

package com.norsehorse.scrubpony

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import com.norsehorse.scrubpony.i18n.LanguageState
import com.norsehorse.scrubpony.ui.ScrubScreen
import com.norsehorse.scrubpony.ui.bulk.BulkCleanOverlay
import com.norsehorse.scrubpony.ui.onboarding.OnboardingScreen
import com.norsehorse.scrubpony.ui.settings.SettingsScreen
import java.io.File

class MainActivity : AppCompatActivity() {

    private val viewModel: ScrubViewModel by viewModels()

    private val bulkVm: BulkCleanViewModel by viewModels()

    private val pickImages = registerForActivityResult(
        ActivityResultContracts.PickMultipleVisualMedia(50),
    ) { uris -> if (uris.isNotEmpty()) viewModel.scrub(uris) }

    // The system photo picker only surfaces "visual media" the gallery knows
    // about, which is why HEICs sitting in Files often do not appear. The
    // document picker (Storage Access Framework) browses Files, Downloads, an
    // SD card, a synced cloud folder, anything, filtered to images. The read
    // grant it hands back is short-lived, but ScrubEngine copies each input
    // into cache immediately, so that is long enough.
    private val pickDocuments = registerForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments(),
    ) { uris -> if (uris.isNotEmpty()) viewModel.scrub(uris) }

    // OpenDocumentTree takes no input and hands back a folder Uri on success
    // (null if the user backed out), so the files it should save are stashed
    // here between the launch call and the picker's result arriving.
    private var pendingSaveToFiles: List<File> = emptyList()

    private val pickSaveFolder = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree(),
    ) { treeUri -> if (treeUri != null) saveToFiles(treeUri, pendingSaveToFiles) }

    // Bulk clean: pick a folder, scan it for images that still carry metadata,
    // then clean them in one pass. OpenDocumentTree grants scoped read/write to
    // just that tree, so no broad storage permission is needed.
    private val pickCleanFolder = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree(),
    ) { treeUri -> if (treeUri != null) bulkVm.scan(treeUri, viewModel.strict, viewModel.keepOrientation) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Reflect the stored per-app language into the observable used by the
        // Settings picker, so the checkmark is right on first open.
        LanguageState.initFromAppCompat(this)

        val launchedFromShare = isShareIntent(intent)
        handleIncomingShare(intent)

        setContent {
            ScrubPonyTheme {
                AppRoot(
                    viewModel = viewModel,
                    launchedFromShare = launchedFromShare,
                    onPickImages = {
                        pickImages.launch(
                            PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly),
                        )
                    },
                    onPickFiles = { pickDocuments.launch(arrayOf("image/*")) },
                    onShareResults = { files -> shareResults(files) },
                    onSaveResults = { files -> saveResults(files) },
                    onSaveToFiles = { files ->
                        pendingSaveToFiles = files
                        pickSaveFolder.launch(null)
                    },
                    onCleanFolder = { pickCleanFolder.launch(null) },
                )
                BulkCleanOverlay(bulkVm)
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIncomingShare(intent)
    }

    private fun isShareIntent(intent: Intent?): Boolean =
        intent?.action == Intent.ACTION_SEND || intent?.action == Intent.ACTION_SEND_MULTIPLE

    /** Handles the app being opened via another app's share sheet
     *  ("Scrub metadata" for one photo or several, see the SEND /
     *  SEND_MULTIPLE intent filters in AndroidManifest.xml). */
    private fun handleIncomingShare(intent: Intent?) {
        if (intent == null) return
        val uris: List<Uri> = when (intent.action) {
            Intent.ACTION_SEND -> listOfNotNull(extraStreamUri(intent))
            Intent.ACTION_SEND_MULTIPLE -> extraStreamUriList(intent)
            else -> emptyList()
        }
        if (uris.isNotEmpty()) viewModel.scrub(uris)
    }

    @Suppress("DEPRECATION")
    private fun extraStreamUri(intent: Intent): Uri? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        } else {
            intent.getParcelableExtra(Intent.EXTRA_STREAM)
        }

    @Suppress("DEPRECATION")
    private fun extraStreamUriList(intent: Intent): List<Uri> =
        (
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)
            } else {
                intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM)
            }
        ) ?: emptyList()

    private fun shareResults(files: List<File>) {
        if (files.isEmpty()) return
        val intent = SaveExporter.buildShareIntent(this, files)
        startActivity(Intent.createChooser(intent, getString(R.string.share_chooser_title)))
    }

    private fun saveResults(files: List<File>) {
        files.forEach { SaveExporter.saveToGallery(this, it, it.name) }
    }

    private fun saveToFiles(treeUri: Uri, files: List<File>) {
        if (files.isEmpty()) return
        SaveExporter.saveToTree(this, treeUri, files)
    }
}

private const val PREFS = "scrubpony_prefs"
private const val KEY_ONBOARDING_SEEN = "onboarding_seen"

private fun onboardingSeen(context: Context): Boolean =
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_ONBOARDING_SEEN, false)

private fun markOnboardingSeen(context: Context) {
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .edit().putBoolean(KEY_ONBOARDING_SEEN, true).apply()
}

private enum class Tab { SCRUB, SETTINGS }

@Composable
private fun AppRoot(
    viewModel: ScrubViewModel,
    launchedFromShare: Boolean,
    onPickImages: () -> Unit,
    onPickFiles: () -> Unit,
    onShareResults: (List<File>) -> Unit,
    onSaveResults: (List<File>) -> Unit,
    onSaveToFiles: (List<File>) -> Unit,
    onCleanFolder: () -> Unit,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val alreadySeen = remember { onboardingSeen(context) }
    var showOnboarding by rememberSaveable { mutableStateOf(!alreadySeen && !launchedFromShare) }

    if (showOnboarding) {
        OnboardingScreen(
            onFinish = {
                markOnboardingSeen(context)
                showOnboarding = false
            },
        )
        return
    }

    var tab by rememberSaveable { mutableStateOf(Tab.SCRUB) }

    Scaffold(
        containerColor = ScrubPonyTheme.background,
        bottomBar = {
            NavigationBar(containerColor = ScrubPonyTheme.panel) {
                NavItem(
                    selected = tab == Tab.SCRUB,
                    onClick = { tab = Tab.SCRUB },
                    icon = Icons.Filled.AutoFixHigh,
                    label = stringResource(R.string.nav_scrub),
                )
                NavItem(
                    selected = tab == Tab.SETTINGS,
                    onClick = { tab = Tab.SETTINGS },
                    icon = Icons.Filled.Settings,
                    label = stringResource(R.string.nav_settings),
                )
            }
        },
    ) { inner ->
        when (tab) {
            Tab.SCRUB -> ScrubScreen(
                viewModel = viewModel,
                onPickImages = onPickImages,
                onPickFiles = onPickFiles,
                onShareResults = onShareResults,
                onSaveResults = onSaveResults,
                onSaveToFiles = onSaveToFiles,
                onCleanFolder = onCleanFolder,
                modifier = Modifier.padding(inner),
            )
            Tab.SETTINGS -> SettingsScreen(
                keepOrientation = viewModel.keepOrientation,
                onKeepOrientationChange = { viewModel.keepOrientation = it },
                strict = viewModel.strict,
                onStrictChange = { viewModel.strict = it },
                onReplayOnboarding = {
                    tab = Tab.SCRUB
                    showOnboarding = true
                },
                modifier = Modifier.padding(inner),
            )
        }
    }
}

@Composable
private fun androidx.compose.foundation.layout.RowScope.NavItem(
    selected: Boolean,
    onClick: () -> Unit,
    icon: ImageVector,
    label: String,
) {
    NavigationBarItem(
        selected = selected,
        onClick = onClick,
        icon = { Icon(icon, contentDescription = label) },
        label = { Text(label) },
        colors = NavigationBarItemDefaults.colors(
            selectedIconColor = ScrubPonyTheme.onAccent,
            selectedTextColor = ScrubPonyTheme.ink,
            indicatorColor = ScrubPonyTheme.accent,
            unselectedIconColor = ScrubPonyTheme.dim,
            unselectedTextColor = ScrubPonyTheme.dim,
        ),
    )
}
