package com.handoff.band

import android.app.Application
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Pairing

/**
 * Nothing here but one decision: if a band has already been paired, start
 * holding its connection as soon as the process exists, without waiting for
 * anybody to open the app.
 *
 * That is the shape architecture §11.1 asks for. A handshake happens with the
 * phone in a pocket, so the app being open is exactly the condition that will
 * not hold when it matters.
 */
class HandoffApp : Application() {
    override fun onCreate() {
        super.onCreate()
        if (Pairing.storedAddress(this) != null) {
            BandService.start(this)
        }
    }
}
