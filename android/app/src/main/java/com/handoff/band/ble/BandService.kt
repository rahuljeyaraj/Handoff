package com.handoff.band.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.data.Merge
import com.handoff.band.data.Prefs
import com.handoff.band.ui.MainActivity
import com.handoff.band.vcard.VCard
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * The foreground service that holds the GATT link.
 *
 * THIS IS THE REASON THE CLIENT IS NATIVE. Architecture §11.1 rejected Web
 * Bluetooth over exactly one requirement: the phone is in the wearer's pocket
 * with the screen off when a handshake happens, and a backgrounded browser tab
 * drops the GATT link. A foreground service with `connectedDevice` type is
 * what Android gives you instead, and development plan M2 makes catching an
 * `rx_vcard` notify under those conditions an exit criterion rather than a
 * nice-to-have.
 *
 * Every received card with a way to reach the person is written to the local
 * database here, immediately — the promotion into the system address book is
 * a separate user action (see `contacts/Promote.kt`), so nothing is lost if
 * the phone is locked when the card arrives. A card with neither a phone nor
 * an email is a failed handshake, not a contact (design decisions §4a): it is
 * reported, kept for diagnostics, and never becomes a list entry.
 */
class BandService : LifecycleService(), BandClient.Listener {

    data class State(
        val address: String? = null,
        val connected: Boolean = false,
        val ready: Boolean = false,
        val status: BandStatus? = null,
        val lastError: String? = null,
        /** When a handshake last arrived without a phone or email, and what it said. */
        val lastIncompleteAt: Long? = null,
        val lastIncompleteText: String? = null,
        /**
         * Set 15 s after a connect attempt starts with no link yet (review
         * item 15, O8); cleared the moment the link comes up. Doubles as the
         * one-shot event timestamp for the snackbar on whichever screen is
         * open — see ContactsScreen/BandScreen's LaunchedEffect on it.
         */
        val notFoundAt: Long? = null,
    )

    inner class LocalBinder : Binder() {
        val service: BandService get() = this@BandService
    }

    private val binder = LocalBinder()
    private var client: BandClient? = null

    /** The card the wearer wants on the band, observed for the whole service life. */
    private val prefs by lazy { Prefs.get(this) }

    /** A push or an erase is on the wire; do not send another until it lands. */
    private var syncInFlight = false

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    override fun onBind(intent: Intent): IBinder {
        super.onBind(intent)
        return binder
    }

    override fun onCreate() {
        super.onCreate()
        // Whenever the local card changes - saved, edited, removed - the band
        // should follow. The same sync runs on every status the band sends.
        lifecycleScope.launch { prefs.ownCard.collect { sync() } }
        registerReceiver(bondWatch, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
    }

    /**
     * The band unpaired from Bluetooth settings rather than from the app.
     *
     * Left alone, the service kept the link up and re-ran createBond() on
     * every reconnect, and a band that is connected is a band that is not
     * advertising — so the next "Pair a band" chooser found nothing (seen
     * 13 Sep: bond dropped 10:04, a 20 s scan at 10:06 came back empty).
     * An unpair in settings is the wearer saying Forget, so it does what
     * Forget does. A pairing that never completed goes BONDING -> NONE and
     * is BandClient's to report, not this.
     */
    private val bondWatch = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val who: BluetoothDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            else @Suppress("DEPRECATION") i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            val stored = Pairing.storedAddress(this@BandService) ?: return
            if (!who?.address.equals(stored, ignoreCase = true)) return
            val was = i.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.BOND_NONE)
            val now = i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
            if (was == BluetoothDevice.BOND_BONDED && now == BluetoothDevice.BOND_NONE) {
                Log.i(TAG, "bond for $stored removed outside the app; forgetting it")
                forget(this@BandService)
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)

        // A bound service does not die on stopService() while the activity
        // holds its binding, so "stop" has to be an explicit request that
        // closes the link itself rather than a hope that onDestroy runs.
        if (intent?.action == ACTION_STOP) {
            client?.close()
            client = null
            notFoundJob?.cancel()
            notFoundJob = null
            _state.value = State()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }

        createChannel()
        startForeground(NOTIFICATION_ID, notification("Not connected"))

        val address = intent?.getStringExtra(EXTRA_ADDRESS)
            ?: Pairing.storedAddress(this)

        // No band, or the user switched it off: nothing to hold. NOT_STICKY,
        // or the system would bring us straight back to do nothing again.
        if (address == null || Prefs.get(this).bandOff.value) {
            stopSelf()
            return START_NOT_STICKY
        }

        if (client == null || _state.value.address != address) {
            client?.close()
            _state.value = State(address = address)
            client = BandClient(this, address, this).also { it.connect() }
            armNotFoundWatch()
        }

        // STICKY, because the whole point is surviving the phone going to
        // sleep in a pocket. The band is re-derived from Pairing on restart.
        return START_STICKY
    }

    override fun onDestroy() {
        runCatching { unregisterReceiver(bondWatch) }
        client?.close()
        client = null
        super.onDestroy()
    }

    // ---- things the UI asks for -----------------------------------------

    fun provision(vcardText: String) {
        syncInFlight = true
        pendingHash = vcardText.hashCode().toString(16)
        client?.provision(vcardText)
    }

    fun control(payload: ByteArray) = client?.control(payload)

    /** See [BandClient.forceMtuFloor] — an M2 exit criterion, not a debug toy. */
    var forceMtuFloor: Boolean
        get() = client?.forceMtuFloor ?: false
        set(v) {
            val c = client ?: return
            if (c.forceMtuFloor == v) return
            /*
             * The ATT MTU is negotiated once per connection and cannot be
             * lowered on a live link, so setting the flag alone would leave
             * the band still notifying at the negotiated size. Rebuilding the
             * client is what makes the README procedure -- turn the floor on
             * and repeat 1 and 2 -- actually repeat at the floor.
             */
            val addr = _state.value.address ?: return
            c.close()
            _state.value = _state.value.copy(connected = false, ready = false, status = null)
            /*
             * close() alone does not drop the ACL: the stack keeps the link
             * up for a moment and a new client opened straight away rides it,
             * MTU and all -- measured, the "reconnect" took 70 ms and the band
             * still saw 255. BandClient.close() now disconnects first, and
             * this waits for the link to actually go before reconnecting.
             */
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                if (client != null) return@postDelayed   // something else reconnected
                client = BandClient(this, addr, this).also {
                    it.forceMtuFloor = v
                    it.connect()
                }
            }, 2000)
            client = null
        }

    // ---- BandClient.Listener --------------------------------------------

    override fun onConnectionChanged(connected: Boolean, ready: Boolean) {
        _state.value = _state.value.copy(connected = connected, ready = ready)
        if (connected) {
            clearNotFoundWatch()
        } else {
            syncInFlight = false
            _state.value = _state.value.copy(status = null)
            // autoConnect keeps the controller retrying on its own (BandClient's
            // STATE_DISCONNECTED comment) — this is that retry starting over.
            armNotFoundWatch()
        }
        updateNotification(
            when {
                ready -> "Connected"
                connected -> "Connecting…"
                else -> "Waiting for the band"
            }
        )
    }

    override fun onCardReceived(vcardText: String) {
        val card = VCard.parse(vcardText)
        val incoming = Handshake(
            receivedAt = System.currentTimeMillis(),
            vcard = vcardText,
            displayName = card.displayName,
            mobile = card.mobile,
            work = card.work,
            email = card.email,
            org = card.org,
            title = card.title,
            fieldCount = card.fieldCount,
        )

        // No phone and no email: you cannot reach the person, cannot
        // deduplicate it, cannot usefully save it. The band buzzed for it,
        // so it must not fail silently -- but it is not a contact.
        if (incoming.phoneKey == null && incoming.emailKey == null) {
            _state.value = _state.value.copy(
                lastIncompleteAt = incoming.receivedAt,
                lastIncompleteText = vcardText,
            )
            notify("Handshake didn't complete — try again")
            return
        }

        lifecycleScope.launch {
            val dao = HandoffDb.get(this@BandService).handshakes()
            // Match on phone or email, never on name (design decisions §3).
            // On a match, fill blanks and bump received_at; the user's own
            // edits are non-blank and so are never touched.
            val existing = dao.matching(incoming.phoneKey, incoming.emailKey)
            if (existing != null) dao.update(Merge.merge(into = existing, from = incoming))
            else dao.insert(incoming)
        }

        // The wearer's phone was in a pocket. Say what arrived, so the exit
        // criterion can be checked from the lock screen rather than by
        // unlocking and hoping.
        notify("Handshake: ${card.displayName}")
    }

    override fun onStatus(status: BandStatus) {
        _state.value = _state.value.copy(status = status)
        // A status after an erase reports provisioned=false; that is the
        // acknowledgement, since CTRL_FORGET has no reply of its own.
        if (syncInFlight && pendingErase && !status.provisioned) {
            syncInFlight = false
            pendingErase = false
        }
        sync()
    }

    override fun onProvisioned(ok: Boolean) {
        syncInFlight = false
        if (ok) prefs.setPushedCard(pendingHash)
        pendingHash = null
        _state.value = _state.value.copy(
            lastError = if (ok) null else "Provisioning failed"
        )
    }

    // ---- keeping the band's card in step with the local one --------------

    private var pendingHash: String? = null
    private var pendingErase = false

    /**
     * Self-provision on connect, design decisions §7. The band's status says
     * `provisioned` and a byte count but never what the card is, so the rule
     * is: push when the local card changed since the last push that landed,
     * or when the band reports no card at all. If the local card is gone and
     * the band still holds one, erase it.
     *
     * Gated on the link being encrypted: `ble.c:249` silently drops FORGET
     * on a plain link, and `my_vcard` needs the bond too. The status flag is
     * what says so, which is why this runs from onStatus rather than firing
     * the moment the link comes up.
     */
    private fun sync() {
        val c = client ?: return
        val s = _state.value
        val status = s.status ?: return
        if (!s.ready || !status.encrypted || syncInFlight) return

        val local = prefs.ownCard.value?.takeIf { it.complete }?.vcard()
        if (local == null) {
            if (status.provisioned) {
                syncInFlight = true
                pendingErase = true
                prefs.setPushedCard(null)
                c.control(Gatt.forget())
            }
            return
        }

        val hash = local.hashCode().toString(16)
        if (status.provisioned && hash == prefs.pushedCard.value) return

        syncInFlight = true
        pendingHash = hash
        c.provision(local)
    }

    override fun onError(message: String) {
        _state.value = _state.value.copy(lastError = message)
    }

    // ---- the not-found timer (review item 15, O8) -------------------------

    private var notFoundJob: Job? = null

    /** (Re)start the 15 s countdown to "not found" for a fresh connect attempt. */
    private fun armNotFoundWatch() {
        notFoundJob?.cancel()
        if (_state.value.notFoundAt != null) _state.value = _state.value.copy(notFoundAt = null)
        notFoundJob = lifecycleScope.launch {
            delay(NOT_FOUND_TIMEOUT_MS)
            _state.value = _state.value.copy(notFoundAt = System.currentTimeMillis())
            updateNotification(notFoundText())
        }
    }

    private fun clearNotFoundWatch() {
        notFoundJob?.cancel()
        notFoundJob = null
        if (_state.value.notFoundAt != null) _state.value = _state.value.copy(notFoundAt = null)
    }

    private fun notFoundText(): String = "${Pairing.storedName(this) ?: "Handoff band"} not found"

    // ---- notifications ---------------------------------------------------

    private fun createChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Handoff band",
                NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun notification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CHANNEL)
            .setContentTitle("Handoff")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(open)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, notification(text))
    }

    private fun notify(text: String) {
        getSystemService(NotificationManager::class.java).notify(
            NOTIFICATION_ID + 1,
            Notification.Builder(this, CHANNEL)
                .setContentTitle("Handoff")
                .setContentText(text)
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setAutoCancel(true)
                .build()
        )
    }

    companion object {
        private const val TAG = "HandoffService"
        private const val CHANNEL = "handoff.band"
        private const val NOTIFICATION_ID = 1
        private const val NOT_FOUND_TIMEOUT_MS = 15_000L
        const val EXTRA_ADDRESS = "address"

        fun start(context: Context, address: String? = null) {
            val intent = Intent(context, BandService::class.java)
            address?.let { intent.putExtra(EXTRA_ADDRESS, it) }
            context.startForegroundService(intent)
        }

        private const val ACTION_STOP = "com.handoff.band.STOP"

        private fun stop(context: Context) {
            context.startService(Intent(context, BandService::class.java).setAction(ACTION_STOP))
        }

        /** Drop the link and keep the pairing (design decisions §6). */
        fun disconnect(context: Context) {
            Prefs.get(context).setBandOff(true)
            stop(context)
        }

        /** Undo [disconnect]. */
        fun reconnect(context: Context) {
            Prefs.get(context).setBandOff(false)
            if (Pairing.storedAddress(context) != null) start(context)
        }

        /**
         * Forget this band: the stored address, the CompanionDeviceManager
         * association, the OS bond, and the service (review item 14) — a
         * band left bonded still showed up as paired in Bluetooth settings
         * after this ran.
         *
         * @return false when the OS bond could not be removed, so the caller
         * can point the wearer at Bluetooth settings by hand.
         */
        fun forget(context: Context): Boolean {
            val address = Pairing.storedAddress(context)
            stop(context)
            val bondRemoved = address?.let { Pairing.removeBond(context, it) } ?: true
            address?.let { Pairing.disassociate(context, it) }
            Pairing.forget(context)
            Prefs.get(context).setBandOff(false)
            return bondRemoved
        }
    }
}
