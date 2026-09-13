package com.handoff.band.ui.screens

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.handoff.band.data.OwnCard
import com.handoff.band.ui.Notice
import com.handoff.band.ui.components.DetailRow
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.HandoffSnackbarHost
import com.handoff.band.ui.components.PersonHeader
import com.handoff.band.ui.theme.semantic

/**
 * Where the card stands relative to the band — the one fact this page knows
 * that a received contact's does not, said in one chip under the name.
 */
enum class CardOnBand {
    /** The band holds exactly this card. */
    ON_BAND,
    /** Saved on the phone; the band is off, away, or still being written. */
    NOT_YET,
    /** The band holds a card this one has since replaced. */
    OLDER_COPY;

    companion object {
        /**
         * [pushed] is the hash of the last card the band acknowledged
         * (`Prefs.pushedCard`); [onBand] is the band's own report, null
         * until it has made one.
         */
        fun of(card: OwnCard, pushed: String?, onBand: Boolean?): CardOnBand = when {
            pushed == card.hash && onBand != false -> ON_BAND
            pushed != null && pushed != card.hash -> OLDER_COPY
            else -> NOT_YET
        }
    }
}

/**
 * Your saved contact card, read-only, the same page a received contact gets:
 * pen and bin in the app bar, the header, the rows. Editing is a page of its
 * own, so what is on the band is never confused with what is being typed.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardViewScreen(
    card: OwnCard,
    onBand: CardOnBand,
    bandName: String,
    cardSaved: Notice?,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
    onBack: () -> Unit,
) {
    var confirmDelete by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    // Back from Save on the editor: where the card is going.
    LaunchedEffect(cardSaved) {
        cardSaved?.take()?.let { snackbar.showSnackbar(it) }
    }

    Scaffold(
        snackbarHost = { HandoffSnackbarHost(snackbar) },
        topBar = {
            TopAppBar(
                title = { Text("Your contact card") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = onEdit) {
                        Icon(Icons.Filled.Edit, contentDescription = "Edit")
                    }
                    IconButton(onClick = { confirmDelete = true }) {
                        Icon(Icons.Filled.Delete, contentDescription = "Delete")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize()) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                PersonHeader(card.name, card.title, card.org) {
                    OnBandChip(onBand, bandName, Modifier.padding(top = 12.dp))
                }

                HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                if (card.mobile.isNotBlank()) DetailRow(HandoffIcons.Phone, card.mobile, "Mobile")
                if (card.work.isNotBlank()) DetailRow(HandoffIcons.Phone, card.work, "Work")
                if (card.email.isNotBlank()) DetailRow(HandoffIcons.Mail, card.email, "Email")

                // Room for the caption when the page is short enough not to scroll.
                Spacer(Modifier.height(72.dp))
            }

            // The same sentence as setup's step 2 and the first-time editor,
            // at the foot of the page.
            Text("Handed over when you shake hands.",
                 style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 textAlign = TextAlign.Center,
                 modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth()
                     .padding(horizontal = 32.dp, vertical = 24.dp))
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete your contact card?") },
            text = { Text("It will be removed from the band as well.") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

/** The tick in the ok colour when the band has it; a muted clock otherwise. */
@Composable
private fun OnBandChip(state: CardOnBand, bandName: String, modifier: Modifier = Modifier) {
    val ok = state == CardOnBand.ON_BAND
    val colour = if (ok) MaterialTheme.semantic.ok else MaterialTheme.colorScheme.onSurfaceVariant
    val text = when (state) {
        CardOnBand.ON_BAND -> "On $bandName"
        CardOnBand.NOT_YET -> "Not on the band yet"
        CardOnBand.OLDER_COPY -> "Band has an older copy"
    }
    Row(
        modifier
            .height(28.dp)
            .border(1.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Icon(if (ok) Icons.Filled.Check else HandoffIcons.Clock, contentDescription = null,
             tint = colour, modifier = Modifier.size(14.dp))
        Text(text, style = MaterialTheme.typography.labelMedium, color = colour)
    }
}
