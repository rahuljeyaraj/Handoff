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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Person
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.handoff.band.contacts.ContactReader
import com.handoff.band.ui.theme.MonoStyle
import com.handoff.band.vcard.VCard

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
 * PHOTO is never offered: `compact.c` rejects it at encode time (§8.2).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardScreen(
    onSave: (vcard: String) -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current

    var name by rememberSaveable { mutableStateOf("") }
    var mobile by rememberSaveable { mutableStateOf("") }
    var email by rememberSaveable { mutableStateOf("") }
    var org by rememberSaveable { mutableStateOf("") }
    var title by rememberSaveable { mutableStateOf("") }

    var sendMobile by rememberSaveable { mutableStateOf(true) }
    var sendEmail by rememberSaveable { mutableStateOf(true) }
    var sendOrg by rememberSaveable { mutableStateOf(true) }
    var sendTitle by rememberSaveable { mutableStateOf(true) }

    // A pick overwrites the form; from there it is editable like any other
    // manual entry.
    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val uri = result.data?.data ?: return@rememberLauncherForActivityResult
        ContactReader.read(context, uri)?.let { p ->
            name = p.displayName
            mobile = p.mobile.orEmpty()
            email = p.email.orEmpty()
            org = p.org.orEmpty()
            title = p.title.orEmpty()
        }
    }
    val readContacts = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) picker.launch(Intent(Intent.ACTION_PICK, ContactsContract.Contacts.CONTENT_URI))
    }

    val card = VCard.build(
        fullName = name,
        structuredName = name.split(' ').takeIf { it.size >= 2 }
            ?.let { "${it.last()};${it.dropLast(1).joinToString(" ")};;;" },
        mobile = mobile.takeIf { sendMobile },
        email = email.takeIf { sendEmail },
        org = org.takeIf { sendOrg },
        title = title.takeIf { sendTitle },
    )

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
                    TextButton(enabled = name.isNotBlank(), onClick = { onSave(card) }) {
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
                Spacer(Modifier.padding(4.dp))
                Text("Fill from a phone contact")
            }

            Text("Choose what you share.", style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)

            OutlinedTextField(name, { name = it }, label = { Text("Name") },
                              singleLine = true, modifier = Modifier.fillMaxWidth())
            ToggledField("Mobile", mobile, { mobile = it }, sendMobile) { sendMobile = it }
            ToggledField("Email", email, { email = it }, sendEmail) { sendEmail = it }
            ToggledField("Organisation", org, { org = it }, sendOrg) { sendOrg = it }
            ToggledField("Title", title, { title = it }, sendTitle) { sendTitle = it }

            HorizontalDivider(Modifier.padding(vertical = 4.dp))

            Text("What gets written to the band", style = MaterialTheme.typography.titleSmall)
            Text(card, style = MonoStyle, color = MaterialTheme.colorScheme.onSurfaceVariant)

            Spacer(Modifier.height(24.dp))
        }
    }
}

@Composable
fun ToggledField(
    label: String,
    value: String,
    onValue: (String) -> Unit,
    send: Boolean,
    enabled: Boolean = true,
    onSend: (Boolean) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedTextField(value, onValue, label = { Text(label) }, singleLine = true,
                          modifier = Modifier.weight(1f))
        Switch(checked = send, onCheckedChange = onSend, enabled = enabled)
    }
}
