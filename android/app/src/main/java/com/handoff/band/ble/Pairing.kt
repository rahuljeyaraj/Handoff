package com.handoff.band.ble

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.regex.Pattern

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
 *
 * NO HARDWARE SCAN FILTER, ANYWHERE. A filtered scan — service UUID, name or
 * address — is offloaded to the controller, which has a handful of filter
 * slots shared by every app on the phone. On the bench handset (OnePlus
 * CPH2569, Android 15) those slots were all taken by other apps, the
 * controller answered our filter add with MEMORY_CAPACITY_EXCEEDED, and the
 * stack then ran the scan against a filter that was never installed:
 * 0 results for 12 s while a PC heard the band at -56 dBm (13 Sep 21:56,
 * `pairing.log`). The OS chooser's own scan takes the same path, which is
 * why the "Handoff band 93D1" filter found the band twice and then never
 * again (pairing-page brief §2). So [locate] scans unfiltered and matches
 * the name in the app, and [associate] gives CompanionDeviceManager a NAME
 * PATTERN rather than a ScanFilter — CDM matches that in Java against the
 * stack's cached device name, and its scan is unfiltered too. CDM stays in
 * the loop for what it is good at: companion status for the foreground
 * service, and an association that survives reinstalls.
 *
 * Even an unfiltered scan needs one slot for its all-pass parameter, and
 * on that handset (Play services and OnePlus's HeyTap Accessory service
 * between them) the slots fill again within minutes of a Bluetooth reset
 * (22:21 the same evening, after four pairings in a row had worked). The
 * app cannot fix that; what it can do is tell it apart from a band that is
 * off: a 12 s unfiltered scan that hears NOTHING — not one advertiser of
 * any kind — is a phone that cannot scan, and [locate] says so.
 */
object Pairing {

    private const val TAG = "HandoffPairing"
    private const val PREFS = "handoff.pairing"
    /** Long enough for a band advertising every 100 ms to be heard several times over. */
    private const val LOCATE_TIMEOUT_MS = 12_000L
    private const val KEY_ADDRESS = "band_address"
    private const val KEY_NAME = "band_name"

    /** What the chooser handed back: the MAC for the client, the name for people. */
    data class Found(val address: String, val name: String?)

    fun storedAddress(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_ADDRESS, null)?.uppercase()

    /**
     * The band's advertised name — "Handoff band 7A3C" (review item 3),
     * built by `ble.c` from the last two bytes of the Pico's unique board
     * id. It is how two bands on one bench are told apart, and the only
     * identity the wearer ever sees: the MAC address appears nowhere in the
     * customer-facing UI (design §2).
     */
    fun storedName(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_NAME, null)

    /**
     * Bumped by every [remember] and [forget], so a screen can re-read the
     * stored band when it changes underneath it — which it does when the
     * service forgets a band that was unpaired from Bluetooth settings.
     */
    private val _version = MutableStateFlow(0)
    val version: StateFlow<Int> = _version

    fun remember(context: Context, found: Found) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putString(KEY_ADDRESS, found.address)
            if (found.name != null) putString(KEY_NAME, found.name) else remove(KEY_NAME)
        }
        _version.value++
    }

    fun forget(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit { remove(KEY_ADDRESS); remove(KEY_NAME) }
        _version.value++
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
        }.onFailure { Log.w(TAG, "disassociate failed", it) }
    }

    /**
     * Drop the OS bond itself (review item 14) — `disassociate` above only
     * revokes this app's permission to hold the device; the pairing keys
     * stay in the Bluetooth stack until this runs too, which is why "Forget"
     * used to leave the band listed as paired in Bluetooth settings.
     *
     * `removeBond()` is a hidden API with no public equivalent, called by
     * reflection; that is the whole reason this can fail; on failure the
     * caller falls back to sending the wearer to Bluetooth settings by hand.
     */
    @SuppressLint("MissingPermission")
    fun removeBond(context: Context, address: String): Boolean = runCatching {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return false
        val device = adapter.getRemoteDevice(address)
        if (device.bondState == BluetoothDevice.BOND_NONE) return true
        BluetoothDevice::class.java.getMethod("removeBond").invoke(device) as? Boolean ?: false
    }.getOrElse {
        Log.w(TAG, "removeBond failed", it)
        false
    }

    /**
     * Ask the OS to show its device chooser. [onChooser] receives the
     * IntentSender to launch; the result comes back to the activity, and
     * [foundFrom] pulls the band out of it. [onFailure] fires on a discovery
     * timeout — the OS scans for 20 s — and must be surfaced by the caller:
     * the chooser itself shows nothing on failure.
     *
     * With a code from the label the filter is that one band's name, so the
     * OS's unavoidable confirmation lists exactly one device (design
     * decisions §2a). The name is a pattern, not a ScanFilter, for the
     * reason in the class comment. Without a code — the bench path — it is
     * any "Handoff band".
     */
    fun associate(
        activity: Activity,
        target: BandCode? = null,
        onChooser: (IntentSender) -> Unit,
        onFailure: (CharSequence?) -> Unit,
    ) {
        val manager = activity.getSystemService(CompanionDeviceManager::class.java)

        val name = target?.name ?: BandCode.NAME_PREFIX
        val filter = BluetoothLeDeviceFilter.Builder()
            .setNamePattern(Pattern.compile(Pattern.quote(name), Pattern.CASE_INSENSITIVE))
            .build()

        val request = AssociationRequest.Builder()
            .addDeviceFilter(filter)
            .setSingleDevice(true)
            .build()

        Log.i(TAG, "associate: name pattern $name")
        manager.associate(request, object : CompanionDeviceManager.Callback() {
            @Deprecated("Replaced by onAssociationPending on API 33")
            override fun onDeviceFound(intentSender: IntentSender) = onChooser(intentSender)

            override fun onAssociationPending(intentSender: IntentSender) =
                onChooser(intentSender)

            override fun onFailure(error: CharSequence?) {
                Log.w(TAG, "associate failed: $error")
                onFailure(error)
            }
        }, null)
    }

    /** A running [locate] scan; [cancel] when the page that asked for it goes. */
    class Locate internal constructor(private val stop: () -> Unit) {
        fun cancel() = stop()
    }

    /**
     * Find the band the label names, by a scan of our own before the OS
     * chooser is asked: unfiltered (class comment), the name — which only
     * the scan response carries — compared here once the stack has merged
     * the two halves of the scan record. Ends with the band's address and
     * name, or [onNotFound] after [LOCATE_TIMEOUT_MS] with nothing matching:
     * a quicker and better-worded failure than the chooser's 20 s silence,
     * and the chooser only opens once the band is known to be on the air.
     * [onNotFound] is told how many advertisements of any kind the scan
     * heard; zero means the phone is not scanning at all (class comment).
     *
     * Returns null when this handset cannot scan (permission refused,
     * Bluetooth off), and the caller goes straight to the chooser.
     *
     * The permission is BLUETOOTH_SCAN with `neverForLocation` on API 31+
     * (no location prompt); below that it is the location permission the
     * app already asks for.
     */
    @SuppressLint("MissingPermission")   // checked just below
    fun locate(
        context: Context,
        target: BandCode,
        onFound: (Found) -> Unit,
        onNotFound: (heard: Int) -> Unit,
    ): Locate? {
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            Manifest.permission.BLUETOOTH_SCAN else Manifest.permission.ACCESS_FINE_LOCATION
        if (ContextCompat.checkSelfPermission(context, permission) != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "locate: no scan permission, leaving it to the chooser")
            return null
        }
        val scanner = BluetoothAdapter.getDefaultAdapter()?.takeIf { it.isEnabled }?.bluetoothLeScanner
        if (scanner == null) {
            Log.w(TAG, "locate: no LE scanner, leaving it to the chooser")
            return null
        }

        val handler = Handler(Looper.getMainLooper())
        var live = true
        var heard = 0
        val bands = LinkedHashMap<String, String?>()
        lateinit var finish: (Found?) -> Unit
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                if (!live) return
                heard++
                val rec = r.scanRecord
                val name = rec?.deviceName ?: runCatching { r.device.name }.getOrNull()
                val ours = rec?.serviceUuids?.any { it.uuid == Gatt.SERVICE } == true ||
                    name?.startsWith(BandCode.NAME_PREFIX, ignoreCase = true) == true
                if (ours && bands.put(r.device.address, name) == null)
                    Log.i(TAG, "locate: band ${r.device.address} rssi=${r.rssi} name=$name")
                val hit = name.equals(target.name, ignoreCase = true) ||
                    r.device.address.equals(target.address, ignoreCase = true)
                if (hit) finish(Found(r.device.address.uppercase(), name ?: target.name))
            }
            override fun onScanFailed(errorCode: Int) {
                Log.e(TAG, "locate: scan failed $errorCode")
                if (live) finish(null)
            }
        }
        val timeout = Runnable { if (live) finish(null) }
        finish = { found ->
            live = false
            handler.removeCallbacks(timeout)
            runCatching { scanner.stopScan(cb) }
            Log.i(TAG, "locate: ${if (found != null) "found ${found.address}" else "not found"} " +
                "after $heard results, bands=$bands")
            if (found != null) onFound(found) else onNotFound(heard)
        }

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        Log.i(TAG, "locate: looking for ${target.name}")
        scanner.startScan(null, settings, cb)
        handler.postDelayed(timeout, LOCATE_TIMEOUT_MS)

        return Locate {
            if (!live) return@Locate
            live = false
            handler.removeCallbacks(timeout)
            runCatching { scanner.stopScan(cb) }
        }
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
