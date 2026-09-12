package com.handoff.band.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.handoff.band.data.Handshake
import com.handoff.band.ui.components.initials
import com.handoff.band.ui.theme.MonoStyle
import com.handoff.band.ui.theme.semantic
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * A received card, design decisions §4. View, the wearer's note, and "Save to
 * phone contacts" as a secondary action rather than the only thing a tap can
 * do. The raw vCard is behind the overflow: a card that crossed a body in
 * 250 ms is partial by design, and the exact text is worth a look.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactDetailScreen(
    contact: Handshake,
    duplicates: List<Handshake>,
    onEdit: () -> Unit,
    onSaveToPhone: () -> Unit,
    onMerge: (into: Handshake) -> Unit,
    onDelete: () -> Unit,
    onBack: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    var rawCard by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }

    Scaffold(
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
                    Box {
                        IconButton(onClick = { menu = true }) {
                            Icon(Icons.Filled.MoreVert, contentDescription = "More")
                        }
                        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                            DropdownMenuItem(text = { Text("Card as received") },
                                             onClick = { menu = false; rawCard = true })
                            DropdownMenuItem(text = { Text("Delete") },
                                             onClick = { menu = false; confirmDelete = true })
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState())) {
            Header(contact)

            if (duplicates.isNotEmpty()) {
                DuplicateBanner(contact, duplicates.first(), onMerge = { onMerge(duplicates.first()) })
            }

            HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

            contact.mobile?.let { FieldRow(it, "Mobile") }
            contact.work?.let { FieldRow(it, "Work") }
            contact.email?.let { FieldRow(it, "Email") }
            contact.org?.let { FieldRow(it, "Organisation") }

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

            Column(Modifier.padding(horizontal = 16.dp),
                   horizontalAlignment = Alignment.CenterHorizontally) {
                Button(onClick = onSaveToPhone, modifier = Modifier.fillMaxWidth()) {
                    Text("Save to phone contacts")
                }
                Text(
                    if (contact.promoted) "Saved to your phone"
                    else "Not in your phone's address book yet.",
                    style = MaterialTheme.typography.bodySmall,
                    color = if (contact.promoted) MaterialTheme.semantic.ok
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 8.dp),
                )
            }

            Spacer(Modifier.height(24.dp))
        }
    }

    if (rawCard) {
        AlertDialog(
            onDismissRequest = { rawCard = false },
            title = { Text("Card as received") },
            text = { Text(contact.vcard, style = MonoStyle) },
            confirmButton = { TextButton(onClick = { rawCard = false }) { Text("Close") } },
        )
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

@Composable
private fun Header(h: Handshake) {
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(72.dp).background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(initials(h.displayName), style = MaterialTheme.typography.titleLarge,
                 color = MaterialTheme.colorScheme.onPrimaryContainer)
        }
        Text(h.displayName, style = MaterialTheme.typography.titleLarge,
             textAlign = TextAlign.Center, modifier = Modifier.padding(top = 12.dp))
        val role = listOfNotNull(h.title, h.org).joinToString(" · ")
        if (role.isNotEmpty()) {
            Text(role, style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center,
                 modifier = Modifier.padding(top = 2.dp))
        }
        Text(metLabel(h.receivedAt), style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant,
             modifier = Modifier.padding(top = 8.dp))
    }
}

@Composable
private fun FieldRow(value: String, label: String) {
    Column(
        Modifier.fillMaxWidth().height(72.dp).padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(value, style = MaterialTheme.typography.bodyLarge)
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * Another row shares a phone or email with this one. Merge is the escape
 * hatch design decisions §3 keeps for the cases automatic matching missed.
 */
@Composable
private fun DuplicateBanner(h: Handshake, other: Handshake, onMerge: () -> Unit) {
    val shared = when {
        h.phoneKey != null && h.phoneKey == other.phoneKey -> "mobile number"
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

/** "Met today, 14:32" · "Met yesterday, 18:47" · "Met 8 Sep, 10:12". */
fun metLabel(at: Long, now: Long = System.currentTimeMillis()): String {
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(at))
    return when (daysBetween(at, now)) {
        0 -> "Met today, $time"
        1 -> "Met yesterday, $time"
        else -> "Met ${SimpleDateFormat("d MMM", Locale.getDefault()).format(Date(at))}, $time"
    }
}
