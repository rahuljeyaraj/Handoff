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
 * Settings: Contacts · Band · Appearance · About · Advanced.
 *
 * Your contact card and the band's own screen are one tap from home (the
 * nudge/Band row and the status line), so this screen does not repeat them
 * (review item 11). "Save to phone automatically" is gone for good — it
 * needed WRITE_CONTACTS and a provider insert, which the brief rules out
 * (review item 10).
 *
 * Vibrate reflects the band's own truth rather than the phone's memory of
 * it: [hapticOn] comes off `status`'s flags, and the band persists whatever
 * the switch sends through [onSetHaptic] (`BLE_CTRL_HAPTIC`), so a band that
 * reboots is back in step within a second of reconnecting (review O5).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    theme: Prefs.Theme,
    sort: Prefs.Sort,
    appVersion: String,
    hapticOn: Boolean,
    onTheme: (Prefs.Theme) -> Unit,
    onSort: (Prefs.Sort) -> Unit,
    onSetHaptic: (Boolean) -> Unit,
    onAdvanced: () -> Unit,
    onBack: () -> Unit,
) {
    var themeDialog by remember { mutableStateOf(false) }
    var sortDialog by remember { mutableStateOf(false) }

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
            SectionHeader("Contacts")
            SettingsRow("Sort order", icon = HandoffIcons.Sort, subtitle = sort.label,
                        onClick = { sortDialog = true })

            SectionHeader("Band")
            SettingsRow("Vibrate", icon = HandoffIcons.Vibrate, chevron = false,
                        trailing = { Switch(checked = hapticOn, onCheckedChange = onSetHaptic) })

            SectionHeader("Appearance")
            SettingsRow("Theme", icon = HandoffIcons.Theme, subtitle = theme.label,
                        onClick = { themeDialog = true })

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
    if (sortDialog) {
        ChoiceDialog("Sort order", Prefs.Sort.entries, sort, { it.label },
                     onPick = { onSort(it); sortDialog = false },
                     onDismiss = { sortDialog = false })
    }
}

val Prefs.Sort.label: String
    get() = when (this) {
        Prefs.Sort.NEWEST -> "Newest first"
        Prefs.Sort.AZ -> "A to Z"
    }

val Prefs.Theme.label: String
    get() = when (this) {
        Prefs.Theme.SYSTEM -> "System default"
        Prefs.Theme.LIGHT -> "Light"
        Prefs.Theme.DARK -> "Dark"
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
            Column {
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
