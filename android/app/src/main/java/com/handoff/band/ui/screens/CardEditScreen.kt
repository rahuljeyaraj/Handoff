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
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.handoff.band.contacts.PhoneFormat
import com.handoff.band.data.OwnCard

/** The two phone labels the band carries (`TEL;TYPE=CELL` and `TYPE=WORK`). */
enum class PhoneLabel(val label: String) { MOBILE("Mobile"), WORK("Work") }

private data class PhoneRow(val number: String, val label: PhoneLabel)

/**
 * Your contact card, design decisions §4a: fields only. A field left empty
 * is a field not shared, so there are no switches and nothing to explain.
 * Phone is a number plus a label, the way the phone's Contacts app has it,
 * and "Add another phone" gives the second row — two at most, which is what
 * the band carries. Save wants a name and a phone or email; below that it
 * is simply disabled.
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
        mutableStateOf(phoneRows(initial))
    }
    var confirmDelete by remember { mutableStateOf(false) }

    fun number(label: PhoneLabel) = phones.firstOrNull { it.label == label }?.number.orEmpty()
    val complete = name.isNotBlank() &&
        (phones.any { it.number.isNotBlank() } || email.isNotBlank())

    fun save() = onSave(OwnCard(
        name = name.trim(),
        mobile = PhoneFormat.format(context, number(PhoneLabel.MOBILE)),
        work = PhoneFormat.format(context, number(PhoneLabel.WORK)),
        email = email.trim(),
        org = org.trim(),
        title = title.trim(),
    ))

    // Two rows, two labels: picking the other row's label swaps them, so the
    // number you just typed keeps the label you just chose.
    fun relabel(index: Int, label: PhoneLabel) {
        val other = if (label == PhoneLabel.MOBILE) PhoneLabel.WORK else PhoneLabel.MOBILE
        phones = phones.mapIndexed { i, row ->
            when {
                i == index -> row.copy(label = label)
                row.label == label -> row.copy(label = other)
                else -> row
            }
        }
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
                    PhoneField(
                        row = row,
                        onNumber = { n -> phones = phones.mapIndexed { j, r -> if (j == i) r.copy(number = n) else r } },
                        onLabel = { relabel(i, it) },
                    )
                }
            }
            if (phones.size < 2) {
                // A 24 dp slot; the 40 dp button overflows it, centred, so it
                // keeps its ripple and touch target without opening a gap.
                Box(Modifier.height(24.dp)) {
                    TextButton(
                        onClick = {
                            val taken = phones.map { it.label }
                            val free = PhoneLabel.entries.first { it !in taken }
                            phones = phones + PhoneRow("", free)
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
            text = { Text("It will be removed from the band as well.") },
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

/** The number, and beside it the label as a dropdown. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PhoneField(row: PhoneRow, onNumber: (String) -> Unit, onLabel: (PhoneLabel) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Row(verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedTextField(
            row.number, onNumber, label = { Text("Phone") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            modifier = Modifier.weight(1f),
        )
        ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }) {
            OutlinedTextField(
                row.label.label, {}, readOnly = true, singleLine = true,
                label = { Text("Label") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
                modifier = Modifier.width(132.dp).menuAnchor(MenuAnchorType.PrimaryNotEditable),
            )
            ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                PhoneLabel.entries.forEach { l ->
                    DropdownMenuItem(
                        text = { Text(l.label) },
                        onClick = { open = false; onLabel(l) },
                    )
                }
            }
        }
    }
}

/** The rows a saved card opens with; a fresh card starts with one Mobile row. */
private fun phoneRows(card: OwnCard?): List<PhoneRow> {
    val rows = listOfNotNull(
        card?.mobile?.takeIf { it.isNotBlank() }?.let { PhoneRow(it, PhoneLabel.MOBILE) },
        card?.work?.takeIf { it.isNotBlank() }?.let { PhoneRow(it, PhoneLabel.WORK) },
    )
    return rows.ifEmpty { listOf(PhoneRow("", PhoneLabel.MOBILE)) }
}

/** Survives rotation as number, label, number, label. */
private val PhoneRowsSaver = listSaver<List<PhoneRow>, String>(
    save = { rows -> rows.flatMap { listOf(it.number, it.label.name) } },
    restore = { l -> l.chunked(2).map { (n, label) -> PhoneRow(n, PhoneLabel.valueOf(label)) } },
)
