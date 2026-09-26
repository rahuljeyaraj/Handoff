package com.handoff.band.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.data.Merge
import com.handoff.band.data.Phones
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
 *
 * ONE BAND, ONE PHONE (docs/band-ownership-brief.md). The band keeps one
 * bond and pairs with nobody else until it is reset — by its button, or by
 * this app's *Forget this band* ([forgetBand]). The reverse also holds: when
 * the band stops honouring this phone's key, because it was reset for a new
 * wearer, the phone forgets it ([dropBand]) exactly as it does when the bond
 * is removed in Bluetooth settings, and the app is back on *Pair a band*.
 */
class BandService : LifecycleService(), BandClient.Listener {

    data class State(
        val address: String? = null,
        val connected: Boolean = false,
        val ready: Boolean = false,
        val status: BandStatus? = null,
        /** The body-link bench readings, twice a second while Advanced is open. */
        val bench: BandBench? = null,
        /**
         * The trigger counters, arriving with each [bench] block. This is the
         * half that says whether the two bands hear EACH OTHER, which the
         * signal numbers cannot.
         */
        val trig: BandTrig? = null,
        /** The same readings kept over time, for the Advanced page's chart. */
        val benchTrace: BenchTrace = BenchTrace.EMPTY,
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
        /** The phone's radio, not the band — see [btWatch]. */
        val bluetoothOff: Boolean = false,
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
        registerReceiver(btWatch, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED))
    }

    /**
     * The phone's Bluetooth radio, not the band's link. Left undetected, the
     * radio going off looked exactly like the band going out of range: the
     * status line sat on "Looking for the band" and then "not found," never
     * saying what was actually wrong (seen 15 Sep). And `autoConnect = true`
     * does not reliably resume once the adapter itself has cycled, so a
     * phone that had it off and back on could sit "not found" forever with
     * nothing prompting a fresh [BluetoothDevice.connectGatt] — the fix is
     * this receiver rebuilding the client the moment the radio comes back,
     * not a user-facing scan (there is no discovery step in a reconnect;
     * see [BandClient]'s class comment on autoConnect).
     */
    private val btWatch = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            when (i.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1)) {
                BluetoothAdapter.STATE_OFF -> {
                    notFoundJob?.cancel()
                    notFoundJob = null
                    client?.close()
                    client = null
                    _state.value = _state.value.copy(
                        bluetoothOff = true, connected = false, ready = false,
                        status = null, notFoundAt = null,
                    )
                    updateNotification(BLUETOOTH_OFF_TEXT)
                }
                BluetoothAdapter.STATE_ON -> {
                    _state.value = _state.value.copy(bluetoothOff = false)
                    val address = _state.value.address ?: return
                    // The user's own Disconnect, not the radio, is why there
                    // is no client — leave that alone.
                    if (prefs.bandOff.value) return
                    client = BandClient(this@BandService, address, this@BandService).also { it.connect() }
                    armNotFoundWatch()
                }
            }
        }
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
     *
     * Not handled here: the band reset for a new wearer while this phone
     * still holds its key. Android 15 does broadcast KEY_MISSING for that,
     * but only to privileged receivers (the bench OnePlus delivered it to
     * Wear and not to us); BandClient's deadline on the first encrypted
     * write catches it instead and arrives as ERR_KEY_REJECTED.
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
                dropBand(reason = null)
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
            // A launch with the radio already off: say so from the start
            // rather than cycling through "Looking for the band" first —
            // btWatch picks it up the moment it comes on.
            val adapterOff = BluetoothAdapter.getDefaultAdapter()?.isEnabled != true
            _state.value = State(address = address, bluetoothOff = adapterOff)
            if (!adapterOff) {
                client = BandClient(this, address, this).also { it.connect() }
                armNotFoundWatch()
            }
        }

        // STICKY, because the whole point is surviving the phone going to
        // sleep in a pocket. The band is re-derived from Pairing on restart.
        return START_STICKY
    }

    override fun onDestroy() {
        runCatching { unregisterReceiver(bondWatch) }
        runCatching { unregisterReceiver(btWatch) }
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

    /**
     * Forget this band, from the Band screen (band-ownership brief §3). If
     * the band is connected on its encrypted link it is told to reset for a
     * new wearer first — the same wipe as the 5 s hold — and the phone side
     * follows once the band has answered: the write's acknowledgement, or
     * the disconnect the band makes as its last step, whichever lands first,
     * with a timeout in case neither does. If the band is not reachable the
     * phone side is cleaned alone and nothing is said about it: the button
     * will clean the band when it next changes hands.
     *
     * [onDone] gets false when the OS bond could not be removed, so the
     * caller can point the wearer at Bluetooth settings by hand.
     */
    fun forgetBand(onDone: (bondRemoved: Boolean) -> Unit) {
        val c = client
        val s = _state.value
        if (c == null || !s.ready || s.status?.encrypted != true) {
            onDone(dropBand(reason = null))
            return
        }
        if (resetPending != null) return          // one at a time
        resetPending = onDone
        Log.i(TAG, "asking ${s.address} to reset for a new wearer")
        c.control(Gatt.reset())
        lifecycleScope.launch {
            delay(RESET_TIMEOUT_MS)
            if (resetPending != null) {
                Log.w(TAG, "no answer to the reset in ${RESET_TIMEOUT_MS} ms; forgetting anyway")
                finishReset()
            }
        }
    }

    private var resetPending: ((Boolean) -> Unit)? = null

    private fun finishReset() {
        val done = resetPending ?: return
        resetPending = null
        done(dropBand(reason = null))
    }

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
        // A client already let go of (dropBand, the MTU-floor rebuild) may
        // still report its own disconnect; there is nothing to track for it,
        // and arming the not-found watch would post a notification for a
        // band this phone has just forgotten.
        if (client == null) return
        _state.value = _state.value.copy(connected = connected, ready = ready)
        if (!connected && resetPending != null) {
            // The band drops the link as the last step of its reset.
            finishReset()
            return
        }
        if (connected) {
            clearNotFoundWatch()
        } else {
            syncInFlight = false
            // The trace goes with the link. A gap in it would draw as a
            // straight line between two readings minutes apart, which is
            // exactly the shape a quiet channel makes.
            _state.value = _state.value.copy(
                status = null, bench = null, trig = null,
                benchTrace = BenchTrace.EMPTY)
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
            phones = card.phones.take(Phones.MAX),
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
            // edits are non-blank and so are never touched. A card whose
            // number contradicts the row it shares an email with is a new
            // contact, not a merge — see Merge.pick.
            val existing = Merge.pick(dao.candidates(incoming.phoneKey, incoming.emailKey), incoming)
            if (existing != null) dao.update(Merge.merge(into = existing, from = incoming))
            else dao.insert(incoming)
        }

        // The wearer's phone was in a pocket. Say what arrived, so the exit
        // criterion can be checked from the lock screen rather than by
        // unlocking and hoping.
        notify("Handshake: ${card.displayName}")
    }

    /*
     * Straight into the state and nowhere else: these are numbers on the
     * Advanced page, not something the app acts on. sync() is deliberately
     * not called — at twice a second it would run the provisioning
     * reconciliation 120 times a minute.
     */
    override fun onBench(bench: BandBench) {
        val s = _state.value
        val sample = BenchSample(
            atMs = SystemClock.elapsedRealtime(),
            signal = bench.signal, noise = bench.noiseRef, present = bench.present,
        )
        _state.value = s.copy(bench = bench, benchTrace = s.benchTrace.plus(sample))
    }

    /*
     * The trigger block follows its bench block by a few milliseconds, so the
     * peak lands on the sample that was just appended rather than opening a
     * second series at the same timestamps.
     */
    override fun onTrig(trig: BandTrig) {
        val s = _state.value
        _state.value = s.copy(trig = trig, benchTrace = s.benchTrace.withPeak(trig.peakSignal))
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

        val card = prefs.ownCard.value?.takeIf { it.complete }
        if (card == null) {
            if (status.provisioned) {
                syncInFlight = true
                pendingErase = true
                prefs.setPushedCard(null)
                c.control(Gatt.forget())
            }
            return
        }

        val hash = card.hash
        if (status.provisioned && hash == prefs.pushedCard.value) return

        syncInFlight = true
        pendingHash = hash
        c.provision(card.vcard())
    }

    override fun onControlWritten(op: Int, ok: Boolean) {
        if (op == Gatt.CTRL_RESET && resetPending != null) {
            // The band does the wipe inside the write, so the acknowledgement
            // means it is done; a failed write means it is not reachable
            // after all, and the phone side goes ahead alone.
            Log.i(TAG, "reset ${if (ok) "acknowledged" else "refused"} by the band")
            finishReset()
        }
    }

    override fun onError(message: String) {
        _state.value = _state.value.copy(lastError = message)
        /*
         * The band refused this phone's key, or refused to pair with it: it
         * has an owner and this phone is no longer — or never was — it
         * (brief §6). The phone cannot know which, and says nothing that
         * claims to; it just lets go of the band, the way it does when the
         * bond is removed in Bluetooth settings. The pairing page, if that
         * is where the wearer is, reads the reason out of [State.lastError],
         * which [dropBand] keeps.
         */
        if (message == BandClient.ERR_KEY_REJECTED || message == BandClient.ERR_BOND_REFUSED) {
            Log.i(TAG, "$message; forgetting ${_state.value.address}")
            dropBand(reason = message)
        }
    }

    /**
     * The phone side of forgetting the band, from inside the service: the
     * link, the stored address, the CompanionDeviceManager association, the
     * OS bond, and the foreground state — the same as [forget], without the
     * stop-intent round trip, and keeping [reason] in the state so a screen
     * watching for it still sees why. Returns false when the OS bond could
     * not be removed.
     */
    private fun dropBand(reason: String?): Boolean {
        val address = Pairing.storedAddress(this) ?: _state.value.address
        client?.close()
        client = null
        notFoundJob?.cancel()
        notFoundJob = null
        resetPending = null
        syncInFlight = false
        _state.value = State(lastError = reason)
        val bondRemoved = address?.let { Pairing.removeBond(this, it) } ?: true
        address?.let { Pairing.disassociate(this, it) }
        Pairing.forget(this)
        prefs.setBandOff(false)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        return bondRemoved
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
        /** How long Forget waits for the band to answer a reset before going ahead. */
        private const val RESET_TIMEOUT_MS = 3_000L
        const val EXTRA_ADDRESS = "address"
        /** [btWatch]'s notification text, and [com.handoff.band.ui.BandView.Connection]'s. */
        const val BLUETOOTH_OFF_TEXT = "Bluetooth is off"

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
         * Forget this band from outside the service, phone side only: the
         * stored address, the CompanionDeviceManager association, the OS
         * bond, and the service (review item 14) — a band left bonded still
         * showed up as paired in Bluetooth settings after this ran. The Band
         * screen's *Forget this band* goes through [forgetBand] instead, so
         * the band is reset too; this is for the pairing page giving up on
         * a band it never finished with, and for a service that is not bound.
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
