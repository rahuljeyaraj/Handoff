package com.handoff.band.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.handoff.band.data.Handshake
import com.handoff.band.ui.BandView
import com.handoff.band.ui.components.Avatar
import com.handoff.band.ui.components.BandStatusLine
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.theme.semantic
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

/**
 * Home. The contact list IS the home screen (design decisions §1) — there is
 * no separate destination to navigate to. Band state is one tappable line
 * under the app bar; everything else lives behind the cog.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
    contacts: List<Handshake>,
    band: BandView,
    cardSet: Boolean,
    onContact: (Handshake) -> Unit,
    onBand: () -> Unit,
    onSetUpCard: () -> Unit,
    onSettings: () -> Unit,
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Handoff") },
                actions = {
                    IconButton(onClick = onSettings) {
                        Icon(Icons.Filled.Settings, contentDescription = "Settings")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            BandStatusLine(band, onClick = onBand)

            // The nudge sits above the list whenever no card is set, not only
            // while the list is empty: someone can collect a dozen cards
            // before realising they never set their own (§7).
            if (!cardSet && contacts.isNotEmpty()) {
                CardNudge(onSetUpCard, Modifier.padding(top = 12.dp))
            }

            if (contacts.isEmpty()) {
                EmptyState(cardSet, onSetUpCard)
            } else {
                LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(top = 4.dp, bottom = 24.dp)) {
                    items(contacts, key = { it.id }) { h ->
                        ContactRow(h, timeLabel(h.receivedAt), onClick = { onContact(h) })
                    }
                }
            }
        }
    }
}

@Composable
fun ContactRow(h: Handshake, time: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .height(72.dp)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Avatar(h.displayName)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(h.displayName, style = MaterialTheme.typography.bodyLarge,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(secondaryLine(h), style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text(time, style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (h.promoted) {
                Icon(Icons.Filled.Check, contentDescription = "Saved to phone",
                     tint = MaterialTheme.semantic.ok, modifier = Modifier.size(15.dp))
            }
        }
    }
}

/** "Org · reach-them-by", the two facts a list line has room for. */
fun secondaryLine(h: Handshake): String =
    listOfNotNull(h.org, h.mobile ?: h.email).joinToString(" · ")

@Composable
private fun CardNudge(onSetUp: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.primaryContainer)
            .padding(start = 16.dp, end = 8.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Icon(HandoffIcons.CardOff, contentDescription = null,
             tint = MaterialTheme.colorScheme.onPrimaryContainer, modifier = Modifier.size(22.dp))
        Text("You haven't set your contact card",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onPrimaryContainer,
             modifier = Modifier.weight(1f))
        TextButton(onClick = onSetUp) { Text("Set up") }
    }
}

@Composable
private fun EmptyState(cardSet: Boolean, onSetUp: () -> Unit) {
    Column(
        Modifier.fillMaxSize().padding(horizontal = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.weight(1f))
        Icon(HandoffIcons.Band, contentDescription = null,
             tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(56.dp))
        Spacer(Modifier.height(20.dp))
        Text("No handshakes yet", style = MaterialTheme.typography.titleLarge,
             textAlign = TextAlign.Center)
        Spacer(Modifier.height(8.dp))
        Text("Shake hands to exchange contact cards.",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        Spacer(Modifier.weight(1f))
        if (!cardSet) CardNudge(onSetUp, Modifier.padding(bottom = 24.dp))
    }
}

// ---- time ------------------------------------------------------------

/** Time of day for today and yesterday, "8 Sep" for anything older. */
fun timeLabel(at: Long, now: Long = System.currentTimeMillis()): String =
    if (daysBetween(at, now) < 2) DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(at))
    else SimpleDateFormat("d MMM", Locale.getDefault()).format(Date(at))

/** Whole calendar days from [at] to [now], in the device's zone. */
fun daysBetween(at: Long, now: Long): Int {
    fun dayStart(t: Long): Long = Calendar.getInstance().apply {
        timeInMillis = t
        set(Calendar.HOUR_OF_DAY, 0); set(Calendar.MINUTE, 0)
        set(Calendar.SECOND, 0); set(Calendar.MILLISECOND, 0)
    }.timeInMillis
    return ((dayStart(now) - dayStart(at)) / 86_400_000L).toInt()
}
