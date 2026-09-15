package com.handoff.band.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Gatt
import com.handoff.band.ble.Pairing
import com.handoff.band.data.OwnCard
import com.handoff.band.ui.LocalBand
import com.handoff.band.ui.components.SectionHeader
import com.handoff.band.ui.components.SettingsRow
import com.handoff.band.ui.theme.MonoStyle

/**
 * The bench tools, design decisions §11. They are M2 exit criteria, not debug
 * toys, so they stay in the build — behind a deliberate tap and a caption
 * that says some of them drop the connection.
 *
 * This is also the one place the MAC address and the raw `status` fields are
 * shown. Nothing here appears on the home screen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AdvancedScreen(state: BandService.State?, ownCard: OwnCard?, onBack: () -> Unit) {
    val band = LocalBand.current
    val context = LocalContext.current

    var floor by remember { mutableStateOf(band.service?.forceMtuFloor ?: false) }
    var scanNote by remember { mutableStateOf<String?>(null) }
    var scanning by remember { mutableStateOf<Pairing.Locate?>(null) }

    // A scan that outlives the row that started it leaks its filter slot the
    // same way a second tap used to (see debugScan) — leaving this screen
    // must end it, not just stop logging its results.
    DisposableEffect(Unit) { onDispose { scanning?.cancel(); scanning = null } }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Advanced") },
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
            Text(
                "Bench tools for bring-up tests. Nothing here is needed for normal use, " +
                    "and some of it will drop the connection.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
            )

            // The M2 exit criterion this exists for, spelled out: the chunking
            // has to work at 23 bytes, not at whatever this handset negotiates.
            SettingsRow(
                "Force the 23-byte MTU floor",
                subtitle = "Reconnects the band. Proves chunking at the ATT minimum.",
                chevron = false,
                trailing = {
                    Switch(checked = floor, onCheckedChange = {
                        floor = it
                        band.service?.forceMtuFloor = it
                    })
                },
            )

            HorizontalDivider(Modifier.padding(horizontal = 16.dp))

            SettingsRow(
                "Run a Bluetooth scan",
                subtitle = scanNote ?: "Ten seconds, unfiltered, every result logged.",
                chevron = false,
                onClick = {
                    scanning?.cancel()
                    scanNote = "scanning…"
                    scanning = Pairing.debugScan(context) { scanNote = it; scanning = null }
                },
            )

            // Development plan M2: the notify has to land with the screen off
            // and the app backgrounded. Ten seconds is enough to lock the
            // phone and put it down.
            SettingsRow(
                "Send a fake card in 10 s",
                subtitle = "Time to lock the phone and pocket it.",
                chevron = false,
                onClick = { band.service?.control(Gatt.fakeRx(10)) },
            )

            SettingsRow(
                "Erase the card on the band",
                subtitle = "Leaves the band receive-only.",
                chevron = false,
                onClick = { band.service?.control(Gatt.forget()) },
            )

            SectionHeader("Band status")
            StatusDump(state)

            // The bytes as written to the band. A developer's view, moved
            // here off the Your card screen where it wore a user's hat (§4a).
            ownCard?.let { card ->
                val text = card.vcard()
                Row(Modifier.fillMaxWidth().padding(end = 16.dp), verticalAlignment = androidx.compose.ui.Alignment.Bottom) {
                    SectionHeader("Card written to the band", Modifier.weight(1f))
                    Text("${text.toByteArray(Charsets.UTF_8).size} B", style = MonoStyle,
                         color = MaterialTheme.colorScheme.onSurfaceVariant,
                         modifier = Modifier.padding(bottom = 8.dp))
                }
                Text(text, style = MonoStyle,
                     modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }

            state?.lastIncompleteText?.let { raw ->
                SectionHeader("Last incomplete handshake")
                Text(
                    "Arrived with no phone and no email, so it was not stored as a contact.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
                Text(raw, style = MonoStyle,
                     modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))
            }

            Spacer(Modifier.height(24.dp))
        }
    }
}

@Composable
private fun StatusDump(state: BandService.State?) {
    val s = state?.status
    val rows = listOf(
        "address" to (state?.address ?: "—"),
        "link" to when {
            state == null -> "service not bound"
            state.ready -> "ready"
            state.connected -> "connecting"
            else -> "waiting"
        },
        "encrypted" to (s?.encrypted?.yesNo() ?: "—"),
        "provisioned" to (s?.provisioned?.yesNo() ?: "—"),
        "flash" to (s?.flashOk?.let { if (it) "ok" else "FAULT" } ?: "—"),
        "record id" to (s?.recordId?.toString() ?: "—"),
        "own card" to (s?.ownBlobLen?.let { "$it B" } ?: "—"),
        "chunk errors" to (s?.chunkErrors?.toString() ?: "—"),
        "frame errors" to (s?.frameErrors?.toString() ?: "—"),
        "link state" to (s?.linkState?.toString() ?: "—"),
        "status version" to (s?.version?.toString() ?: "—"),
        "vsys" to (s?.vsysMv?.let { "$it mV" } ?: "—"),
        "usb power" to (s?.let { it.usbPower.yesNo() } ?: "—"),
        "firmware" to (s?.firmware ?: "—"),
    )
    Column(Modifier.padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(0.dp)) {
        for ((k, v) in rows) {
            Row(Modifier.fillMaxWidth()) {
                Text(k, style = MonoStyle, color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.weight(1f))
                Text(v, style = MonoStyle)
            }
        }
        state?.lastError?.let {
            Text(it, style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.error,
                 modifier = Modifier.padding(top = 8.dp))
        }
    }
}

private fun Boolean.yesNo() = if (this) "yes" else "no"
