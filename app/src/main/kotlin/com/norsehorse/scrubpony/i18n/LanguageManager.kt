package com.norsehorse.scrubpony.i18n

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.compose.runtime.mutableStateOf
import androidx.core.os.LocaleListCompat

/**
 * In-app language switching, mirroring the pattern the sibling apps use.
 *
 * The picked language is applied through AppCompatDelegate.setApplicationLocales,
 * which owns persistence (system per-app language on API 33+, AppCompat's own
 * store via AppLocalesMetadataHolderService below that). We keep a small
 * process-wide observable so Compose gets an instant checkmark update, since a
 * bare locale read does not trigger recomposition.
 */
enum class SupportedLanguage(val tag: String, val nativeName: String) {
    EN("en", "English"),
    DE("de", "Deutsch"),
    ES("es", "Español"),
    FR("fr", "Français"),
    JA("ja", "日本語"),
    PT_BR("pt-BR", "Português (Brasil)");

    companion object {
        fun fromTag(tag: String?): SupportedLanguage {
            if (tag.isNullOrBlank()) return EN
            entries.firstOrNull { it.tag.equals(tag, ignoreCase = true) }?.let { return it }
            val base = tag.substringBefore('-').lowercase()
            return entries.firstOrNull { it.tag.substringBefore('-').lowercase() == base } ?: EN
        }
    }
}

/** Observable current-language tag, for reactive UI. */
object LanguageState {
    val current = mutableStateOf(SupportedLanguage.EN.tag)

    fun initFromAppCompat(context: Context) {
        val locales = AppCompatDelegate.getApplicationLocales()
        current.value = if (!locales.isEmpty) {
            SupportedLanguage.fromTag(locales.toLanguageTags()).tag
        } else {
            detectInitialLanguage(context).tag
        }
    }

    private fun detectInitialLanguage(context: Context): SupportedLanguage {
        val sys = context.resources.configuration.locales
        if (sys.isEmpty) return SupportedLanguage.EN
        return SupportedLanguage.fromTag(sys[0].toLanguageTag())
    }
}

object LanguageManager {
    fun current(): SupportedLanguage = SupportedLanguage.fromTag(LanguageState.current.value)

    fun setLanguage(lang: SupportedLanguage) {
        LanguageState.current.value = lang.tag
        AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(lang.tag))
    }
}
