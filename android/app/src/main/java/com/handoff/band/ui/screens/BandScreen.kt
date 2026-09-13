package com.handoff.band.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.TextButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.handoff.band.ui.BandView
import com.handoff.band.ui.components.BatteryIcon
import com.handoff.band.ui.components.ConnectionDot
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.SettingsRow

/**
 * The Band screen: name, connection, battery, firmware, your card, and the
 * two ways to let go of it. One band — the screen shows one, which is the
 * statement; forgetting it is how you swap (§6).
 *
 * Reachable only for a paired band (review item 12) — the status line on
 * home is a "Pair a band" button when there isn't one, and never opens here.
 *
 * No MAC address anywhere here. It is an identifier for us, not the wearer;
 * Advanced shows it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BandScreen(
    band: BandView,
    cardSummary: String,
    firmware: String?,
    notFoundAt: Long?,
    onDisconnect: () -> Unit,
    onForget: () -> Unit,
    onCard: () -> Unit,
    onBack: () -> Unit,
) {
    var confirmForget by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    // One-shot, like the home screen's incomplete-handshake snackbar: shown
    // once per timestamp, not on every recomposition (review item 15, O8).
    LaunchedEffect(notFoundAt) {
        if (notFoundAt != null && System.currentTimeMillis() - notFoundAt < 60_000) {
            snackbar.showSnackbar(band.statusLabel)
        }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        topBar = {
            TopAppBar(
                title = { Text("Band") },
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
            Hero(band)

            SettingsRow("Battery", subtitle = band.battery.label, chevron = false,
                        icon = null, trailing = { BatteryIcon(band.battery) })
            SettingsRow("Firmware", subtitle = firmware ?: "Not reported", chevron = false)
            SettingsRow("Your contact card", icon = Icons.Filled.Person,
                        subtitle = cardSummary, onClick = onCard)

            Spacer(Modifier.weight(1f))
            Spacer(Modifier.height(24.dp))

            // Two distinct actions, not one: drop the link and keep the
            // pairing, or forget the band altogether (§6).
            Column(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                OutlinedButton(onClick = onDisconnect, modifier = Modifier.fillMaxWidth()) {
                    Text(if (band.connection == BandView.Connection.OFF) "Connect" else "Disconnect")
                }
                TextButton(
                    onClick = { confirmForget = true },
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error),
                ) { Text("Forget this band") }
            }

            Spacer(Modifier.height(24.dp))
        }
    }

    if (confirmForget) {
        AlertDialog(
            onDismissRequest = { confirmForget = false },
            title = { Text("Forget ${band.name}?") },
            text = { Text("You'll need to pair it again to use it.") },
            confirmButton = {
                TextButton(onClick = { confirmForget = false; onForget() }) { Text("Forget") }
            },
            dismissButton = { TextButton(onClick = { confirmForget = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun Hero(band: BandView) {
    Column(
        Modifier
            .fillMaxWidth()
            .padding(start = 16.dp, end = 16.dp, top = 4.dp)
            .clip(RoundedCornerShape(20.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .padding(horizontal = 20.dp, vertical = 22.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(HandoffIcons.Band, contentDescription = null,
             tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(44.dp))
        Text(band.name, style = MaterialTheme.typography.titleLarge,
             modifier = Modifier.padding(top = 12.dp))
        Row(
            Modifier
                .padding(top = 14.dp)
                .height(32.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(MaterialTheme.colorScheme.primaryContainer)
                .padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            ConnectionDot(band.connection == BandView.Connection.CONNECTED)
            Text(band.statusLabel, style = MaterialTheme.typography.labelLarge,
                 color = MaterialTheme.colorScheme.onPrimaryContainer)
        }
        if (band.connection == BandView.Connection.NOT_FOUND) {
            Text("Switch it on and keep the phone close.",
                 style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 modifier = Modifier.padding(top = 10.dp))
        }
    }
}
