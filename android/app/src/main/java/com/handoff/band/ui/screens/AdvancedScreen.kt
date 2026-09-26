package com.handoff.band.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.handoff.band.ble.BandService
import com.handoff.band.ble.BenchTrace
import com.handoff.band.ble.Gatt
import com.handoff.band.ble.Pairing
import com.handoff.band.ble.ScanReport
import com.handoff.band.data.OwnCard
import com.handoff.band.ui.LocalBand
import com.handoff.band.ui.components.BenchChart
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.SectionHeader
import com.handoff.band.ui.components.SettingsRow
import com.handoff.band.ui.theme.MonoStyle

/**
 * The bench tools, design decisions §11 — and, since 26 Sep 2026, the page the
 * link is DEMONSTRATED from as well as debugged from.
 *
 * That second job is what set the layout. A viewer standing over your shoulder
 * gets one section, at the top, that answers "are the two bands hearing each
 * other" without a word of explanation; everything that needs explaining is
 * behind a **More** that starts closed. The actions moved to the bottom,
 * because an action is what you came to press and a reading is what you came
 * to watch, and only one of those should be under your thumb by accident.
 *
 * WHAT CAME OFF THIS PAGE AND WHY. The 23-byte MTU floor switch and the erase
 * row are gone: the first is an M2 exit criterion that passed and has no
 * second use, and the second does the same thing as *Forget* on the band
 * screen, where a user can already reach it. The standing caption is gone
 * because nothing left here drops the connection.
 *
 * This is still the one place the MAC address and the raw `status` fields are
 * shown. Nothing here appears on the home screen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AdvancedScreen(state: BandService.State?, ownCard: OwnCard?, onBack: () -> Unit) {
    val band = LocalBand.current
    val context = LocalContext.current

    var scanReport by remember { mutableStateOf<ScanReport?>(null) }
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

            // ---- the demo section, first and uncrowded --------------------
            SectionHeader("Band to band")
            LinkVerdict(state)
            LinkHeadline(state)
            Spacer(Modifier.height(8.dp))
            BenchChart(state?.benchTrace ?: BenchTrace.EMPTY)
            Spacer(Modifier.height(8.dp))
            More("More link numbers") { LinkDetail(state) }

            SectionHeader("Finding each other")
            FindingHeadline(state)
            More("More rendezvous numbers") { FindingDetail(state) }

            SectionHeader("Band status")
            StatusDump(state)

            // The bytes as written to the band. A developer's view, moved
            // here off the Your card screen where it wore a user's hat (§4a).
            ownCard?.let { card ->
                val text = card.vcard()
                Row(Modifier.fillMaxWidth().padding(end = 16.dp),
                    verticalAlignment = Alignment.Bottom) {
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

            // ---- actions, last, where a thumb will not find them by chance
            SectionHeader("Tools")

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
                "Run a Bluetooth scan",
                subtitle = if (scanning != null) "Scanning, ten seconds…"
                           else "Ten seconds, unfiltered. Does this phone hear the band at all?",
                chevron = false,
                onClick = {
                    scanning?.cancel()
                    scanReport = null
                    scanning = Pairing.debugScan(context) { scanReport = it; scanning = null }
                },
            )
            scanReport?.let { ScanResult(it) }

            Spacer(Modifier.height(24.dp))
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Band to band                                                             */
/* ------------------------------------------------------------------------ */

/**
 * ONE SENTENCE THAT IS ALWAYS THERE.
 *
 * This replaces four conditional red notes, and the reason is not tidiness.
 * They appeared and vanished as the numbers moved, so the chart under them
 * jumped up and down the screen twice a second — unreadable to watch, and
 * worse than useless in a demo where the chart is the thing being pointed at.
 *
 * A fixed-height line cannot do that. It also has to say something in every
 * state, which forced each state to be named in words a viewer can act on
 * rather than left as a number to be inferred from.
 *
 * THE ORDER IS THE DIAGNOSIS ORDER, most disqualifying first: a detector that
 * has not finished measuring the room is not answering at all, so nothing
 * below it can be read; energy with no sync is a different fault from no
 * energy, and saying "nothing on the skin" when the receiver is in fact
 * swamped has sent a bench the wrong way before.
 */
private enum class Verdict(val text: String, val tone: Tone) {
    WAITING("Not connected to the band.", Tone.IDLE),
    WARMING("Measuring the room.", Tone.IDLE),
    QUIET("Nothing on the skin.", Tone.IDLE),
    SWAMPED("Something on the skin, but no card in it.", Tone.WARN),
    HEARING("Hearing the other band.", Tone.GOOD),
    EXCHANGED("Cards exchanged.", Tone.GOOD),
}

private enum class Tone { IDLE, WARN, GOOD }

private fun verdictOf(state: BandService.State?): Verdict {
    val b = state?.bench ?: return Verdict.WAITING
    // noiseRef is zero until the CFAR boxcar has HANDOFF_CFAR_CELLS cells in
    // it, and presence.c refuses to call anything busy until then. Zero is
    // "no answer yet", never a silent room — so it outranks every test below.
    if (b.noiseRef == 0) return Verdict.WARMING
    if (b.complete > 0) return Verdict.EXCHANGED
    if (b.good > 0) return Verdict.HEARING
    // THE COUNTED VERDICT, NOT A LEVEL AGAINST A LINE. This test used to
    // compare the band's peak spectrum with k times the room, and a peak has
    // no business being compared with a per-window k: with the peer's link
    // switched off it read SWAMPED continuously, which is the same fault the
    // chart had and for the same reason (BenchChart says it at length).
    //
    // The floor is 0.5 % of the band's listening time. Both sides of that are
    // measured rather than picked: an empty channel read 0.03 % on 26 Sep 2026
    // with nothing but this band on it, and ONE beacon from the other band is
    // eleven milliseconds of a five-hundred millisecond interval, so 2.2 % is
    // what a single shout looks like. 0.5 % sits fifteen times above the floor
    // and four times under one shout.
    val heard = state.benchTrace.lastHeardPct()
    if (heard != null && heard >= HEARD_FLOOR_PCT && b.syncs == 0) return Verdict.SWAMPED
    return Verdict.QUIET
}

/** See [verdictOf]: measured floor 0.03 %, one beacon 2.2 %. */
private const val HEARD_FLOOR_PCT = 0.5f

@Composable
private fun LinkVerdict(state: BandService.State?) {
    val v = verdictOf(state)
    val dot = when (v.tone) {
        Tone.GOOD -> MaterialTheme.colorScheme.primary
        Tone.WARN -> MaterialTheme.colorScheme.tertiary
        Tone.IDLE -> MaterialTheme.colorScheme.outline
    }
    Row(
        Modifier.fillMaxWidth()
            // Fixed, so the chart below it cannot move when the wording does.
            .heightIn(min = 40.dp)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(10.dp).clip(CircleShape).background(dot))
        Spacer(Modifier.width(10.dp))
        Text(v.text, style = MaterialTheme.typography.titleMedium)
    }
}

/** The three numbers a viewer can follow, big enough to read across a table. */
@Composable
private fun LinkHeadline(state: BandService.State?) {
    val b = state?.bench
    val t = state?.trig
    Column(Modifier.padding(horizontal = 16.dp)) {
        Headline("heard", t?.let { "${it.peers} of ${it.beacons} beacons" } ?: "—")
        Headline("handshakes", b?.complete?.toString() ?: "—")
        Headline("frames", b?.let { "${it.good} good, ${it.bad} bad" } ?: "—")
    }
}

@Composable
private fun Headline(name: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(name, style = MaterialTheme.typography.bodyLarge,
             color = MaterialTheme.colorScheme.onSurfaceVariant,
             modifier = Modifier.weight(1f))
        Text(value, style = MaterialTheme.typography.bodyLarge.copy(
            fontWeight = FontWeight.Medium))
    }
}

/**
 * Everything the headline leaves out, for when the headline says the wrong
 * thing and you need to know why.
 *
 * THE CFAR PAIR IS STILL HERE and still means what design §6 says: signal is
 * the louder tone bin, "the room" is the guard reference, and the band called
 * the channel busy when the first cleared the second by k. There is no floor,
 * no gate, no ratio and no min_delta in this design — step 5 deleted the
 * detector that had them — so a reader that labels this pair "level / floor"
 * is reporting a comparison the band never made.
 *
 * `link state` used to be printed here AND in Band status, off two different
 * blocks. It is one number; it lives down there with the rest of the band's
 * own state.
 */
@Composable
private fun LinkDetail(state: BandService.State?) {
    val b = state?.bench
    if (b == null) {
        Muted(
            "Nothing yet. The band pushes these once the app is connected; " +
                "they stop when it disconnects. A band on link v1 firmware " +
                "sends an older block and is deliberately ignored."
        )
        return
    }
    val trig = state.trig
    val bank = state.bank
    Rows(
        listOf(
            "signal" to "${b.signal}",
            "the room" to "${b.noiseRef}",
            "busy above" to "${b.threshold}",
            // The peak is the only level reading that survives this sample
            // rate, so it sits with the instant it is meant to be read against.
            "peak signal" to (trig?.let {
                "${it.peakSignal} of ${it.peakThreshold}" +
                    if (it.peakHeard) " — heard" else ""
            } ?: "—"),
            "busy now" to (if (b.present) "yes" else "no"),
            // The chart's own number, printed, because a line is hard to read a
            // value off — and the two counters it is differenced from, because
            // that is what makes it checkable against the band's console.
            "heard" to (state.benchTrace.lastHeardPct()?.let { "%.2f%% of the time".format(it) }
                ?: "—"),
            "listening" to (bank?.let { "${it.listenWindows} windows" } ?: "—"),
            "of them busy" to (bank?.listenBusy?.toString() ?: "—"),
            // The spectrum is no longer drawn and is still the thing to look at
            // when the percentage surprises you: two tones out of three flat
            // guards is a band, all five up together is the room. Gated against
            // our own transmitter, unlike the pair at the top of this list,
            // which is why the two can disagree.
            "tones A / B" to (bank?.let { "${it.toneA} / ${it.toneB}" } ?: "—"),
            "guards" to (bank?.let {
                "${it.guardLo} / ${it.guardMid} / ${it.guardHi}" } ?: "—"),
            "frames sent" to "${b.sent}",
            "framer syncs" to "${b.syncs}",
            "handshakes" to "${b.complete} complete, ${b.aborts} abort",
            "core 1" to "${b.core1Load}%",
        )
    )
}

/* ------------------------------------------------------------------------ */
/* Finding each other                                                       */
/* ------------------------------------------------------------------------ */

/**
 * The rendezvous, in words rather than jargon. It was called *Rendezvous*,
 * which names the mechanism to someone who already knows it and nothing at
 * all to anyone else.
 *
 * READ HEARD AGAINST SHOUTED. Each band shouts a random number — a nonce —
 * and whichever one hears the other first sends first. Both shouting and
 * neither hearing is a channel fault; one hearing and not the other is the
 * asymmetry that WAS the step-7 fault, and no amount of looking at a level
 * would have found it.
 *
 * The counters are cumulative since the band booted, on purpose: two readings
 * and a subtraction give a rate over whatever window you chose. Erasing the
 * card does not reset them; a reboot does.
 */
@Composable
private fun FindingHeadline(state: BandService.State?) {
    val t = state?.trig
    if (t == null) {
        Muted("Nothing yet. These arrive with the band-to-band readings.")
        return
    }
    Column(Modifier.padding(horizontal = 16.dp)) {
        Headline("this band shouted", "${t.beacons}")
        Headline("it heard the other", "${t.peers}")
        Headline("who spoke first", "${t.sends} sent, ${t.receives} received")
    }
}

@Composable
private fun FindingDetail(state: BandService.State?) {
    val t = state?.trig ?: return
    Rows(
        listOf(
            "self echoes" to "${t.selfEchoes}",
            "shouts bad CRC" to "${t.beaconsBadCrc}",
            "trigger state" to t.stateName,
            // Redrawn on every echo, so it moving IS the echo count moving.
            "nonce" to "%04x".format(t.nonce),
        )
    )
    if (t.selfEchoes > 0) {
        Muted(
            "${t.selfEchoes} of the shouts it decoded were its own, so its " +
                "amplifier is still ringing past the settle. They were " +
                "discarded, but a large count next to few peers is a receiver " +
                "listening mostly to itself."
        )
    }
}

/* ------------------------------------------------------------------------ */
/* Band status                                                              */
/* ------------------------------------------------------------------------ */

/**
 * The band's own state, off `ble_status_t`.
 *
 * THREE ROWS CAME OFF THIS LIST as duplicates of the section above, which is
 * worth naming because two of them did not look like duplicates:
 *
 *   - `link state` was printed twice, off two different blocks. One number.
 *   - `frame errors` is body-link CRC failures since boot — the same
 *     measurement as *frames bad*, differing only in reset point, and two
 *     counters of one thing that disagree by design is a trap.
 *   - `last score` went with the v1 detector.
 *
 * `chunk errors` stays and is renamed: those are BLUETOOTH chunks rejected by
 * the reassembler, nothing to do with the body link, and the bare word
 * "errors" next to the radio counters read as if it were.
 */
@Composable
private fun StatusDump(state: BandService.State?) {
    val s = state?.status
    Rows(
        listOf(
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
            "phone chunk errors" to (s?.chunkErrors?.toString() ?: "—"),
            "link state" to (s?.linkState?.toString() ?: "—"),
            "status version" to (s?.version?.toString() ?: "—"),
            "vsys" to (s?.vsysMv?.let { "$it mV" } ?: "—"),
            "usb power" to (s?.let { it.usbPower.yesNo() } ?: "—"),
            "firmware" to (s?.firmware ?: "—"),
        )
    )
    state?.lastError?.let {
        Text(it, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.error,
             modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))
    }
}

/* ------------------------------------------------------------------------ */
/* Tools                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * The scan result, laid out.
 *
 * It used to be one string containing every advertisement's raw bytes, which
 * answered nothing. The scan exists for one question — does this handset hear
 * the band at all, or hear it and fail its own filter — so the bands come
 * first with a signal strength, and the rest of the air is a count. The raw
 * bytes are still in logcat under `HandoffScan`.
 */
@Composable
private fun ScanResult(r: ScanReport) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 4.dp)) {
        r.error?.let {
            Text(it, style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.error,
                 modifier = Modifier.padding(bottom = 8.dp))
        }
        if (r.bands.isEmpty()) {
            Text("No band heard.", style = MaterialTheme.typography.bodyLarge)
        } else {
            for (h in r.bands) {
                Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                    Text(h.name ?: h.address, style = MonoStyle,
                         modifier = Modifier.weight(1f))
                    Text("${h.rssi} dBm", style = MonoStyle)
                }
                Text(h.address, style = MonoStyle,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Muted(
            "${r.total} devices in ten seconds, ${r.named} of them named. " +
                "Full advertisements are in logcat under HandoffScan."
        )
    }
}

/* ------------------------------------------------------------------------ */
/* Shared bits                                                              */
/* ------------------------------------------------------------------------ */

/** A disclosure whose content starts hidden — this page's crowd control. */
@Composable
private fun More(label: String, content: @Composable () -> Unit) {
    var open by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth()
            .clickable { open = !open }
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            if (open) HandoffIcons.ExpandLess else HandoffIcons.ExpandMore,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.width(8.dp))
        Text(label, style = MaterialTheme.typography.labelLarge,
             color = MaterialTheme.colorScheme.primary)
    }
    AnimatedVisibility(open) { Column { content() } }
}

/** The mono key/value list this page is mostly made of. */
@Composable
private fun Rows(rows: List<Pair<String, String>>) {
    Column(Modifier.padding(horizontal = 16.dp)) {
        for ((k, v) in rows) {
            Row(Modifier.fillMaxWidth()) {
                Text(k, style = MonoStyle, color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.weight(1f))
                Text(v, style = MonoStyle)
            }
        }
    }
}

@Composable
private fun Muted(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
    )
}

private fun Boolean.yesNo() = if (this) "yes" else "no"
