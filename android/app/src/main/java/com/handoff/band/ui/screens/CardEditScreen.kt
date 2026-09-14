package com.handoff.band.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
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
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.handoff.band.contacts.PhoneFormat
import com.handoff.band.data.OwnCard
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import com.handoff.band.data.Phones

/** The two phone labels the band carries (`TEL;TYPE=CELL` and `TYPE=WORK`). */

/**
 * Your contact card, design decisions §4a: fields only. A field left empty
 * is a field not shared, so there are no switches and nothing to explain.
 * Phone is a number plus a label, the way the phone's Contacts app has it:
 * Mobile, Work, Home, Main, or one the wearer types themselves. "Add another
 * phone" gives the next row, up to [Phones.MAX], and the same label twice is
 * allowed — two mobiles is a real thing a person has. Save wants a name and a
 * phone or email; below that it is simply disabled.
 *
 * [initial] null is the first-time page, reached from the Band screen or
 * setup step 2: titled like the page it stands in for, and with the same
 * sentence setup used.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardEditScreen(
    initial: OwnCard?,
    onSave: (OwnCard) -> Unit,
    onDelete: () -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var name by rememberSaveable { mutableStateOf(initial?.name.orEmpty()) }
    var email by rememberSaveable { mutableStateOf(initial?.email.orEmpty()) }
    var org by rememberSaveable { mutableStateOf(initial?.org.orEmpty()) }
    var title by rememberSaveable { mutableStateOf(initial?.title.orEmpty()) }
    var phones by rememberSaveable(stateSaver = PhoneRowsSaver) {
        mutableStateOf(phoneRows(initial?.phones))
    }
    var confirmDelete by remember { mutableStateOf(false) }

    val complete = name.isNotBlank() &&
        (phones.any { it.number.isNotBlank() } || email.isNotBlank())

    fun save() = onSave(OwnCard(
        name = name.trim(),
        phones = phones
            .filterNot { it.blank }
            .map { it.copy(number = PhoneFormat.format(context, it.number), custom = it.custom.trim()) },
        email = email.trim(),
        org = org.trim(),
        title = title.trim(),
    ))

    fun edit(index: Int, change: (Phone) -> Phone) {
        phones = phones.mapIndexed { i, row -> if (i == index) change(row) else row }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (initial == null) "Your contact card" else "Edit your card") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        if (initial == null) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                        } else {
                            Icon(Icons.Filled.Close, contentDescription = "Cancel")
                        }
                    }
                },
                actions = {
                    TextButton(enabled = complete, onClick = ::save) { Text("Save") }
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
            if (initial == null) {
                Text("Handed over when you shake hands.",
                     style = MaterialTheme.typography.bodyMedium,
                     color = MaterialTheme.colorScheme.onSurfaceVariant,
                     modifier = Modifier.padding(bottom = 4.dp))
            }

            Field("Name", name) { name = it }

            // The phone group: rows, and "Add another phone" under the last
            // one as on the artboard — as close to the fields as they sit to
            // each other, its plus on the field edge.
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                phones.forEachIndexed { i, row ->
                    PhoneField(row) { updated -> edit(i) { updated } }
                }
            }
            if (phones.size < Phones.MAX) {
                // A 24 dp slot; the 40 dp button overflows it, centred, so it
                // keeps its ripple and touch target without opening a gap.
                Box(Modifier.height(24.dp)) {
                    TextButton(
                        onClick = {
                            // The first label not already used, so the common
                            // case needs no second tap; the same label twice
                            // is still a choice the menu allows.
                            val free = LABELS.firstOrNull { l -> phones.none { it.label == l } }
                            phones = phones + Phone("", free ?: PhoneLabel.MOBILE)
                        },
                        modifier = Modifier.requiredHeight(40.dp).offset(x = (-12).dp),
                    ) {
                        Icon(Icons.Filled.Add, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("Add another phone")
                    }
                }
            }

            Field("Email", email, KeyboardType.Email) { email = it }
            Field("Organisation", org) { org = it }
            Field("Title", title) { title = it }

            if (initial != null) {
                Spacer(Modifier.height(8.dp))
                HorizontalDivider()
                TextButton(
                    onClick = { confirmDelete = true },
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error),
                ) {
                    Icon(Icons.Filled.Delete, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Delete my contact card")
                }
            }

            Spacer(Modifier.height(24.dp))
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete your contact card?") },
            // Same words as the view page's dialog: what stops, what does not.
            text = { Text("The band will stop sharing your contact. It will still receive contacts from others.") },
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
