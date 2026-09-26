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
import com.handoff.band.ble.BenchTrace
import com.handoff.band.ble.Gatt
import com.handoff.band.ble.Pairing
import com.handoff.band.data.OwnCard
import com.handoff.band.ui.LocalBand
import com.handoff.band.ui.components.BenchChart
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

            SectionHeader("Body link")
            BenchDump(state)
            Spacer(Modifier.height(8.dp))
            BenchChart(state?.benchTrace ?: BenchTrace.EMPTY)

            SectionHeader("Rendezvous")
            TrigDump(state)

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

/**
 * The body link, live, twice a second — the numbers the console prints for `s`,
 * arriving by radio instead.
 *
 * This is the whole point of the section: design §13 forbids a USB tether to a
 * mains-powered PC while anyone touches an electrode, and the 24 Sep 2026 bench
 * proved a tethered reading is also WRONG — both bands share the PC ground and
 * that wire is the return path under test, so the detector read a signal while
 * zero frames decoded. To measure the link you must read it from a floating
 * band, which means reading it here.
 *
 * WHAT THESE ARE SINCE LINK V2. There is no carrier floor, no gate, no ratio
 * and no min_delta in this design any more — step 5 deleted the detector that
 * had them. The pair at the top is CFAR: the louder tone bin against the mean
 * of the guard bins, both measured in the same windows through the same body,
 * and the band called the channel busy when the first cleared k times the
 * second. So the row that matters is signal against THRESHOLD, and the page
 * prints the threshold rather than leaving it to be inferred.
 */
@Composable
private fun BenchDump(state: BandService.State?) {
    val b = state?.bench
    if (b == null) {
        Text(
            "Nothing yet. The band pushes these once the app is connected; " +
                "they stop when it disconnects. A band on link v1 firmware " +
                "sends an older block and is deliberately ignored.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        return
    }

    val trig = state.trig
    val rows = listOf(
        // Three numbers, not two: the reference alone does not say what the
        // detector wanted, and k is not a number anyone should be doing in
        // their head at the bench.
        "signal" to "${b.signal}",
        "noise ref" to "${b.noiseRef}",
        "busy above" to "${b.threshold}",
        // The peak is the only level reading that survives this sample rate,
        // so it sits with the instant it is meant to be read against.
        "peak signal" to (trig?.let {
            "${it.peakSignal} of ${it.peakThreshold}" +
                if (it.peakHeard) " — heard" else ""
        } ?: "—"),
        "busy now" to (if (b.present) "yes" else "no"),
        "frames good" to "${b.good}",
        "frames bad CRC" to "${b.bad}",
        "frames sent" to "${b.sent}",
        "framer syncs" to "${b.syncs}",
        "handshakes" to "${b.complete} complete, ${b.aborts} abort",
        "link state" to "${b.linkState}",
        "core 1" to "${b.core1Load}%",
    )

    Column(Modifier.padding(horizontal = 16.dp)) {
        for ((k, v) in rows) {
            Row(Modifier.fillMaxWidth()) {
                Text(k, style = MonoStyle, color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.weight(1f))
                Text(v, style = MonoStyle)
            }
        }

        // The readings that are worth a sentence rather than a number, because
        // each one invalidates everything above it.
        if (b.onUsb) {
            Note(
                "The band is on USB. A body-coupled reading taken now is not " +
                    "valid — the PC ground is the return path under test. " +
                    "Run it on the cell.",
            )
        }
        if (b.noiseRef == 0) {
            // presence.c answers zero until the boxcar has HANDOFF_CFAR_CELLS
            // cells in it, and refuses to call anything busy until then. A
            // reference of zero is therefore "no answer yet", not a silent
            // room, and every number above it means nothing.
            Note(
                "The CFAR reference is still filling, so the band is not " +
                    "deciding anything yet. One preamble of airtime.",
            )
        } else if (trig != null && trig.peakSignal > trig.peakThreshold && b.syncs == 0) {
            // The v1 test asked this of the instantaneous level, which under
            // v2 is almost always the empty room. The peak is what actually
            // answers it.
            Note(
                "The loudest window cleared the detector but the framer has " +
                    "never synced: the receiver is being swamped, not starved.",
            )
        }
    }
}

/**
 * The trigger, which is the other half of the diagnosis and the half the signal
 * numbers cannot give.
 *
 * READ PEERS AGAINST BEACONS. Beacons is what this band transmitted, peers is
 * what it decoded from the other one. Both bands beaconing and neither hearing
 * is a channel fault; one hearing and not the other is the asymmetry that was
 * the step-7 fault, and no amount of looking at a level would have found it.
 *
 * The counters are cumulative since the band booted, on purpose — two readings
 * and a subtraction give a rate over whatever window you chose. Erasing the
 * card does not reset them; a reboot does.
 */
@Composable
private fun TrigDump(state: BandService.State?) {
    val t = state?.trig
    if (t == null) {
        Text(
            "Nothing yet. These arrive with the body-link block.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        return
    }

    val rows = listOf(
        // The pair, on one row, because neither number means anything alone.
        "beacons sent / heard" to "${t.beacons} / ${t.peers}",
        "self echoes" to "${t.selfEchoes}",
        "beacons bad CRC" to "${t.beaconsBadCrc}",
        "elected" to "${t.sends} sender, ${t.receives} receiver",
        "trigger state" to t.stateName,
        // Redrawn on every echo, so it moving IS the echo count moving.
        "nonce" to "%04x".format(t.nonce),
    )

    Column(Modifier.padding(horizontal = 16.dp)) {
        for ((k, v) in rows) {
            Row(Modifier.fillMaxWidth()) {
                Text(k, style = MonoStyle, color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.weight(1f))
                Text(v, style = MonoStyle)
            }
        }

        if (t.beacons > 0 && t.peers == 0) {
            Note(
                "This band has beaconed ${t.beacons} times and decoded none " +
                    "from the other one. Check the other band is armed, and " +
                    "that both are floating on their cells.",
            )
        }
        if (t.selfEchoes > 0) {
            Note(
                "${t.selfEchoes} of the beacons it decoded were its own, so " +
                    "its amplifier is still ringing past the settle. They were " +
                    "discarded, but a large count next to few peers is a " +
                    "receiver listening mostly to itself.",
            )
        }
    }
}

/** A sentence that invalidates or qualifies the numbers above it. */
@Composable
private fun Note(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.error,
        modifier = Modifier.padding(top = 8.dp),
    )
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
