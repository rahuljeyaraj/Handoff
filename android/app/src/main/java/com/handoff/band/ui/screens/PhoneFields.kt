package com.handoff.band.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel

/**
 * One phone row — a number and its label — shared by the two editors that
 * have phones on them: the wearer's own card and a received contact.
 *
 * Both sides of a handshake are the same shape now (design decisions §4a), so
 * a received number the sender called "Reception" is edited here with the
 * label it arrived with, not flattened into a Mobile and a Work field.
 *
 * The menu is the one the phone's own Contacts app puts under a number, in
 * the same order and with the same behaviour: a label already typed sits at
 * the TOP as an entry of its own, the selected entry carries a tick, and
 * *Custom* at the bottom asks for the word in a dialog.
 */

/** The named labels the menu offers, in the order it lists them. */
internal val LABELS = listOf(
    PhoneLabel.MOBILE, PhoneLabel.WORK, PhoneLabel.HOME, PhoneLabel.MAIN,
)

/**
 * The number, and beside it the label as a dropdown. [onRow] is given the
 * whole row back, so a custom label — which changes the label AND its text
 * together — arrives as one change rather than two.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun PhoneField(row: Phone, onRow: (Phone) -> Unit) {
    var open by remember { mutableStateOf(false) }
    // Non-null while the dialog is up; it holds what has been typed so far,
    // so Cancel leaves both the label and the old word exactly as they were.
    var naming by remember { mutableStateOf<String?>(null) }

    // The word stays on the row even while a named label is showing, so
    // switching to Mobile and back does not mean typing it again — the menu
    // keeps offering it, the way Contacts keeps a label you have used.
    val custom = row.custom.trim()

    Row(verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedTextField(
            row.number, { onRow(row.copy(number = it)) },
            label = { Text("Phone") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            modifier = Modifier.weight(1f),
        )
        ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }) {
            OutlinedTextField(
                row.text, {}, readOnly = true, singleLine = true,
                label = { Text("Label") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
                modifier = Modifier.width(132.dp).menuAnchor(MenuAnchorType.PrimaryNotEditable),
            )
            ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                // A word already typed goes first, ticked while it is the
                // label this number is actually wearing.
                if (custom.isNotEmpty()) {
                    LabelItem(custom, selected = row.label == PhoneLabel.CUSTOM) {
                        open = false
                        onRow(row.copy(label = PhoneLabel.CUSTOM, custom = custom))
                    }
                }
                LABELS.forEach { l ->
                    LabelItem(Phone("", l).text, selected = row.label == l) {
                        open = false
                        onRow(row.copy(label = l))
                    }
                }
                LabelItem("Custom", selected = false) {
                    open = false
                    naming = custom
                }
            }
        }
    }

    naming?.let { draft ->
        CustomLabelDialog(
            draft = draft,
            onDraft = { naming = it },
            onCancel = { naming = null },
            onOk = {
                naming = null
                onRow(row.copy(label = PhoneLabel.CUSTOM, custom = draft.trim()))
            },
        )
    }
}

@Composable
private fun LabelItem(text: String, selected: Boolean, onClick: () -> Unit) {
    DropdownMenuItem(
        text = { Text(text) },
        trailingIcon = { if (selected) Icon(Icons.Filled.Check, contentDescription = "Selected") },
        onClick = onClick,
    )
}

/**
 * The word itself, asked for the way Contacts asks for it. Empty is not a
 * label, so OK stays disabled until something is typed — and Cancel is then
 * the only way out, which leaves the row on the label it already had.
 */
@Composable
private fun CustomLabelDialog(
    draft: String,
    onDraft: (String) -> Unit,
    onCancel: () -> Unit,
    onOk: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Custom label name") },
        text = {
            OutlinedTextField(
                draft, onDraft, label = { Text("Label name") }, singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Words),
                modifier = Modifier.fillMaxWidth(),
            )
        },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
        confirmButton = {
            TextButton(enabled = draft.isNotBlank(), onClick = onOk) { Text("OK") }
        },
    )
}

/** The rows an editor opens with; with nothing to show it is one Mobile row. */
internal fun phoneRows(phones: List<Phone>?): List<Phone> =
    phones?.filterNot { it.blank }?.takeIf { it.isNotEmpty() }
        ?: listOf(Phone("", PhoneLabel.MOBILE))

/** Survives rotation as number, label, custom, number, label, custom. */
internal val PhoneRowsSaver = listSaver<List<Phone>, String>(
    save = { rows -> rows.flatMap { listOf(it.number, it.label.name, it.custom) } },
    restore = { l ->
        l.chunked(3).map { (n, label, custom) ->
            Phone(n, runCatching { PhoneLabel.valueOf(label) }.getOrDefault(PhoneLabel.MOBILE), custom)
        }
    },
)
