package com.handoff.band.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import android.provider.ContactsContract
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.handoff.band.ble.BandService
import com.handoff.band.contacts.ContactReader
import com.handoff.band.vcard.VCard

/**
 * Provisioning the wearer's own card, architecture §11.3.
 *
 * Two sources, as §11.3 requires: a contact picked out of the phone's address
 * book, or manual entry for a purpose-built card that is not derived from one.
 *
 * PER-FIELD TOGGLES ARE NOT DECORATION. The card is going to cross somebody's
 * skin into a stranger's phone, and the wearer should be able to send a name
 * and a mobile without also sending their home address. The preview shows the
 * exact bytes that will be written, because "what did I actually give away"
 * has to be answerable before the handshake and not after it.
 *
 * PHOTO is never offered: `compact.c` rejects it at encode time with an
 * explicit error (§8.2), so offering it would produce a provisioning failure
 * rather than a bigger card.
 */
class ProvisionActivity : ComponentActivity() {

    private var service: BandService? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            service = (binder as BandService.LocalBinder).service
        }
        override fun onServiceDisconnected(name: ComponentName?) { service = null }
    }

    private var picked by mutableStateOf<ContactReader.Fields?>(null)

    private val readContacts = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> if (granted) pick() }

    private val picker = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        result.data?.data?.let { uri -> picked = ContactReader.read(this, uri) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bindService(Intent(this, BandService::class.java), connection,
            Context.BIND_AUTO_CREATE)

        setContent {
            MaterialTheme {
                Surface(Modifier.fillMaxSize()) { Body() }
            }
        }
    }

    override fun onDestroy() {
        runCatching { unbindService(connection) }
        super.onDestroy()
    }

    private fun pick() {
        picker.launch(Intent(Intent.ACTION_PICK, ContactsContract.Contacts.CONTENT_URI))
    }

    @Composable
    private fun Body() {
        var name by remember { mutableStateOf("") }
        var mobile by remember { mutableStateOf("") }
        var email by remember { mutableStateOf("") }
        var org by remember { mutableStateOf("") }
        var title by remember { mutableStateOf("") }

        var sendMobile by remember { mutableStateOf(true) }
        var sendEmail by remember { mutableStateOf(true) }
        var sendOrg by remember { mutableStateOf(true) }
        var sendTitle by remember { mutableStateOf(true) }

        // A pick overwrites the form; from there it is editable like any other
        // manual entry, because a contact record and a card you want to hand
        // out are not always the same thing.
        //
        // In a LaunchedEffect rather than straight in the composable body:
        // writing to remembered state during composition is how you get a
        // form that fights the person typing into it.
        LaunchedEffect(picked) {
            picked?.let { p ->
                name = p.displayName
                mobile = p.mobile.orEmpty()
                email = p.email.orEmpty()
                org = p.org.orEmpty()
                title = p.title.orEmpty()
                picked = null
            }
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

        Column(
            Modifier.padding(16.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(onClick = {
                readContacts.launch(Manifest.permission.READ_CONTACTS)
            }) { Text("Pick from Contacts") }

            OutlinedTextField(name, { name = it }, label = { Text("Name") })
            Field("Mobile", mobile, { mobile = it }, sendMobile) { sendMobile = it }
            Field("Email", email, { email = it }, sendEmail) { sendEmail = it }
            Field("Organisation", org, { org = it }, sendOrg) { sendOrg = it }
            Field("Title", title, { title = it }, sendTitle) { sendTitle = it }

            HorizontalDivider()

            Text("This is what crosses the skin:",
                 style = MaterialTheme.typography.titleSmall)
            Text(card, style = MaterialTheme.typography.bodySmall,
                 fontFamily = FontFamily.Monospace)

            Button(
                enabled = name.isNotBlank(),
                onClick = {
                    service?.provision(card)
                    finish()
                },
            ) { Text("Write to band") }
        }
    }

    @Composable
    private fun Field(
        label: String,
        value: String,
        onValue: (String) -> Unit,
        send: Boolean,
        onSend: (Boolean) -> Unit,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Checkbox(checked = send, onCheckedChange = onSend)
            OutlinedTextField(value, onValue, label = { Text(label) })
        }
    }
}
