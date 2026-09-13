package com.handoff.band.data

import android.content.Context
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

    private val sp = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    private val _theme = MutableStateFlow(
        sp.getString(KEY_THEME, null)?.let { runCatching { Theme.valueOf(it) }.getOrNull() }
            ?: Theme.SYSTEM
    )
    val theme: StateFlow<Theme> = _theme

    fun setTheme(t: Theme) {
        sp.edit { putString(KEY_THEME, t.name) }
        _theme.value = t
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
        return OwnCard.fromList(CARD_STRINGS.map { sp.getString(KEY_OWN_CARD + it, "") ?: "" })
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

    companion object {
        private const val FILE = "handoff.prefs"
        private const val KEY_THEME = "theme"
        private const val KEY_SORT = "sort"
        private const val KEY_BAND_OFF = "band_off"
        private const val KEY_OWN_CARD = "own_card"
        private const val KEY_PUSHED = "pushed_card"
        private val CARD_STRINGS = listOf(".name", ".mobile", ".work", ".email", ".org", ".title")

        @Volatile private var instance: Prefs? = null

        fun get(context: Context): Prefs = instance ?: synchronized(this) {
            instance ?: Prefs(context.applicationContext).also { instance = it }
        }
    }
}
