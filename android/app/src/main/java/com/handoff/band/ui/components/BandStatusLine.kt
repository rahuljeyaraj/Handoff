package com.handoff.band.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.handoff.band.ui.BandView
import com.handoff.band.ui.theme.semantic

/**
 * One line under the app bar, design decisions §1. It navigates to the Band
 * screen and never expands: an expander pushes the list down and leaves the
 * home screen doing two jobs.
 *
 * Taller than a contact row (88 dp against 72) with a bigger glyph and a
 * larger name: the wearer wanted the band to read as the thing the home
 * screen is about, not as one more row in the list.
 *
 * Unpaired, there is no Band screen to open (review item 12) — the same
 * box, in the primary colour, is a single "Pair a band" button straight
 * into setup: one shape for "the band" whether or not there is one yet.
 */
@Composable
fun BandStatusLine(
    band: BandView,
    onClick: () -> Unit,
    onPair: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!band.paired) {
        Row(
            modifier
                .fillMaxWidth()
                .padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 8.dp)
                .height(88.dp)
                .clip(RoundedCornerShape(20.dp))
                .background(MaterialTheme.colorScheme.primary)
                .clickable(onClick = onPair)
                .padding(start = 16.dp, end = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Icon(HandoffIcons.Band, contentDescription = null,
                 tint = MaterialTheme.colorScheme.onPrimary,
                 modifier = Modifier.padding(end = 2.dp).size(28.dp))
            Text("Pair a band", style = MaterialTheme.typography.titleMedium,
                 color = MaterialTheme.colorScheme.onPrimary, modifier = Modifier.weight(1f))
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
                 tint = MaterialTheme.colorScheme.onPrimary, modifier = Modifier.size(22.dp))
        }
        return
    }

    val connected = band.connection == BandView.Connection.CONNECTED
    Row(
        modifier
            .fillMaxWidth()
            .padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 8.dp)
            .height(88.dp)
            .clip(RoundedCornerShape(20.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .clickable(onClick = onClick)
            .padding(start = 16.dp, end = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Icon(HandoffIcons.Band, contentDescription = null,
             tint = MaterialTheme.colorScheme.primary,
             modifier = Modifier.padding(end = 2.dp).size(28.dp))

        // On a 360 dp phone the text column is about 160 dp once the card,
        // battery and chevron have theirs. The name is right above, so the
        // status line says only the state ("Not found"), never the name
        // again (Copy §1) — that repeat is what review item 15's snackbar
        // and notification are for instead.
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
            Text(band.name, style = MaterialTheme.typography.titleMedium,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
            Row(verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ConnectionDot(connected, modifier = Modifier.padding(top = 6.dp))
                Text(band.connection.label, style = MaterialTheme.typography.bodyMedium,
                     color = MaterialTheme.colorScheme.onSurfaceVariant,
                     maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
        }

        CardOnBandIcon(band.cardOnBand, modifier = Modifier.size(22.dp))
        BatteryIcon(band.battery)

        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
             tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(22.dp))
    }
}

@Composable
fun ConnectionDot(connected: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(7.dp)
            .background(
                if (connected) MaterialTheme.semantic.ok else MaterialTheme.colorScheme.outline,
                CircleShape,
            )
    )
}

/**
 * Green ID card when the band holds your card; red with a slash when it
 * does not. Grey card while nothing is known yet.
 */
@Composable
fun CardOnBandIcon(onBand: Boolean?, modifier: Modifier = Modifier) {
    when (onBand) {
        true -> Icon(HandoffIcons.Card, contentDescription = "Your card is on the band",
                     tint = MaterialTheme.semantic.ok, modifier = modifier.size(21.dp))
        false -> Icon(HandoffIcons.CardOff, contentDescription = "No card on the band",
                      tint = MaterialTheme.colorScheme.error, modifier = modifier.size(21.dp))
        null -> Icon(HandoffIcons.Card, contentDescription = null,
                     tint = MaterialTheme.colorScheme.outline, modifier = modifier.size(21.dp))
    }
}
