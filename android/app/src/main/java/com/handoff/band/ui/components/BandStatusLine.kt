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
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.handoff.band.ui.BandView
import com.handoff.band.ui.theme.semantic

/**
 * One line under the app bar, design decisions §1. It navigates to the Band
 * screen and never expands: an expander pushes the list down and leaves the
 * home screen doing two jobs.
 *
 * Unpaired, there is no Band screen to open (review item 12) — the line's
 * place holds a single "Pair a band" button straight into setup instead —
 * a pill, matching the "Create contact" pill below it.
 */
@Composable
fun BandStatusLine(
    band: BandView,
    onClick: () -> Unit,
    onPair: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!band.paired) {
        Button(
            onClick = onPair,
            modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp).height(56.dp),
        ) { Text("Pair a band") }
        return
    }

    val connected = band.connection == BandView.Connection.CONNECTED
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .height(56.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Icon(HandoffIcons.Band, contentDescription = null,
             tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(22.dp))

        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
            Text(band.name, style = MaterialTheme.typography.titleSmall)
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ConnectionDot(connected)
                Text(band.statusLabel, style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }

        CardOnBandIcon(band.cardOnBand)
        BatteryIcon(band.battery)

        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
             tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(20.dp))
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
 * Green ID card when the band holds your card; amber with a slash when it
 * does not. Grey card while nothing is known yet.
 */
@Composable
fun CardOnBandIcon(onBand: Boolean?, modifier: Modifier = Modifier) {
    when (onBand) {
        true -> Icon(HandoffIcons.Card, contentDescription = "Your card is on the band",
                     tint = MaterialTheme.semantic.ok, modifier = modifier.size(21.dp))
        false -> Icon(HandoffIcons.CardOff, contentDescription = "No card on the band",
                      tint = MaterialTheme.semantic.warn, modifier = modifier.size(21.dp))
        null -> Icon(HandoffIcons.Card, contentDescription = null,
                     tint = MaterialTheme.colorScheme.outline, modifier = modifier.size(21.dp))
    }
}
