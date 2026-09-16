package com.handoff.band.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.handoff.band.data.Prefs
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.SectionHeader
import com.handoff.band.ui.components.SettingsRow

/**
 * Settings: the four things about the app itself, then About.
 *
 * Nothing here belongs to a contact or to the band. Sort order went back to
 * the list it sorts — the toolbar control on home is the only one now — and
 * Vibrate went to the Band screen, which is where the band's own switches
 * live. Your contact card and the band are both one tap from home, so this
 * screen does not repeat them either (review item 11). "Save to phone
 * automatically" is gone for good: it needed WRITE_CONTACTS and a provider
 * insert, which the brief rules out (review item 10).
 *
 * The rows are in the order a wearer needs them. Notifications first —
 * missing the notice that a card arrived is the one setting that loses
 * something — then Theme, which is the one people actually change; Language
 * and analytics are set once, if ever, and sit below.
 *
 * Notifications opens Android's own screen for this app rather than
 * mirroring its switches: the app posts two kinds of notice (the foreground
 * service's, and a handshake's) and the system is where they are turned
 * down. [language] and [analyticsOn] are remembered but do nothing yet —
 * see `Prefs.Language` and `Prefs.analytics` for what each one still needs.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    theme: Prefs.Theme,
    language: Prefs.Language,
    analyticsOn: Boolean,
    appVersion: String,
    onTheme: (Prefs.Theme) -> Unit,
    onLanguage: (Prefs.Language) -> Unit,
    onAnalytics: (Boolean) -> Unit,
    onNotifications: () -> Unit,
    onAdvanced: () -> Unit,
    onBack: () -> Unit,
) {
    var themeDialog by remember { mutableStateOf(false) }
    var languageDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState())) {
            SectionHeader("App")
            SettingsRow("Notifications", icon = HandoffIcons.Bell,
                        subtitle = "Alerts and the ongoing band notice",
                        onClick = onNotifications)
            SettingsRow("Theme", icon = HandoffIcons.Theme, subtitle = theme.label,
                        onClick = { themeDialog = true })
            SettingsRow("Language", icon = HandoffIcons.Globe, subtitle = language.label,
                        onClick = { languageDialog = true })
            SettingsRow("Allow app analytics", icon = HandoffIcons.Chart,
                        subtitle = "Anonymous usage stats", chevron = false,
                        trailing = { Switch(checked = analyticsOn, onCheckedChange = onAnalytics) })

            SectionHeader("About")
            SettingsRow("App version", icon = Icons.Filled.Info, subtitle = appVersion,
                        chevron = false)

            HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

            SettingsRow("Advanced", icon = HandoffIcons.Advanced,
                        subtitle = "Bench and diagnostic tools", onClick = onAdvanced)

            Spacer(Modifier.height(24.dp))
        }
    }

    if (themeDialog) {
        ChoiceDialog("Theme", Prefs.Theme.entries, theme, { it.label },
                     onPick = { onTheme(it); themeDialog = false },
                     onDismiss = { themeDialog = false })
    }
    if (languageDialog) {
        ChoiceDialog("Language", Prefs.Language.entries, language, { it.label },
                     onPick = { onLanguage(it); languageDialog = false },
                     onDismiss = { languageDialog = false })
    }
}

val Prefs.Theme.label: String
    get() = when (this) {
        Prefs.Theme.SYSTEM -> "System default"
        Prefs.Theme.LIGHT -> "Light"
        Prefs.Theme.DARK -> "Dark"
    }

/** Each language in its own script, the way every other app lists them. */
val Prefs.Language.label: String
    get() = when (this) {
        Prefs.Language.SYSTEM -> "System default"
        Prefs.Language.ENGLISH -> "English"
        Prefs.Language.HINDI -> "हिन्दी"
        Prefs.Language.SPANISH -> "Español"
        Prefs.Language.GERMAN -> "Deutsch"
        Prefs.Language.FRENCH -> "Français"
        Prefs.Language.JAPANESE -> "日本語"
    }

@Composable
private fun <T> ChoiceDialog(
    title: String,
    options: List<T>,
    current: T,
    label: (T) -> String,
    onPick: (T) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                for (t in options) {
                    Row(
                        Modifier.fillMaxWidth().clickable { onPick(t) }.padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = t == current, onClick = { onPick(t) })
                        Spacer(Modifier.width(8.dp))
                        Text(label(t), style = MaterialTheme.typography.bodyLarge)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
