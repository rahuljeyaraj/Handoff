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

    companion object {
        private const val FILE = "handoff.prefs"
        private const val KEY_THEME = "theme"

        @Volatile private var instance: Prefs? = null

        fun get(context: Context): Prefs = instance ?: synchronized(this) {
            instance ?: Prefs(context.applicationContext).also { instance = it }
        }
    }
}
