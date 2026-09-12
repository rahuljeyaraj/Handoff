package com.handoff.band.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.data.Merge
import com.handoff.band.ui.MainActivity
import com.handoff.band.vcard.VCard
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
    )

    inner class LocalBinder : Binder() {
        val service: BandService get() = this@BandService
    }

    private val binder = LocalBinder()
    private var client: BandClient? = null

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    override fun onBind(intent: Intent): IBinder {
        super.onBind(intent)
        return binder
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)

        createChannel()
        startForeground(NOTIFICATION_ID, notification("Not connected"))

        val address = intent?.getStringExtra(EXTRA_ADDRESS)
            ?: Pairing.storedAddress(this)

        if (address == null) {
            stopSelf()
            return START_NOT_STICKY
        }

        if (client == null || _state.value.address != address) {
            client?.close()
            _state.value = State(address = address)
            client = BandClient(this, address, this).also { it.connect() }
        }

        // STICKY, because the whole point is surviving the phone going to
        // sleep in a pocket. The band is re-derived from Pairing on restart.
        return START_STICKY
    }

    override fun onDestroy() {
        client?.close()
        client = null
        super.onDestroy()
    }

    // ---- things the UI asks for -----------------------------------------

    fun provision(vcardText: String) = client?.provision(vcardText)

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
    }

    override fun onProvisioned(ok: Boolean) {
        _state.value = _state.value.copy(
            lastError = if (ok) null else "Provisioning failed"
        )
    }

    override fun onError(message: String) {
        _state.value = _state.value.copy(lastError = message)
    }

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
        private const val CHANNEL = "handoff.band"
        private const val NOTIFICATION_ID = 1
        const val EXTRA_ADDRESS = "address"

        fun start(context: Context, address: String? = null) {
            val intent = Intent(context, BandService::class.java)
            address?.let { intent.putExtra(EXTRA_ADDRESS, it) }
            context.startForegroundService(intent)
        }
    }
}
