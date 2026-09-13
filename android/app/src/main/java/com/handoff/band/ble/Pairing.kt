package com.handoff.band.ble

import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.bluetooth.le.ScanFilter
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import androidx.core.content.edit

/**
 * First-run pairing, architecture §11.3.
 *
 * CompanionDeviceManager rather than a raw scan, for three reasons that all
 * matter to a device the wearer keeps on their wrist all day:
 *
 *   - the OS runs the scan and shows the chooser, so the app never needs the
 *     location permission a self-run BLE scan implies
 *   - the association survives reboots and app updates, so there is one
 *     dialog in the device's life rather than one per session
 *   - an associated companion is allowed to keep a connection and run a
 *     foreground service for it, which is exactly what BandService does
 *
 * The filter is the Handoff service UUID, which is why `ble.c` puts it in the
 * ADVERTISEMENT rather than the scan response: CompanionDeviceManager matches
 * on what is advertised, and a band that only answers an active scan would
 * never appear in the chooser.
 */
object Pairing {

    private const val PREFS = "handoff.pairing"
    private const val KEY_ADDRESS = "band_address"
    private const val KEY_NAME = "band_name"

    /** What the chooser handed back: the MAC for the client, the name for people. */
    data class Found(val address: String, val name: String?)

    fun storedAddress(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_ADDRESS, null)?.uppercase()

    /**
     * The band's advertised name — "Handoff 7A3C", built by `ble.c` from the
     * last two bytes of the Pico's unique board id. It is how two bands on one
     * bench are told apart, and the only identity the wearer ever sees: the
     * MAC address appears nowhere in the customer-facing UI (design §2).
     */
    fun storedName(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_NAME, null)

    fun remember(context: Context, found: Found) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putString(KEY_ADDRESS, found.address)
            if (found.name != null) putString(KEY_NAME, found.name) else remove(KEY_NAME)
        }
    }

    fun forget(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit { remove(KEY_ADDRESS); remove(KEY_NAME) }
    }

    /**
     * Undo [associate]. On API 33+ associations are addressed by id; below
     * that, by MAC. Either way the OS forgets it allowed this app to hold
     * that device, which is what "Forget this band" promises.
     */
    @Suppress("DEPRECATION")
    fun disassociate(context: Context, address: String) {
        val manager = context.getSystemService(CompanionDeviceManager::class.java) ?: return
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                manager.myAssociations
                    .filter { it.deviceMacAddress?.toString()?.equals(address, ignoreCase = true) == true }
                    .forEach { manager.disassociate(it.id) }
            } else {
                manager.disassociate(address)
            }
        }.onFailure { Log.w("HandoffPairing", "disassociate failed", it) }
    }

    /**
     * Ask the OS to show its device chooser. [onChooser] receives the
     * IntentSender to launch; the result comes back to the activity, and
     * [addressFrom] pulls the band out of it.
     */
    fun associate(
        activity: Activity,
        target: BandCode? = null,
        onChooser: (IntentSender) -> Unit,
        onFailure: (CharSequence?) -> Unit,
    ) {
        val manager = activity.getSystemService(CompanionDeviceManager::class.java)

        /*
         * With a code from the label the filter names that one band, so the
         * OS's unavoidable confirmation lists exactly one device (design
         * decisions §2a). The service UUID stays in the filter either way.
         * Without a code - the bench path - it is any Handoff band in range.
         */
        val scan = ScanFilter.Builder().setServiceUuid(ParcelUuid(Gatt.SERVICE))
        if (target != null) {
            scan.setDeviceName(target.name)
            target.address?.let { scan.setDeviceAddress(it) }
        }
        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(scan.build())
            .build()

        val request = AssociationRequest.Builder()
            .addDeviceFilter(filter)
            .setSingleDevice(true)
            .build()

        manager.associate(request, object : CompanionDeviceManager.Callback() {
            @Deprecated("Replaced by onAssociationPending on API 33")
            override fun onDeviceFound(intentSender: IntentSender) = onChooser(intentSender)

            override fun onAssociationPending(intentSender: IntentSender) =
                onChooser(intentSender)

            override fun onFailure(error: CharSequence?) = onFailure(error)
        }, null)
    }

    /**
     * The chooser result. On API 33+ the association is delivered as a
     * `CompanionDeviceManager.EXTRA_ASSOCIATION`, which carries the display
     * name the chooser showed; below that it is the BluetoothDevice itself
     * under the deprecated extra, and its name is the cached one.
     *
     * Do not read the name from the GATT GAP characteristic later: that one
     * is the generic "Handoff" for every board, because `gap_set_local_name()`
     * belongs to BTstack's Classic half (`ble.c:457`).
     */
    @Suppress("DEPRECATION", "MissingPermission")
    fun foundFrom(data: android.content.Intent?): Found? {
        data ?: return null

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val association = data.getParcelableExtra(
                CompanionDeviceManager.EXTRA_ASSOCIATION,
                android.companion.AssociationInfo::class.java
            )
            // MacAddress.toString() is lowercase; BluetoothAdapter.getRemoteDevice
            // rejects anything but uppercase hex, and rejects it by throwing.
            association?.deviceMacAddress?.toString()?.uppercase()?.let { address ->
                return Found(address, association.displayName?.toString()?.takeIf { it.isNotBlank() })
            }
        }

        val device: BluetoothDevice? = data.getParcelableExtra(
            CompanionDeviceManager.EXTRA_DEVICE
        )
        return device?.let { Found(it.address, runCatching { it.name }.getOrNull()) }
    }

    /**
     * Diagnostic only: an unfiltered ten-second scan, every result logged.
     * For when the chooser above comes back empty and the question is whether
     * this handset hears the band at all, or hears it and fails the filter.
     */
    @Suppress("MissingPermission")
    fun debugScan(context: Context, onDone: (String) -> Unit) {
        val scanner = BluetoothAdapter.getDefaultAdapter()?.bluetoothLeScanner
            ?: return onDone("no LE scanner")
        val seen = LinkedHashMap<String, String>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                val rec = r.scanRecord
                val line = "${r.device.address} rssi=${r.rssi} name=${rec?.deviceName} " +
                    "uuids=${rec?.serviceUuids} raw=${rec?.bytes?.joinToString("") { "%02x".format(it) }}"
                if (seen.put(r.device.address, line) == null) Log.i("HandoffScan", line)
            }
            override fun onScanFailed(errorCode: Int) { Log.e("HandoffScan", "failed $errorCode") }
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(null, settings, cb)
        Handler(Looper.getMainLooper()).postDelayed({
            scanner.stopScan(cb)
            val hit = seen.values.firstOrNull { it.contains("48414e44-0001", ignoreCase = true) || it.contains("Handoff") }
            onDone("scan: ${seen.size} devices, band ${hit ?: "NOT seen"}")
        }, 10_000)
    }
}
