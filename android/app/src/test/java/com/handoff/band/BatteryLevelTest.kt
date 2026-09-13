package com.handoff.band

import com.handoff.band.ble.BandStatus
import com.handoff.band.ui.components.BatteryLevel
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The three blocks and the two states that are not levels, design decisions
 * §8. VSYS is after D1, so each threshold is met at VSYS = cell − 0.35 V.
 */
class BatteryLevelTest {

    private fun status(vsysMv: Int?, flags: Int = 0x03) = BandStatus(
        version = 2, flags = flags, linkState = 0, recordId = 0, lastScore = 0,
        ownBlobLen = 0, fragBitmap = 0, chunkErrors = 0, frameErrors = 0,
        vsysMv = vsysMv, firmware = "0.2.0",
    )

    @Test
    fun unknownBeforeAnyReadingAndNeverCritical() {
        assertEquals(BatteryLevel.UNKNOWN, BatteryLevel.from(null))
        assertEquals(BatteryLevel.UNKNOWN, BatteryLevel.from(status(vsysMv = null)))
    }

    @Test
    fun thresholdsOnTheCellNotOnVsys() {
        assertEquals(BatteryLevel.FULL, BatteryLevel.from(status(3660)))      // cell 4.01
        assertEquals(BatteryLevel.GOOD, BatteryLevel.from(status(3640)))      // cell 3.99
        assertEquals(BatteryLevel.GOOD, BatteryLevel.from(status(3360)))      // cell 3.71
        assertEquals(BatteryLevel.LOW, BatteryLevel.from(status(3340)))       // cell 3.69
        assertEquals(BatteryLevel.LOW, BatteryLevel.from(status(3160)))       // cell 3.51
        assertEquals(BatteryLevel.CRITICAL, BatteryLevel.from(status(3140)))  // cell 3.49
    }

    @Test
    fun usbIsAPlugNotACharge() {
        assertEquals(BatteryLevel.USB, BatteryLevel.from(status(4600)))
        assertEquals(BatteryLevel.USB, BatteryLevel.from(status(3800, flags = 0x13)))
    }
}
