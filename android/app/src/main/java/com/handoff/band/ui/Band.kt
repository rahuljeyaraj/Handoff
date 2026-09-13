package com.handoff.band.ui

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import com.handoff.band.ble.BandService
import com.handoff.band.ui.components.BatteryLevel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * The activity's handle on [BandService]: one binding, one flow of state, for
 * every screen. Screens reach it through [LocalBand] rather than each binding
 * the service themselves.
 */
class BandConnection(private val context: Context, private val scope: CoroutineScope) :
    ServiceConnection {

    var service: BandService? = null
        private set

    private val _state = MutableStateFlow<BandService.State?>(null)
    val state: StateFlow<BandService.State?> = _state

    private var mirror: Job? = null
    private var bound = false

    fun bind() {
        if (bound) return
        bound = context.bindService(Intent(context, BandService::class.java), this,
                                    Context.BIND_AUTO_CREATE)
    }

    fun unbind() {
        if (!bound) return
        runCatching { context.unbindService(this) }
        bound = false
        mirror?.cancel()
        service = null
    }

    override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
        val s = (binder as BandService.LocalBinder).service
        service = s
        // Mirror the service's own flow into one the composables collect, so
        // the UI has a single source whether the service is bound yet or not.
        mirror?.cancel()
        mirror = scope.launch { s.state.collect { _state.value = it } }
    }

    override fun onServiceDisconnected(name: ComponentName?) {
        service = null
        mirror?.cancel()
        _state.value = null
    }
}

val LocalBand = staticCompositionLocalOf<BandConnection> {
    error("No BandConnection provided")
}

/**
 * What the customer-facing screens say about the band, derived once from the
 * service's state. Four facts, all readable without a tap (design §1): name,
 * connection, whether your card is on the band, battery.
 */
data class BandView(
    val paired: Boolean,
    val name: String,
    val connection: Connection,
    /** null until the band has reported a status. */
    val cardOnBand: Boolean?,
    val battery: BatteryLevel,
    /** "0.2.0", or null until a version-2 band has reported. */
    val firmware: String?,
    /** The band's own vibrate preference (review item 11). True until reported. */
    val hapticOn: Boolean,
) {
    enum class Connection(val label: String) {
        NOT_PAIRED("Not paired"),
        OFF("Disconnected"),
        WAITING("Looking for the band"),
        /** Label unused — the status line interpolates the band's name (Copy G). */
        NOT_FOUND("Not found"),
        CONNECTING("Connecting…"),
        CONNECTED("Connected"),
    }

    /** The status line's second line (review item 15, Copy G). */
    val statusLabel: String
        get() = if (connection == Connection.NOT_FOUND) "$name not found" else connection.label

    companion object {
        /**
         * [address] is the stored pairing, which outlives the service: once
         * the user disconnects, the service is gone and its state is null,
         * but the band is still theirs.
         */
        fun from(state: BandService.State?, address: String?, name: String?, off: Boolean): BandView {
            val paired = address != null
            val connection = when {
                !paired -> Connection.NOT_PAIRED
                off -> Connection.OFF
                state?.ready == true -> Connection.CONNECTED
                state?.connected == true -> Connection.CONNECTING
                state?.notFoundAt != null -> Connection.NOT_FOUND
                else -> Connection.WAITING
            }
            return BandView(
                paired = paired,
                // The advertised name, "Handoff band 7A3C" (review item 3).
                // A band paired before names were stored has none, and one
                // paired before the rename shows "Handoff band" until
                // forgotten and re-paired — both fall back the same way.
                name = name ?: "Handoff band",
                connection = connection,
                cardOnBand = state?.status?.provisioned,
                battery = BatteryLevel.from(state?.status),
                firmware = state?.status?.firmware,
                hapticOn = state?.status?.hapticOn ?: true,
            )
        }
    }
}

@Composable
fun bandView(state: BandService.State?, address: String?, name: String?, off: Boolean): BandView =
    BandView.from(state, address, name, off)
