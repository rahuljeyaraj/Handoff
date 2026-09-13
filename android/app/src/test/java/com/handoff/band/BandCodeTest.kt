package com.handoff.band

import com.handoff.band.ble.BandCode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** What the label says and what a person might type instead. */
class BandCodeTest {

    @Test
    fun labelWithAndWithoutAddress() {
        assertEquals(BandCode("7A3C"), BandCode.parse("HANDOFF:7A3C"))
        assertEquals(BandCode("7A3C", "28:CD:C1:0A:1B:2C"),
                     BandCode.parse("HANDOFF:7A3C:28:CD:C1:0A:1B:2C"))
        assertEquals("Handoff 7A3C", BandCode.parse("HANDOFF:7A3C")!!.name)
        assertEquals("HANDOFF:7A3C", BandCode("7A3C").toString())
    }

    @Test
    fun handTypedCodeIsForgiving() {
        assertEquals(BandCode("7A3C"), BandCode.parse("7a3c"))
        assertEquals(BandCode("7A3C"), BandCode.parse("  7A3C \n"))
        assertEquals(BandCode("7A3C", "28:CD:C1:0A:1B:2C"),
                     BandCode.parse("handoff:7a3c:28:cd:c1:0a:1b:2c"))
    }

    @Test
    fun rejectsAnythingElse() {
        assertNull(BandCode.parse(null))
        assertNull(BandCode.parse(""))
        assertNull(BandCode.parse("7A3"))
        assertNull(BandCode.parse("7A3CZ"))
        assertNull(BandCode.parse("HANDOFF:7A3C:notamac"))
        assertNull(BandCode.parse("https://example.com"))
    }
}
