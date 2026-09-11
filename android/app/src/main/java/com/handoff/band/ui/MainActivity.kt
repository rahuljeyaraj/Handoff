package com.handoff.band.ui

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.lifecycleScope
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Gatt
import com.handoff.band.ble.Pairing
import com.handoff.band.contacts.Promote
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.vcard.VCard
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date

/**
 * The handshake history, and the controls M2 needs to check its own exit
 * criteria: pair, provision, arm a delayed fake card, and force the ATT MTU
 * floor.
 *
 * Deliberately plain. This is an M2 skeleton, not the product's UI — the
 * screens architecture §11.3 describes (search, sort, a detail view) are
 * worth building once there is a real handshake to put in them, which is M12.
 */
class MainActivity : ComponentActivity() {

    private var service: BandService? = null
    private val serviceState = MutableStateFlow<BandService.State?>(null)

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val bound = (binder as BandService.LocalBinder).service
            service = bound
            // Mirror the service's own flow into one the composables collect,
            // so the UI has a single source whether the service is bound yet
            // or not.
            lifecycleScope.launch {
                bound.state.collect { serviceState.value = it }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
        }
    }

    private val permissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* the pair button reports what is still missing */ }

    private val chooser = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult()
    ) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@registerForActivityResult
        Pairing.addressFrom(result.data)?.let { address ->
            Pairing.remember(this, address)
            BandService.start(this, address)
            bindService(Intent(this, BandService::class.java), connection,
                Context.BIND_AUTO_CREATE)
        }
    }

    private val promote = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { /* the system editor owns the outcome */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        permissions.launch(requiredPermissions())

        if (Pairing.storedAddress(this) != null) {
            BandService.start(this)
        }
        bindService(Intent(this, BandService::class.java), connection,
            Context.BIND_AUTO_CREATE)

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    val state by serviceState.collectAsState()
                    val history by HandoffDb.get(this).handshakes().all()
                        .collectAsState(initial = emptyList())

                    Scaffold { padding ->
                        Column(Modifier.padding(padding).padding(16.dp)) {
                            BandPanel(state)
                            HorizontalDivider(Modifier.padding(vertical = 12.dp))
                            History(history) { openEditor(it) }
                        }
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        runCatching { unbindService(connection) }
        super.onDestroy()
    }

    private fun openEditor(h: Handshake) {
        promote.launch(Promote.intentFor(VCard.parse(h.vcard)))
    }

    private fun requiredPermissions(): Array<String> = buildList {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
        }
    }.toTypedArray()

    // ---- composables -----------------------------------------------------

    @Composable
    private fun BandPanel(state: BandService.State?) {
        var floor by remember { mutableStateOf(false) }
        var scanNote by remember { mutableStateOf<String?>(null) }

        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Band", style = MaterialTheme.typography.titleMedium)
            Text(
                when {
                    state == null -> "service not bound"
                    state.address == null -> "not paired"
                    state.ready -> "connected — ${state.address}"
                    state.connected -> "connecting…"
                    else -> "waiting for ${state.address}"
                },
                style = MaterialTheme.typography.bodyMedium,
            )

            state?.status?.let { s ->
                Text(
                    "encrypted ${s.encrypted} · provisioned ${s.provisioned} " +
                        "· flash ${s.flashOk} · record ${s.recordId} " +
                        "· ${s.ownBlobLen} B · chunk errors ${s.chunkErrors}",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            state?.lastError?.let {
                Text(it, style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.error)
            }
            scanNote?.let { Text(it, style = MaterialTheme.typography.bodySmall) }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { pair() }) { Text("Pair") }
                Button(onClick = {
                    scanNote = "scanning…"
                    Pairing.debugScan(this@MainActivity) { scanNote = it }
                }) { Text("Scan") }
                Button(onClick = {
                    startActivity(Intent(this@MainActivity, ProvisionActivity::class.java))
                }) { Text("Provision") }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                // Development plan M2: the notify has to land with the screen
                // off and the app backgrounded. Ten seconds is enough to lock
                // the phone and put it down.
                Button(onClick = { service?.control(Gatt.fakeRx(10)) }) {
                    Text("Fake card in 10 s")
                }
                Button(onClick = { service?.control(Gatt.forget()) }) {
                    Text("Forget card")
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Switch(checked = floor, onCheckedChange = {
                    floor = it
                    service?.forceMtuFloor = it
                })
                // The M2 exit criterion this exists for, spelled out: the
                // chunking has to work at 23 bytes, not at whatever this
                // particular handset negotiates.
                Text("Force the 23-byte ATT MTU floor",
                     style = MaterialTheme.typography.bodySmall)
            }
        }
    }

    @Composable
    private fun History(items: List<Handshake>, onPromote: (Handshake) -> Unit) {
        Text("Handshakes", style = MaterialTheme.typography.titleMedium)
        if (items.isEmpty()) {
            Text("Nothing yet.", style = MaterialTheme.typography.bodySmall)
            return
        }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(items, key = { it.id }) { h ->
                Card(Modifier.fillMaxWidth().clickable { onPromote(h) }) {
                    Column(Modifier.padding(12.dp)) {
                        Text(h.displayName, style = MaterialTheme.typography.titleSmall)
                        Text(
                            listOfNotNull(h.mobile, h.email, h.org)
                                .joinToString(" · ").ifBlank { "name only" },
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(
                            "${DateFormat.getDateTimeInstance().format(Date(h.receivedAt))}" +
                                " · ${h.fieldCount} fields" +
                                if (h.promoted) " · saved" else "",
                            style = MaterialTheme.typography.labelSmall,
                        )
                    }
                }
            }
        }
    }

    private fun pair() {
        Pairing.associate(
            this,
            onChooser = { sender ->
                chooser.launch(IntentSenderRequest.Builder(sender).build())
            },
            onFailure = { /* the chooser reports its own failure */ },
        )
    }
}
