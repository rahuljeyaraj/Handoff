package com.handoff.band.ui.screens

import android.Manifest
import android.content.Intent
import android.provider.ContactsContract
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
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
import com.handoff.band.contacts.ContactReader
import com.handoff.band.contacts.PhoneFormat
import com.handoff.band.data.OwnCard

/**
 * Your contact card, design decisions §4a. A real editor: every field is a
 * text field, and "Fill from a phone contact" is a prefill at the top rather
 * than a fork in the road. A contact record and a card you want to hand out
 * are not always the same thing.
 *
 * PER-FIELD SEND TOGGLES ARE NOT DECORATION. The card crosses a stranger's
 * skin and the wearer should be able to send a name and a mobile without
 * also sending their job title.
 *
 * A NAME-ONLY CARD IS NEVER SENT, and the control enforces it rather than a
 * sentence: when only one of mobile or email is left on, that toggle is
 * disabled. You physically cannot switch it off.
 *
 * PHOTO is never offered: `compact.c` rejects it at encode time (§8.2).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardScreen(
    initial: OwnCard?,
    onSave: (OwnCard) -> Unit,
    onRemove: () -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var card by rememberSaveable(stateSaver = OwnCardSaver) {
        mutableStateOf(initial ?: OwnCard())
    }
    var confirmRemove by remember { mutableStateOf(false) }

    // A pick overwrites the fields; from there it is editable like any other
    // manual entry. The toggles are left as they were.
    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val uri = result.data?.data ?: return@rememberLauncherForActivityResult
        ContactReader.read(context, uri)?.let { p ->
            card = card.copy(
                name = p.displayName,
                mobile = p.mobile?.let { PhoneFormat.format(context, it) }.orEmpty(),
                work = p.work?.let { PhoneFormat.format(context, it) }.orEmpty(),
                email = p.email.orEmpty(),
                org = p.org.orEmpty(),
                title = p.title.orEmpty(),
            )
        }
    }
    val readContacts = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) picker.launch(Intent(Intent.ACTION_PICK, ContactsContract.Contacts.CONTENT_URI))
    }

    // The last contact method standing cannot be switched off.
    val mobileLocked = card.mobileShared && !card.emailShared
    val emailLocked = card.emailShared && !card.mobileShared

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Your contact card") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    TextButton(enabled = card.complete, onClick = {
                        onSave(card.copy(
                            mobile = PhoneFormat.format(context, card.mobile),
                            work = PhoneFormat.format(context, card.work),
                        ))
                    }) {
                        Text("Save")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface),
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            OutlinedButton(
                onClick = { readContacts.launch(Manifest.permission.READ_CONTACTS) },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.Person, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text("Fill from a phone contact")
            }

            Text("Choose what you share.", style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)

            OutlinedTextField(card.name, { card = card.copy(name = it) }, label = { Text("Name") },
                              singleLine = true, modifier = Modifier.fillMaxWidth())
            ToggledField("Mobile", card.mobile, { card = card.copy(mobile = it) },
                         send = card.sendMobile, enabled = !mobileLocked,
                         keyboard = KeyboardType.Phone) { card = card.copy(sendMobile = it) }
            ToggledField("Work phone", card.work, { card = card.copy(work = it) },
                         send = card.sendWork, keyboard = KeyboardType.Phone) {
                card = card.copy(sendWork = it)
            }
            ToggledField("Email", card.email, { card = card.copy(email = it) },
                         send = card.sendEmail, enabled = !emailLocked,
                         keyboard = KeyboardType.Email) { card = card.copy(sendEmail = it) }
            ToggledField("Organisation", card.org, { card = card.copy(org = it) },
                         send = card.sendOrg) { card = card.copy(sendOrg = it) }
            ToggledField("Title", card.title, { card = card.copy(title = it) },
                         send = card.sendTitle) { card = card.copy(sendTitle = it) }

            if (initial != null) {
                Spacer(Modifier.height(8.dp))
                HorizontalDivider()
                TextButton(
                    onClick = { confirmRemove = true },
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

    if (confirmRemove) {
        AlertDialog(
            onDismissRequest = { confirmRemove = false },
            title = { Text("Delete your contact card?") },
            text = { Text("The band will still receive other people's cards.") },
            confirmButton = {
                // The page stays open (review O2): clear the form here rather
                // than relying on `initial` to change, since the saved state
                // was seeded from it only once.
                TextButton(onClick = { confirmRemove = false; card = OwnCard(); onRemove() }) {
                    Text("Delete")
                }
            },
            dismissButton = { TextButton(onClick = { confirmRemove = false }) { Text("Cancel") } },
        )
    }
}

@Composable
fun ToggledField(
    label: String,
    value: String,
    onValue: (String) -> Unit,
    send: Boolean,
    enabled: Boolean = true,
    keyboard: KeyboardType = KeyboardType.Text,
    onSend: (Boolean) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedTextField(value, onValue, label = { Text(label) }, singleLine = true,
                          keyboardOptions = KeyboardOptions(keyboardType = keyboard),
                          modifier = Modifier.weight(1f))
        Switch(checked = send, onCheckedChange = onSend, enabled = enabled)
    }
}

/** Survives rotation as its flat list. */
private val OwnCardSaver = listSaver<OwnCard, Any>(
    save = { it.toList() },
    restore = { OwnCard.fromList(it) },
)
