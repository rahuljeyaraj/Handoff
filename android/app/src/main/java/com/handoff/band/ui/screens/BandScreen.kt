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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
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
import com.handoff.band.ui.Notice
import com.handoff.band.ui.components.BatteryIcon
import com.handoff.band.ui.components.ConnectionDot
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.HandoffSnackbarHost
import com.handoff.band.ui.components.SettingsRow
import com.handoff.band.ui.theme.semantic

/**
 * The Band screen: name, connection, your card, battery, the two things you
 * ask of the band, what it is running, and the two ways to let go of it. One
 * band — the screen shows one, which is the statement; forgetting it is how
 * you swap (§6).
 *
 * The rows are in the order they matter to a wearer: the card is what the
 * band is for, the battery decides whether it works today, Find my band is
 * the thing you need the moment you need it, Vibrate is set once, and the
 * firmware version is there to be read, never acted on.
 *
 * Vibrate was in Settings, which was the wrong page for it: it is not a
 * setting of this app but a switch on the band, and it belongs beside the
 * band's other switches. It reflects the band's own truth rather than the
 * phone's memory of it — [hapticOn] comes off `status`'s flags, and the band
 * persists whatever the switch sends through [onSetHaptic]
 * (`BLE_CTRL_HAPTIC`), so a band that reboots is back in step within a second
 * of reconnecting (review O5). A disconnected band cannot be told anything,
 * so the switch says so instead of pretending.
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
    cardSet: Boolean,
    cardSummary: String,
    firmware: String?,
    hapticOn: Boolean,
    notFoundAt: Long?,
    cardSaved: Notice?,
    onDisconnect: () -> Unit,
    onIdentify: () -> Unit,
    onSetHaptic: (Boolean) -> Unit,
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

    // Back from Save on the card editor: where the card is going.
    LaunchedEffect(cardSaved) {
        cardSaved?.take()?.let { snackbar.showSnackbar(it) }
    }

    Scaffold(
        snackbarHost = { HandoffSnackbarHost(snackbar) },
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

            // The card first: it is the one row here you act on, and its
            // glyph is the home page's — green card once one is set, the
            // slashed card in red until then.
            SettingsRow("Your contact card",
                        icon = if (cardSet) HandoffIcons.Card else HandoffIcons.CardOff,
                        iconTint = if (cardSet) MaterialTheme.semantic.ok
                                   else MaterialTheme.colorScheme.error,
                        subtitle = cardSummary, onClick = onCard)
            SettingsRow("Battery", subtitle = band.battery.label, chevron = false,
                        leading = { BatteryIcon(band.battery) })

            // Find my band: the band answers with its own LED and motor
            // (BLE_CTRL_IDENTIFY), so there is nothing to confirm here. Only
            // a connected band can be asked; the row says so otherwise.
            val connected = band.connection == BandView.Connection.CONNECTED
            SettingsRow("Find my band", icon = HandoffIcons.Locate,
                        subtitle = if (connected) "Flashes and buzzes the band"
                                   else "Connect the band first",
                        onClick = if (connected) onIdentify else null,
                        chevron = false)
            SettingsRow("Vibrate", icon = HandoffIcons.Vibrate,
                        subtitle = if (connected) "Buzzes on a card shared or received"
                                   else "Connect the band first",
                        chevron = false,
                        trailing = {
                            Switch(checked = hapticOn, onCheckedChange = onSetHaptic,
                                   enabled = connected)
                        })

            SettingsRow("Firmware", icon = HandoffIcons.Chip,
                        subtitle = firmware ?: "Not reported", chevron = false)

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
            Text(band.connection.label, style = MaterialTheme.typography.labelLarge,
                 color = MaterialTheme.colorScheme.onPrimaryContainer)
        }
        val hint = when (band.connection) {
            BandView.Connection.NOT_FOUND -> "Switch it on and keep the phone close."
            BandView.Connection.BLUETOOTH_OFF -> "Turn on Bluetooth to reconnect."
            else -> null
        }
        if (hint != null) {
            Text(hint, style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 modifier = Modifier.padding(top = 10.dp))
        }
    }
}
