package com.norsehorse.scrubpony.ui.settings

import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.annotation.StringRes
import androidx.browser.customtabs.CustomTabsIntent
import androidx.core.net.toUri
import com.norsehorse.scrubpony.R

/**
 * Every external link the app exposes, taken from the current PGPony settings
 * set and pointed at ScrubPony. Centralised so the URLs live in one place.
 */
object Links {
    const val WEBSITE = "https://scrubpony.app"
    const val PRIVACY = "https://scrubpony.app/privacy.php"
    const val TERMS = "https://scrubpony.app/terms.php"
    const val GUIDES = "https://scrubpony.app/guides"
    const val APP_SOURCE = "https://github.com/norsehorse-dev/ScrubPonyAndroid"
    const val CORE_SOURCE = "https://github.com/norsehorse-dev/ScrubPony"
    const val PONY_FAMILY = "https://pony.norsehor.se"
    const val FEEDBACK_EMAIL = "norsehorse@norsehor.se"

    /** "More from NorseHorse" cross-promo — the family, minus ScrubPony itself. */
    val PONY_APPS: List<PonyApp> = listOf(
        PonyApp("Pony apps", R.string.more_ponyfamily_subtitle, PONY_FAMILY),
        PonyApp("PGPony", R.string.more_pgpony_subtitle, "https://pgpony.app"),
        PonyApp("AgePony", R.string.more_agepony_subtitle, "https://agepony.com"),
        PonyApp("QuorumPony", R.string.more_quorumpony_subtitle, "https://quorumpony.com"),
        PonyApp("CarrierPony", R.string.more_carrierpony_subtitle, "https://carrierpony.com"),
        PonyApp("BurnPony", R.string.more_burnpony_subtitle, "https://burnpony.app"),
        PonyApp("VaultPony", R.string.more_vaultpony_subtitle, "https://vaultpony.app"),
        PonyApp("PassPony", R.string.more_passpony_subtitle, "https://passpony.app"),
        PonyApp("RelayPony", R.string.more_relaypony_subtitle, "https://relaypony.app"),
    )
}

data class PonyApp(val name: String, @StringRes val subtitleRes: Int, val url: String)

/** Opens a URL in a Chrome Custom Tab, falling back to a plain view intent. */
fun openUrl(context: Context, url: String) {
    runCatching {
        CustomTabsIntent.Builder().setShowTitle(true).build()
            .launchUrl(context, url.toUri())
    }.onFailure {
        runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, url.toUri())) }
    }
}

/** Pre-filled feedback email, mirroring PGPony's FeedbackIntent. */
fun sendFeedback(context: Context, versionName: String) {
    val subject = "ScrubPony Android Feedback ($versionName)"
    val body = buildString {
        append("\n\n---\n")
        append("App: ScrubPony $versionName\n")
        append("Device: ${Build.MANUFACTURER} ${Build.MODEL}\n")
        append("Android: ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})")
    }
    val intent = Intent(Intent.ACTION_SENDTO).apply {
        data = "mailto:".toUri()
        putExtra(Intent.EXTRA_EMAIL, arrayOf(Links.FEEDBACK_EMAIL))
        putExtra(Intent.EXTRA_SUBJECT, subject)
        putExtra(Intent.EXTRA_TEXT, body)
    }
    runCatching { context.startActivity(intent) }
}

/** Opens the Play Store listing (market intent, no Play dependency). */
fun rateApp(context: Context) {
    val pkg = context.packageName
    val market = Intent(Intent.ACTION_VIEW, "market://details?id=$pkg".toUri())
    runCatching { context.startActivity(market) }.onFailure {
        openUrl(context, "https://play.google.com/store/apps/details?id=$pkg")
    }
}

/** Resolves the app's versionName without a hard BuildConfig dependency. */
fun appVersionName(context: Context): String =
    runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull() ?: "1.0"
