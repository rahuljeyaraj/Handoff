package com.handoff.band

import com.handoff.band.data.OwnCard
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The wearer's own card: what counts as complete, and what gets written. */
class OwnCardTest {

    @Test
    fun nameOnlyIsNotComplete() {
        assertFalse(OwnCard(name = "Rohan Iyer").complete)
        assertFalse(OwnCard(name = "Rohan Iyer", org = "Handoff", title = "Hardware lead").complete)
        assertFalse(OwnCard(name = "", phones = listOf(Phone("+91 98860 41225"))).complete)
    }

    @Test
    fun oneWayToBeReachedIsEnough() {
        assertTrue(OwnCard(name = "Rohan Iyer", phones = listOf(Phone("+91 98860 41225"))).complete)
        assertTrue(OwnCard(name = "Rohan Iyer", phones = listOf(Phone("+91 80 4718 2200", PhoneLabel.WORK))).complete)
        assertTrue(OwnCard(name = "Rohan Iyer", email = "rohan@handoff.dev").complete)
    }

    @Test
    fun emptyFieldsAreNotWritten() {
        val v = OwnCard(name = "Rohan Iyer", phones = listOf(Phone("+919886041225")),
                        email = "rohan@handoff.dev", org = "Handoff").vcard()
        assertTrue(v.contains("FN:Rohan Iyer\r\n"))
        assertTrue(v.contains("N:Iyer;Rohan;;;\r\n"))
        assertTrue(v.contains("TEL;TYPE=CELL:+919886041225\r\n"))
        assertFalse(v.contains("TYPE=WORK"))
        assertTrue(v.contains("EMAIL;TYPE=INTERNET:rohan@handoff.dev\r\n"))
        assertTrue(v.contains("ORG:Handoff\r\n"))
        assertFalse(v.contains("TITLE"))
    }

    @Test
    fun hashFollowsTheBytes() {
        val a = OwnCard(name = "Rohan Iyer", phones = listOf(Phone("+919886041225")))
        assertEquals(a.hash, a.copy().hash)
        assertNotEquals(a.hash, a.copy(phones = listOf(Phone("+919886041226"))).hash)
    }

    @Test
    fun listRoundTrips() {
        val c = OwnCard(name = "A B", phones = listOf(Phone("1", PhoneLabel.MOBILE), Phone("2", PhoneLabel.WORK)), email = "a@b", org = "o", title = "t")
        assertEquals(c, OwnCard.fromList(c.toList()))
    }
}
