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

    fun storedAddress(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_ADDRESS, null)?.uppercase()

    fun remember(context: Context, address: String) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit { putString(KEY_ADDRESS, address) }
    }

    fun forget(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit { remove(KEY_ADDRESS) }
    }

    /**
     * Ask the OS to show its device chooser. [onChooser] receives the
     * IntentSender to launch; the result comes back to the activity, and
     * [addressFrom] pulls the band out of it.
     */
    fun associate(
        activity: Activity,
        onChooser: (IntentSender) -> Unit,
        onFailure: (CharSequence?) -> Unit,
    ) {
        val manager = activity.getSystemService(CompanionDeviceManager::class.java)

        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(
                ScanFilter.Builder()
                    .setServiceUuid(ParcelUuid(Gatt.SERVICE))
                    .build()
            )
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
     * `CompanionDeviceManager.EXTRA_ASSOCIATION`; below that it is the
     * BluetoothDevice itself under the deprecated extra. Both end in a MAC
     * address, which is all BandClient needs.
     */
    @Suppress("DEPRECATION")
    fun addressFrom(data: android.content.Intent?): String? {
        data ?: return null

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val association = data.getParcelableExtra(
                CompanionDeviceManager.EXTRA_ASSOCIATION,
                android.companion.AssociationInfo::class.java
            )
            // MacAddress.toString() is lowercase; BluetoothAdapter.getRemoteDevice
            // rejects anything but uppercase hex, and rejects it by throwing.
            association?.deviceMacAddress?.toString()?.uppercase()?.let { return it }
        }

        val device: BluetoothDevice? = data.getParcelableExtra(
            CompanionDeviceManager.EXTRA_DEVICE
        )
        return device?.address
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
