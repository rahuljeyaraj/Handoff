package com.handoff.band.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.handoff.band.contacts.PhoneFormat
import com.handoff.band.data.Handshake
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import com.handoff.band.data.Phones
import com.handoff.band.vcard.VCard

/**
 * In-app editing, design decisions §4. The name field alone carries any
 * prefix or suffix a person wants to add — no separate boxes. Two phone
 * numbers, Mobile and Work, because that is what the codec carries. The raw
 * vCard of a received card is never touched by an edit.
 *
 * [contact] null means the "+" screen (review item 8): an empty row, no
 * delete section (there is nothing to delete yet), and a vCard built from
 * what is typed rather than one that crossed a body link.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactEditScreen(
    contact: Handshake?,
    onSave: (Handshake) -> Unit,
    onDelete: (() -> Unit)?,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var name by rememberSaveable { mutableStateOf(contact?.displayName.orEmpty()) }
    var phones by rememberSaveable(stateSaver = PhoneRowsSaver) {
        mutableStateOf(phoneRows(contact?.phones))
    }
    var email by rememberSaveable { mutableStateOf(contact?.email.orEmpty()) }
    var org by rememberSaveable { mutableStateOf(contact?.org.orEmpty()) }
    var title by rememberSaveable { mutableStateOf(contact?.title.orEmpty()) }
    var note by rememberSaveable { mutableStateOf(contact?.note.orEmpty()) }
    var confirmDelete by remember { mutableStateOf(false) }

    fun String.orNull() = trim().takeIf { it.isNotEmpty() }

    fun save() {
        val displayName = name.trim()
        val formatted = phones
            .filterNot { it.blank }
            .map {
                it.copy(number = PhoneFormat.format(context, it.number), custom = it.custom.trim())
            }
        val base = contact ?: Handshake(
            receivedAt = System.currentTimeMillis(),
            vcard = VCard.build(
                fullName = displayName, phones = formatted,
                email = email.orNull(), org = org.orNull(), title = title.orNull(),
                note = note.orNull(),
            ),
            displayName = displayName,
            addedByHand = true,
        )
        val next = base.copy(
            displayName = displayName,
            phones = formatted,
            email = email.orNull(),
            org = org.orNull(),
            title = title.orNull(),
            note = note.orNull(),
        ).rekeyed()
        // Opening the editor and pressing save changes nothing; only a real
        // change makes the phone's copy stale.
        onSave(if (next == base) base else next.edited())
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (contact == null) "New contact" else "Edit contact") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    TextButton(enabled = name.isNotBlank(), onClick = ::save) { Text("Save") }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Field("Name", name) { name = it }

            // The same rows as the card editor: a received number keeps the
            // label it arrived with, and gains one the wearer chooses.
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                phones.forEachIndexed { i, row ->
                    PhoneField(row) { updated ->
                        phones = phones.mapIndexed { j, r -> if (j == i) updated else r }
                    }
                }
            }
            if (phones.size < Phones.MAX) {
                TextButton(
                    onClick = {
                        val free = LABELS.firstOrNull { l -> phones.none { it.label == l } }
                        phones = phones + Phone("", free ?: PhoneLabel.MOBILE)
                    },
                    modifier = Modifier.offset(x = (-12).dp),
                ) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Add another phone")
                }
            }
            Field("Email", email, KeyboardType.Email) { email = it }
            Field("Organisation", org) { org = it }
            Field("Title", title) { title = it }
            OutlinedTextField(
                note, { note = it }, label = { Text("Note") },
                minLines = 3, modifier = Modifier.fillMaxWidth(),
            )

            if (onDelete != null) {
                Spacer(Modifier.height(8.dp))
                HorizontalDivider()
                TextButton(
                    onClick = { confirmDelete = true },
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) {
                    Icon(Icons.Filled.Delete, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Delete this contact")
                }
            }

            Spacer(Modifier.height(24.dp))
        }
    }

    if (confirmDelete && onDelete != null) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete ${contact?.displayName}?") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun Field(
    label: String,
    value: String,
    keyboard: KeyboardType = KeyboardType.Text,
    onValue: (String) -> Unit,
) {
    OutlinedTextField(
        value, onValue, label = { Text(label) }, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = keyboard),
        modifier = Modifier.fillMaxWidth(),
    )
}
