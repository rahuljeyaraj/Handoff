package com.handoff.band.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.ArrayDeque

/**
 * The GATT client. One wristband, held for as long as the service lives.
 *
 * THREE THINGS THIS CLASS EXISTS TO GET RIGHT, all of them things a naive BLE
 * client gets wrong and only finds out about on somebody else's phone:
 *
 * 1. ONE OPERATION AT A TIME. Android's BluetoothGatt silently drops a write
 *    or a descriptor write issued while another is outstanding — it returns
 *    false, or worse, true — so everything goes through [queue] and the next
 *    operation starts from the previous one's callback. A chunked vCard at the
 *    23-byte MTU floor is ten writes, and ten is where this stops being
 *    theoretical.
 *
 * 2. RECONNECTION IS `autoConnect = true`. The band is a battery device that
 *    will go out of range and come back, and M2's last exit criterion is that
 *    it reconnects on its stored bond with no fresh OS pairing dialog.
 *    autoConnect hands that to the controller instead of to a retry loop the
 *    OS would kill in the background.
 *
 * 3. THE MTU IS NOT ASSUMED. [forceMtuFloor] exists so a developer can test at
 *    23 bytes on a handset that would happily negotiate 247 — development plan
 *    M2 names that as an exit criterion precisely because the negotiated path
 *    is the one that works by accident.
 */
@SuppressLint("MissingPermission")   // the service checks before constructing us
class BandClient(
    private val context: Context,
    private val address: String,
    private val listener: Listener,
) {
    interface Listener {
        fun onConnectionChanged(connected: Boolean, ready: Boolean)
        fun onCardReceived(vcardText: String)
        fun onStatus(status: BandStatus)
        fun onBench(bench: BandBench)
        fun onTrig(trig: BandTrig)
        fun onBank(bank: BandBank)
        fun onProvisioned(ok: Boolean)
        /** A control write was answered: [op] is its opcode, [ok] the ATT status. */
        fun onControlWritten(op: Int, ok: Boolean)
        fun onError(message: String)
    }

    /**
     * Pretend the ATT MTU is 23 whatever the phone negotiated. Not a debug
     * nicety: it is how the floor gets tested without hunting for an old
     * handset.
     */
    var forceMtuFloor: Boolean = false

    private var gatt: BluetoothGatt? = null
    private var device: BluetoothDevice? = null
    private var bondWatch: BroadcastReceiver? = null
    private var mtu = Gatt.MIN_ATT_MTU
    private val rxAssembler = Assembler()

    /** Serialised GATT operations. See point 1 in the class comment. */
    private val pending = ArrayDeque<() -> Boolean>()
    private var busy = false

    /**
     * The first encrypted operation on a bonded link gets a deadline. A
     * band that was reset for a new wearer no longer has this phone's key;
     * the stack finds that out when it tries to encrypt for the rx_vcard
     * CCCD write, and then — measured on the OnePlus / Android 15 — drops
     * the write without a callback, every reconnect, forever. (It also
     * broadcasts KEY_MISSING the first time, to privileged receivers only
     * on Android 15 — not to us.) One ATT round trip after a sub-second
     * encryption does not take five seconds. Walked 15 Sep: a stale bond
     * on relaunch, and a live reset with the app running, both back on
     * "Pair a band" five seconds after the reconnect.
     */
    private val main = Handler(Looper.getMainLooper())
    private val keyDeadline = Runnable {
        if (!bonded()) return@Runnable
        Log.i(TAG, "encrypted subscribe got no answer in ${KEY_DEADLINE_MS} ms; key rejected")
        pending.clear()
        busy = false
        listener.onError(ERR_KEY_REJECTED)
    }

    val chunkBytes: Int
        get() = (if (forceMtuFloor) Gatt.MIN_ATT_MTU else mtu) - Gatt.ATT_NOTIFY_OVERHEAD

    // ---- lifecycle -------------------------------------------------------

    fun connect() {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: run {
            listener.onError("no Bluetooth adapter")
            return
        }
        val dev = adapter.getRemoteDevice(address)
        device = dev
        gatt = dev.connectGatt(context, /* autoConnect = */ true, callback,
            BluetoothDevice.TRANSPORT_LE)
    }

    fun close() {
        pending.clear()
        busy = false
        main.removeCallbacks(keyDeadline)
        stopBondWatch()
        // disconnect() before close(): close() alone leaves the ACL up, and
        // the next client would inherit its negotiated MTU (see BandService).
        gatt?.let { runCatching { it.disconnect() }; it.close() }
        gatt = null
    }

    // ---- bonding ---------------------------------------------------------

    /*
     * Bond explicitly rather than trusting the stack to pair on the band's
     * INSUFFICIENT_ENCRYPTION reply to the rx_vcard CCCD write. Measured on
     * a OnePlus CPH2569 / Android 15: the reply arrives, no pairing starts,
     * no callback ever fires, and the operation queue stalls forever. The
     * OS pairing dialog still appears at the first moment the encrypted link
     * is needed, which is the behaviour architecture §11.3 asks for.
     */
    private fun bonded(): Boolean =
        device?.bondState == BluetoothDevice.BOND_BONDED

    private fun bondThen(next: () -> Unit) {
        val dev = device ?: return
        if (bondWatch != null) return          // already waiting on one

        val watch = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                val who: BluetoothDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                    i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                else @Suppress("DEPRECATION") i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                if (who?.address != dev.address) return

                when (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                    BluetoothDevice.BOND_BONDED -> { stopBondWatch(); next() }
                    BluetoothDevice.BOND_NONE -> {
                        stopBondWatch()
                        /*
                         * The band declines a phone that is not its owner
                         * (ble.c, SM_EVENT_JUST_WORKS_REQUEST) and the stack
                         * reports that the same way as the wearer dismissing
                         * the pairing dialog. The unbond reason tells them
                         * apart on the stacks that fill it in; a stack that
                         * does not is read as the band's refusal, since that
                         * message at least tells the wearer what to do.
                         */
                        val reason = i.getIntExtra(EXTRA_UNBOND_REASON, -1)
                        Log.i(TAG, "bond with ${dev.address} not made, reason $reason")
                        listener.onError(
                            if (reason == UNBOND_REASON_AUTH_CANCELED) ERR_BOND_CANCELLED
                            else ERR_BOND_REFUSED)
                    }
                }
            }
        }
        bondWatch = watch
        context.registerReceiver(watch,
            IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))

        if (!dev.createBond()) {
            stopBondWatch()
            listener.onError("could not start pairing")
        }
    }

    private fun stopBondWatch() {
        bondWatch?.let { runCatching { context.unregisterReceiver(it) } }
        bondWatch = null
    }

    // ---- outbound --------------------------------------------------------

    /**
     * Write the wearer's own card, chunked. Architecture §9's provisioning
     * path, and the direction the band re-encodes into compact TLV once and
     * then keeps in flash.
     */
    fun provision(vcardText: String) {
        val ch = characteristic(Gatt.MY_VCARD) ?: run {
            listener.onError("my_vcard not found — is the band running the M2 image?")
            return
        }
        val chunks = Chunk.split(vcardText.toByteArray(Charsets.UTF_8), chunkBytes)
        chunks.forEachIndexed { i, chunk ->
            queue { write(ch, chunk, last = i == chunks.lastIndex) }
        }
    }

    fun control(payload: ByteArray) {
        val ch = characteristic(Gatt.CONTROL) ?: return
        queue { write(ch, payload, last = false) }
    }

    // ---- the operation queue --------------------------------------------

    private fun queue(op: () -> Boolean) {
        pending.add(op)
        pump()
    }

    private fun pump() {
        if (busy) return
        val op = pending.poll() ?: return
        busy = true
        if (!op()) {
            busy = false
            listener.onError("GATT rejected an operation")
            pump()
        }
    }

    private fun done() {
        busy = false
        pump()
    }

    // ---- GATT plumbing ---------------------------------------------------

    private fun characteristic(uuid: java.util.UUID): BluetoothGattCharacteristic? =
        gatt?.getService(Gatt.SERVICE)?.getCharacteristic(uuid)

    private var provisioningTail = false

    /** The opcode of the control write in flight, for onCharacteristicWrite. */
    private var controlOp = -1

    private fun write(
        ch: BluetoothGattCharacteristic,
        value: ByteArray,
        last: Boolean,
    ): Boolean {
        provisioningTail = last
        if (ch.uuid == Gatt.CONTROL) controlOp = value.firstOrNull()?.toInt()?.and(0xFF) ?: -1
        val g = gatt ?: return false
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(
                ch, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            ch.value = value
            @Suppress("DEPRECATION")
            ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            g.writeCharacteristic(ch)
        }
    }

    private fun readStatus(): Boolean {
        val g = gatt ?: return false
        val ch = characteristic(Gatt.STATUS) ?: return false
        return g.readCharacteristic(ch)
    }

    private fun subscribe(uuid: java.util.UUID): Boolean {
        val g = gatt ?: return false
        val ch = g.getService(Gatt.SERVICE)?.getCharacteristic(uuid) ?: return false
        if (!g.setCharacteristicNotification(ch, true)) return false
        val cccd = ch.getDescriptor(Gatt.CCCD) ?: return false
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            g.writeDescriptor(cccd)
        }
    }

    private val callback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    listener.onConnectionChanged(connected = true, ready = false)
                    /*
                     * Asking for 247 is what makes rx_vcard arrive in one
                     * chunk on a modern handset. The band sizes its notify
                     * chunks from the NEGOTIATED MTU (ble.c notify_room()),
                     * so forcing the floor app-side only shrinks the my_vcard
                     * write direction unless we also decline to negotiate --
                     * and then criterion 4 tests reassembly in neither
                     * direction it claims to. Leaving the MTU at the 23-byte
                     * default is the only thing that makes the band chunk.
                     */
                    if (forceMtuFloor) {
                        mtu = Gatt.MIN_ATT_MTU
                        g.discoverServices()
                    } else {
                        g.requestMtu(247)   // discovery waits for the MTU answer
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    pending.clear()
                    busy = false
                    rxAssembler.reset()
                    mtu = Gatt.MIN_ATT_MTU
                    listener.onConnectionChanged(connected = false, ready = false)
                    // autoConnect keeps the controller trying; nothing to do.
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            mtu = if (status == BluetoothGatt.GATT_SUCCESS) newMtu else Gatt.MIN_ATT_MTU
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (g.getService(Gatt.SERVICE) == null) {
                listener.onError("Handoff service not found on this device")
                return
            }

            /*
             * rx_vcard's CCCD is WRITE_ENCRYPTED in ble_service.gatt, so the
             * bond has to exist before the subscribe goes out — see bondThen()
             * for why it is made explicitly rather than left to the stack.
             */
            val subscribeAll = {
                queue {
                    // The one write the deadline above watches: encrypted,
                    // and first. Only when the bond predates this connect;
                    // a bond just made has a key the band accepted seconds ago.
                    if (bonded()) main.postDelayed(keyDeadline, KEY_DEADLINE_MS)
                    subscribe(Gatt.RX_VCARD)
                }
                queue { subscribe(Gatt.STATUS) }
                // Unencrypted by design, so a bench session needs no pairing
                // dance to see anything. Subscribing IS the request: the band
                // pushes nothing to a phone that did not ask.
                queue { subscribe(Gatt.TELEMETRY) }
                // A read as well as the subscription: status only notifies
                // on change, and the service needs a baseline to decide
                // whether the band already holds the wearer's card.
                queue { readStatus() }
                listener.onConnectionChanged(connected = true, ready = true)
            }
            if (bonded()) subscribeAll() else bondThen(subscribeAll)
        }

        /*
         * The band no longer holds this phone's key: it was reset for a new
         * wearer, by the button or by another phone's app. The phone cannot
         * know that; what it sees is an encrypted operation that the stack
         * could not make good on with the key it has. Reported as its own
         * error so the service can forget the band the way it does when the
         * bond is removed in Bluetooth settings (band-ownership brief §6).
         * Only while bonded — unbonded, these statuses mean "pair first".
         */
        private fun keyRejected(status: Int): Boolean =
            bonded() && (status == BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION ||
                         status == BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION ||
                         status == GATT_AUTH_FAIL)

        override fun onDescriptorWrite(
            g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int,
        ) {
            main.removeCallbacks(keyDeadline)
            if (keyRejected(status)) {
                pending.clear()
                listener.onError(ERR_KEY_REJECTED)
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                listener.onError("could not subscribe (status $status)")
            }
            done()
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int,
        ) {
            if (keyRejected(status)) {
                pending.clear()
                if (ch.uuid == Gatt.MY_VCARD) listener.onProvisioned(false)
                listener.onError(ERR_KEY_REJECTED)
            } else if (ch.uuid == Gatt.MY_VCARD) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    pending.clear()
                    listener.onProvisioned(false)
                } else if (provisioningTail) {
                    listener.onProvisioned(true)
                }
            } else if (ch.uuid == Gatt.CONTROL) {
                val op = controlOp
                controlOp = -1
                listener.onControlWritten(op, status == BluetoothGatt.GATT_SUCCESS)
            }
            done()
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray, status: Int,
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) deliver(ch, value)
            done()
        }

        @Deprecated("Superseded by the four-argument overload on API 33")
        override fun onCharacteristicRead(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int,
        ) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
            @Suppress("DEPRECATION")
            if (status == BluetoothGatt.GATT_SUCCESS) ch.value?.let { deliver(ch, it) }
            done()
        }

        // API 33+ delivers the value; below it, read it off the characteristic.
        override fun onCharacteristicChanged(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray,
        ) = deliver(ch, value)

        @Deprecated("Superseded by the three-argument overload on API 33")
        override fun onCharacteristicChanged(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            deliver(ch, ch.value ?: return)
        }

        private fun deliver(ch: BluetoothGattCharacteristic, value: ByteArray) {
            when (ch.uuid) {
                Gatt.RX_VCARD -> when (val r = rxAssembler.push(value)) {
                    is ChunkResult.More -> Unit
                    is ChunkResult.Complete ->
                        listener.onCardReceived(String(r.data, Charsets.UTF_8))
                    is ChunkResult.Error -> {
                        Log.w(TAG, "rx_vcard chunk rejected: ${r.why}")
                        listener.onError("a chunk was rejected: ${r.why}")
                    }
                }
                Gatt.STATUS -> BandStatus.parse(value)?.let(listener::onStatus)
                // Four things ride this characteristic: the bench block, the
                // trigger block, the bank block, and the score stream. Each
                // tagged parse rejects the others, and an untagged 16-byte
                // score block falls through all of them harmlessly.
                Gatt.TELEMETRY -> {
                    BandBench.parse(value)?.let {
                        Log.i(TAG, "bench ${it.line()}")
                        listener.onBench(it)
                    }
                    BandTrig.parse(value)?.let {
                        Log.i(TAG, "trig ${it.line()}")
                        listener.onTrig(it)
                    }
                    BandBank.parse(value)?.let {
                        Log.i(TAG, it.line())
                        listener.onBank(it)
                    }
                }
                else -> Unit
            }
        }
    }

    companion object {
        private const val TAG = "BandClient"

        /**
         * The bond went BONDING -> NONE because the band declined: it has
         * an owner and this phone is not it (band-ownership brief §4). The
         * pairing page turns this into "Band in use".
         */
        const val ERR_BOND_REFUSED = "the band refused to pair"

        /** The bond went BONDING -> NONE because the wearer dismissed the dialog. */
        const val ERR_BOND_CANCELLED = "pairing was cancelled"

        /**
         * Bonded, and the band would not honour the key: it has been reset
         * since. The service forgets the band on this (brief §6).
         */
        const val ERR_KEY_REJECTED = "the band no longer accepts this phone"

        /**
         * BluetoothDevice.EXTRA_REASON and UNBOND_REASON_AUTH_CANCELED are
         * @hide, so the values are copied; a stack that omits the extra
         * yields -1, which reads as the band's refusal.
         */
        private const val EXTRA_UNBOND_REASON = "android.bluetooth.device.extra.REASON"
        private const val UNBOND_REASON_AUTH_CANCELED = 3

        /** GATT_AUTH_FAIL (0x89): the stack tried to encrypt for us and could not. */
        private const val GATT_AUTH_FAIL = 0x89

        /** See [keyDeadline]. */
        private const val KEY_DEADLINE_MS = 5_000L
    }
}
