package com.handoff.band

import com.handoff.band.data.Handshake
import com.handoff.band.data.Keys
import com.handoff.band.data.Merge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The dedup identity and the merge rule, design decisions §3. Plain JVM: no
 * database, no Android.
 */
class DedupTest {

    @Test
    fun phoneKeyIgnoresFormattingAndCountryCode() {
        val k = Keys.phone("+91 98410 23117")
        assertEquals("841023117", k)
        assertEquals(k, Keys.phone("098410 23117"))
        assertEquals(k, Keys.phone("9841023117"))
        assertEquals(k, Keys.phone("(0)98410-23117"))
    }

    @Test
    fun phoneKeyIsNullWithoutDigits() {
        assertNull(Keys.phone(null))
        assertNull(Keys.phone(""))
        assertNull(Keys.phone("ext"))
    }

    @Test
    fun emailKeyIsLowercasedAndTrimmed() {
        assertEquals("priya.r@vit.ac.in", Keys.email("  Priya.R@VIT.ac.in "))
        assertNull(Keys.email("not an address"))
        assertNull(Keys.email(null))
    }

    @Test
    fun entityComputesKeysByDefault() {
        val h = Handshake(receivedAt = 1, vcard = "", displayName = "P",
                          mobile = "+91 98410 23117", email = "P@X.io")
        assertEquals("841023117", h.phoneKey)
        assertEquals("p@x.io", h.emailKey)
    }

    @Test
    fun mergeFillsBlanksAndKeepsEdits() {
        val kept = Handshake(id = 1, receivedAt = 100, vcard = "old", displayName = "Dr Priya Raghavan",
                             email = "priya@vit.ac.in", note = "Met at PCBWay")
        val incoming = Handshake(id = 2, receivedAt = 200, vcard = "new", displayName = "Priya Raghavan",
                                 mobile = "+91 98410 23117", email = "priya@vit.ac.in",
                                 org = "Vellore Institute", note = "should not win")

        val m = Merge.merge(into = kept, from = incoming)

        assertEquals(1L, m.id)
        assertEquals("Dr Priya Raghavan", m.displayName)      // the user's correction stays
        assertEquals("Met at PCBWay", m.note)                 // the user's note stays
        assertEquals("+91 98410 23117", m.mobile)             // blank filled
        assertEquals("Vellore Institute", m.org)              // blank filled
        assertEquals(200L, m.receivedAt)                      // surfaces as the newer handshake
        assertEquals("new", m.vcard)                          // the most recent raw card
        assertEquals("841023117", m.phoneKey)                 // rekeyed
    }

    @Test
    fun mergeKeepsTheOlderCardWhenItIsNewer() {
        val kept = Handshake(id = 1, receivedAt = 300, vcard = "kept", displayName = "A")
        val from = Handshake(id = 2, receivedAt = 200, vcard = "from", displayName = "A")
        assertEquals("kept", Merge.merge(kept, from).vcard)
        assertEquals(300L, Merge.merge(kept, from).receivedAt)
    }
}
