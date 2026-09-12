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
import androidx.compose.material.icons.filled.Person
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
import com.handoff.band.ui.BandView
import com.handoff.band.ui.components.BatteryIcon
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.SectionHeader
import com.handoff.band.ui.components.SettingsRow

/**
 * Settings: Your card · Band · Contacts · Appearance · About · Advanced.
 *
 * "Save to phone automatically" is drawn, off, and disabled: it needs
 * WRITE_CONTACTS and a provider insert, and is deliberately not built.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    band: BandView,
    cardSummary: String,
    theme: Prefs.Theme,
    appVersion: String,
    onTheme: (Prefs.Theme) -> Unit,
    onCard: () -> Unit,
    onBand: () -> Unit,
    onAdvanced: () -> Unit,
    onBack: () -> Unit,
) {
    var themeDialog by remember { mutableStateOf(false) }

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
            SectionHeader("Your card")
            SettingsRow("Your contact card", icon = Icons.Filled.Person,
                        subtitle = cardSummary, onClick = onCard)

            SectionHeader("Band")
            SettingsRow(
                if (band.paired) band.name else "No band paired",
                icon = HandoffIcons.Band,
                subtitle = band.connection.label,
                onClick = onBand,
                trailing = if (band.paired) ({ BatteryIcon(band.battery) }) else null,
            )

            SectionHeader("Contacts")
            SettingsRow("Save to phone automatically", icon = HandoffIcons.PhoneAdd,
                        chevron = false,
                        trailing = { Switch(checked = false, onCheckedChange = null, enabled = false) })

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
        ThemeDialog(theme, onPick = { onTheme(it); themeDialog = false },
                    onDismiss = { themeDialog = false })
    }
}

val Prefs.Theme.label: String
    get() = when (this) {
        Prefs.Theme.SYSTEM -> "System default"
        Prefs.Theme.LIGHT -> "Light"
        Prefs.Theme.DARK -> "Dark"
    }

@Composable
private fun ThemeDialog(current: Prefs.Theme, onPick: (Prefs.Theme) -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Theme") },
        text = {
            Column {
                for (t in Prefs.Theme.entries) {
                    Row(
                        Modifier.fillMaxWidth().clickable { onPick(t) }.padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = t == current, onClick = { onPick(t) })
                        Spacer(Modifier.width(8.dp))
                        Text(t.label, style = MaterialTheme.typography.bodyLarge)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
