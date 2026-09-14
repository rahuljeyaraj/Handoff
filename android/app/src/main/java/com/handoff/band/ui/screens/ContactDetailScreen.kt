package com.handoff.band.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.handoff.band.contacts.PhoneFormat
import com.handoff.band.data.Handshake
import com.handoff.band.ui.Notice
import com.handoff.band.ui.components.DetailRow
import com.handoff.band.ui.components.HandoffSnackbarHost
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.PersonHeader
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * A received card, design decisions §4. View, the wearer's note, and "Save to
 * phone contacts" as a secondary action rather than the only thing a tap can
 * do. There is no "card as received" view here — that is a developer's look
 * at a card that crossed a body in 250 ms, not something to show the wearer
 * (review item 8); the raw-vCard view the brief keeps is the own card's
 * bytes, under Advanced.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactDetailScreen(
    contact: Handshake,
    duplicates: List<Handshake>,
    onEdit: () -> Unit,
    onSaveToPhone: () -> Unit,
    onUpdatePhone: () -> Unit,
    onMerge: (into: Handshake) -> Unit,
    onDelete: () -> Unit,
    onBack: () -> Unit,
    notice: Notice? = null,
) {
    var confirmDelete by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    // The outcome of "Update phone contact": a silent write needs one line
    // saying it happened (the button going quiet is not enough on its own).
    LaunchedEffect(notice) {
        notice?.take()?.let { snackbar.showSnackbar(it) }
    }

    Scaffold(
        snackbarHost = { HandoffSnackbarHost(snackbar) },
        topBar = {
            TopAppBar(
                title = {},
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
        val context = LocalContext.current
        Column(Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState())) {
            PersonHeader(contact.displayName, contact.title, contact.org) {
                Text(metLabel(contact.receivedAt, contact.addedByHand),
                     style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.padding(top = 8.dp))
            }

            if (duplicates.isNotEmpty()) {
                DuplicateBanner(contact, duplicates.first(), onMerge = { onMerge(duplicates.first()) })
            }

            HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

            // Organisation is in the header's role line, so no row for it.
            contact.phones.filterNot { it.blank }.forEach {
                DetailRow(HandoffIcons.Phone, PhoneFormat.format(context, it.number), it.text)
            }
            contact.email?.let { DetailRow(HandoffIcons.Mail, it, "Email") }

            HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

            Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                   verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Your note", style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (contact.note.isNullOrBlank()) {
                    TextButton(onClick = onEdit, contentPadding = PaddingValues(0.dp)) {
                        Text("Add a note")
                    }
                } else {
                    Text(contact.note, style = MaterialTheme.typography.bodyLarge)
                }
            }

            Spacer(Modifier.height(16.dp))

            // One button says where the phone stands: not saved, saved (and
            // nothing to do), or saved but behind this row's edits. No line
            // under it — the wearer found a button plus a caption saying the
            // same thing twice.
            val phoneState = phoneButton(contact)
            Button(
                onClick = if (phoneState.update) onUpdatePhone else onSaveToPhone,
                enabled = phoneState.enabled,
                modifier = Modifier.padding(horizontal = 16.dp).fillMaxWidth(),
            ) { Text(phoneState.label) }

            Spacer(Modifier.height(24.dp))
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete ${contact.displayName}?") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

/** What the phone-contacts button says and whether it does anything. */
data class PhoneButton(val label: String, val enabled: Boolean, val update: Boolean)

fun phoneButton(h: Handshake): PhoneButton = when {
    !h.promoted -> PhoneButton("Save to phone contacts", enabled = true, update = false)
    h.editedSincePromote -> PhoneButton("Update phone contact", enabled = true, update = true)
    else -> PhoneButton("Saved to phone contacts", enabled = false, update = false)
}

/**
 * Another row shares a phone or email with this one. Merge is the escape
 * hatch design decisions §3 keeps for the cases automatic matching missed.
 */
@Composable
private fun DuplicateBanner(h: Handshake, other: Handshake, onMerge: () -> Unit) {
    val shared = when {
        h.phoneKey != null && h.phoneKey == other.phoneKey -> "phone number"
        else -> "email address"
    }
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surfaceContainerHigh)
            .padding(start = 16.dp, end = 8.dp, top = 10.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text("Possible duplicate", style = MaterialTheme.typography.titleSmall)
            Text("“${other.displayName}” shares this $shared.",
                 style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        TextButton(onClick = onMerge) { Text("Merge") }
    }
}

/**
 * "Met today, 14:32" for a card that crossed a body link; "Added today,
 * 14:32" for one entered by hand (review item 8, O4) — same shape, honest
 * about which.
 */
fun metLabel(at: Long, addedByHand: Boolean, now: Long = System.currentTimeMillis()): String {
    val verb = if (addedByHand) "Added" else "Met"
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(at))
    return when (daysBetween(at, now)) {
        0 -> "$verb today, $time"
        1 -> "$verb yesterday, $time"
        else -> "$verb ${SimpleDateFormat("d MMM", Locale.getDefault()).format(Date(at))}, $time"
    }
}
