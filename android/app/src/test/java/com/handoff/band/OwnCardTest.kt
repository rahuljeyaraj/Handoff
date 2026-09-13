package com.handoff.band

import com.handoff.band.data.OwnCard
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The wearer's own card: what counts as complete, and what gets written. */
class OwnCardTest {

    @Test
    fun nameOnlyIsNotComplete() {
        assertFalse(OwnCard(name = "Rohan Iyer").complete)
        assertFalse(OwnCard(name = "Rohan Iyer", mobile = "+91 98860 41225", sendMobile = false).complete)
        assertFalse(OwnCard(name = "", mobile = "+91 98860 41225").complete)
    }

    @Test
    fun oneContactMethodIsEnough() {
        assertTrue(OwnCard(name = "Rohan Iyer", mobile = "+91 98860 41225").complete)
        assertTrue(OwnCard(name = "Rohan Iyer", email = "rohan@handoff.dev", sendMobile = false).complete)
    }

    @Test
    fun togglesDropFieldsFromTheCard() {
        val v = OwnCard(name = "Rohan Iyer", mobile = "+919886041225", work = "+914412345678",
                        email = "rohan@handoff.dev", org = "Handoff", title = "Hardware lead",
                        sendWork = false, sendTitle = false).vcard()
        assertTrue(v.contains("FN:Rohan Iyer\r\n"))
        assertTrue(v.contains("N:Iyer;Rohan;;;\r\n"))
        assertTrue(v.contains("TEL;TYPE=CELL:+919886041225\r\n"))
        assertFalse(v.contains("TYPE=WORK"))
        assertTrue(v.contains("EMAIL;TYPE=INTERNET:rohan@handoff.dev\r\n"))
        assertTrue(v.contains("ORG:Handoff\r\n"))
        assertFalse(v.contains("TITLE"))
    }

    @Test
    fun listRoundTrips() {
        val c = OwnCard(name = "A B", mobile = "1", work = "2", email = "a@b", org = "o", title = "t",
                        sendMobile = true, sendWork = false, sendEmail = true, sendOrg = false,
                        sendTitle = true)
        assertEquals(c, OwnCard.fromList(c.toList()))
    }
}
