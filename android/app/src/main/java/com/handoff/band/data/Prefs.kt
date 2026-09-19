package com.handoff.band.data

import android.app.UiModeManager
import android.content.Context
import android.os.Build
import androidx.core.content.edit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The handful of settings the customer-facing app remembers. SharedPreferences
 * underneath, a StateFlow on top so a Compose screen reacts to a change made
 * on another screen without a restart.
 *
 * Pairing keeps its own file (`Pairing.kt`) because the service reads it
 * before any UI exists; everything a person chooses in Settings lives here.
 */
class Prefs private constructor(context: Context) {

    enum class Theme { SYSTEM, LIGHT, DARK }

    /** Newest first is the order you want right after a conference (§5). */
    enum class Sort { NEWEST, AZ }

    /**
     * The app's language. Presentational for now: every string in the app is
     * still hardcoded English in Kotlin, so nothing reads this and picking a
     * language changes nothing on screen. It is remembered so the row holds
     * what was chosen; making it real means lifting the UI strings into
     * `strings.xml`, translating them, and applying a per-app locale.
     */
    enum class Language { SYSTEM, ENGLISH, HINDI, SPANISH, GERMAN, FRENCH, JAPANESE }

    private val sp = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
    private val uiMode = context.getSystemService(Context.UI_MODE_SERVICE) as UiModeManager

    private val _theme = MutableStateFlow(
        sp.getString(KEY_THEME, null)?.let { runCatching { Theme.valueOf(it) }.getOrNull() }
            ?: Theme.SYSTEM
    )
    val theme: StateFlow<Theme> = _theme

    init { applyNightMode(_theme.value) }

    fun setTheme(t: Theme) {
        sp.edit { putString(KEY_THEME, t.name) }
        _theme.value = t
        applyNightMode(t)
    }

    /**
     * Compose reads [theme] directly, so the app itself never needed the
     * platform to know. The launch screen does: Android 12 draws it before
     * any of our code runs, from the resources of whatever night mode the
     * process is in, and without this a wearer who chose Dark on a light
     * phone got a light splash ahead of a dark app. The app-level override
     * is persisted by the system and applies from the next launch.
     */
    private fun applyNightMode(t: Theme) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return
        uiMode.setApplicationNightMode(when (t) {
            Theme.SYSTEM -> UiModeManager.MODE_NIGHT_AUTO
            Theme.LIGHT -> UiModeManager.MODE_NIGHT_NO
            Theme.DARK -> UiModeManager.MODE_NIGHT_YES
        })
    }

    /**
     * "The user turned the band off." The service is START_STICKY and both
     * Application.onCreate and MainActivity.onCreate restart it from the
     * stored address, so Disconnect without a persisted flag reconnects
     * instantly. Cleared by Connect and by Forget.
     */
    private val _bandOff = MutableStateFlow(sp.getBoolean(KEY_BAND_OFF, false))
    val bandOff: StateFlow<Boolean> = _bandOff

    fun setBandOff(off: Boolean) {
        sp.edit { putBoolean(KEY_BAND_OFF, off) }
        _bandOff.value = off
    }

    /**
     * Setup has been shown once. The first launch opens on it; after that,
     * paired or skipped, the app opens on home and "Pair a band" is the way
     * back in.
     */
    val setupOffered: Boolean get() = sp.getBoolean(KEY_SETUP_OFFERED, false)

    fun setSetupOffered() {
        sp.edit { putBoolean(KEY_SETUP_OFFERED, true) }
    }

    /** The wearer's own card, or null when none is set. */
    private val _ownCard = MutableStateFlow(readOwnCard())
    val ownCard: StateFlow<OwnCard?> = _ownCard

    fun setOwnCard(card: OwnCard?) {
        sp.edit {
            if (card == null) {
                CARD_STRINGS.forEach { remove(KEY_OWN_CARD + it) }
                remove(KEY_OWN_CARD)
            } else {
                val l = card.toList()
                putBoolean(KEY_OWN_CARD, true)
                CARD_STRINGS.forEachIndexed { i, k -> putString(KEY_OWN_CARD + k, l[i]) }
            }
        }
        _ownCard.value = card
    }

    private fun readOwnCard(): OwnCard? {
        if (!sp.getBoolean(KEY_OWN_CARD, false)) return null
        val card = OwnCard.fromList(CARD_STRINGS.map { sp.getString(KEY_OWN_CARD + it, "") ?: "" })

        // A card written before phones had labels (design decisions §4a) is
        // still in here as two keys. Read it once, in the shape it was left,
        // so the update does not blank the wearer's own numbers; the next save
        // writes it back as a labelled list.
        if (card != null && card.phones.isEmpty()) {
            val legacy = listOfNotNull(
                sp.getString(KEY_OWN_CARD + ".mobile", "")?.takeIf { it.isNotBlank() }
                    ?.let { Phone(it, PhoneLabel.MOBILE) },
                sp.getString(KEY_OWN_CARD + ".work", "")?.takeIf { it.isNotBlank() }
                    ?.let { Phone(it, PhoneLabel.WORK) },
            )
            if (legacy.isNotEmpty()) return card.copy(phones = legacy)
        }
        return card
    }

    /**
     * A hash of the last card the band acknowledged. The band reports whether
     * it holds a card but never which, so this is how the service tells "the
     * card on the band is the current one" from "it has some card".
     */
    private val _pushedCard = MutableStateFlow(sp.getString(KEY_PUSHED, null))
    val pushedCard: StateFlow<String?> = _pushedCard

    fun setPushedCard(hash: String?) {
        sp.edit { if (hash == null) remove(KEY_PUSHED) else putString(KEY_PUSHED, hash) }
        _pushedCard.value = hash
    }

    private val _sort = MutableStateFlow(
        sp.getString(KEY_SORT, null)?.let { runCatching { Sort.valueOf(it) }.getOrNull() }
            ?: Sort.NEWEST
    )
    val sort: StateFlow<Sort> = _sort

    fun setSort(s: Sort) {
        sp.edit { putString(KEY_SORT, s.name) }
        _sort.value = s
    }

    private val _language = MutableStateFlow(
        sp.getString(KEY_LANGUAGE, null)?.let { runCatching { Language.valueOf(it) }.getOrNull() }
            ?: Language.SYSTEM
    )
    val language: StateFlow<Language> = _language

    fun setLanguage(l: Language) {
        sp.edit { putString(KEY_LANGUAGE, l.name) }
        _language.value = l
    }

    companion object {
        private const val FILE = "handoff.prefs"
        private const val KEY_THEME = "theme"
        private const val KEY_SORT = "sort"
        private const val KEY_LANGUAGE = "language"
        private const val KEY_BAND_OFF = "band_off"
        private const val KEY_OWN_CARD = "own_card"
        private const val KEY_PUSHED = "pushed_card"
        private const val KEY_SETUP_OFFERED = "setup_offered"
        private val CARD_STRINGS = listOf(".name", ".phones", ".email", ".org", ".title")

        @Volatile private var instance: Prefs? = null

        fun get(context: Context): Prefs = instance ?: synchronized(this) {
            instance ?: Prefs(context.applicationContext).also { instance = it }
        }
    }
}
