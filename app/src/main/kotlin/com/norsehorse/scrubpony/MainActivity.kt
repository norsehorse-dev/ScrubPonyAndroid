package com.norsehorse.scrubpony

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import com.norsehorse.scrubpony.ui.ScrubScreen
import java.io.File

class MainActivity : ComponentActivity() {

    private val viewModel: ScrubViewModel by viewModels()

    private val pickImages = registerForActivityResult(
        ActivityResultContracts.PickMultipleVisualMedia(50),
    ) { uris -> if (uris.isNotEmpty()) viewModel.scrub(uris) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        handleIncomingShare(intent)

        setContent {
            ScrubPonyTheme {
                ScrubScreen(
                    viewModel = viewModel,
                    onPickImages = {
                        pickImages.launch(
                            PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly),
                        )
                    },
                    onShareResults = { files -> shareResults(files) },
                    onSaveResults = { files -> saveResults(files) },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIncomingShare(intent)
    }

    /** Handles the app being opened via another app's share sheet
     *  ("Scrub metadata" for one photo or several — see the SEND /
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
}
