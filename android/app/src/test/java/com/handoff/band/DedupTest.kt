package com.handoff.band

import com.handoff.band.data.Handshake
import com.handoff.band.data.Keys
import com.handoff.band.data.Merge
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import com.handoff.band.data.Phones
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
                          phones = listOf(Phone("+91 98410 23117")), email = "P@X.io")
        assertEquals("841023117", h.phoneKey)
        assertEquals("p@x.io", h.emailKey)
    }

    @Test
    fun aSharedPhoneNumberIsAlwaysTheSamePerson() {
        val existing = Handshake(id = 1, receivedAt = 100, vcard = "", displayName = "Priya",
                                 phones = listOf(Phone("+91 98410 23117")), email = "old@vit.ac.in")
        val incoming = Handshake(id = 0, receivedAt = 200, vcard = "", displayName = "Priya",
                                 phones = listOf(Phone("098410 23117")), email = "new@vit.ac.in")
        assertEquals(existing, Merge.pick(listOf(existing), incoming))
    }

    @Test
    fun aSharedEmailMergesWhileEitherNumberIsBlank() {
        val noNumber = Handshake(id = 1, receivedAt = 100, vcard = "", displayName = "Priya",
                                 email = "priya@vit.ac.in")
        val withNumber = Handshake(id = 0, receivedAt = 200, vcard = "", displayName = "Priya",
                                   phones = listOf(Phone("+91 98410 23117")), email = "priya@vit.ac.in")
        // The card that fills in the blank, and the card that arrives without one.
        assertEquals(noNumber, Merge.pick(listOf(noNumber), withNumber))
        assertEquals(withNumber, Merge.pick(listOf(withNumber), noNumber))
    }

    /** The bug: an edited number, then the original card again (14 Sep). */
    @Test
    fun aSharedEmailWithAContradictingNumberIsANewContact() {
        val edited = Handshake(id = 1, receivedAt = 100, vcard = "", displayName = "Björn Smári",
                               phones = listOf(Phone("+354 555 1543")), email = "bjorn@gmail.com")
        val incoming = Handshake(id = 0, receivedAt = 200, vcard = "", displayName = "Björn Smári",
                                 phones = listOf(Phone("+354 555 1234")), email = "bjorn@gmail.com")
        assertNull(Merge.pick(listOf(edited), incoming))
    }

    @Test
    fun aPhoneMatchWinsOverAnEmailMatchOnAnotherRow() {
        val byEmail = Handshake(id = 1, receivedAt = 300, vcard = "", displayName = "Shared inbox",
                                phones = listOf(Phone("+354 555 1543")), email = "desk@aether.is")
        val byPhone = Handshake(id = 2, receivedAt = 100, vcard = "", displayName = "Björn",
                                phones = listOf(Phone("+354 555 1234")), email = "bjorn@gmail.com")
        val incoming = Handshake(id = 0, receivedAt = 400, vcard = "", displayName = "Björn",
                                 phones = listOf(Phone("+354 555 1234")), email = "desk@aether.is")
        // Newest first, as the query returns them: the email candidate is
        // first and contradicts, the phone candidate is the answer.
        assertEquals(byPhone, Merge.pick(listOf(byEmail, byPhone), incoming))
    }

    @Test
    fun nothingSharedIsANewContact() {
        val other = Handshake(id = 1, receivedAt = 100, vcard = "", displayName = "Someone",
                              phones = listOf(Phone("+354 555 9999")), email = "someone@else.is")
        assertNull(Merge.pick(emptyList(), other))
    }

    @Test
    fun mergeFillsBlanksAndKeepsEdits() {
        val kept = Handshake(id = 1, receivedAt = 100, vcard = "old", displayName = "Dr Priya Raghavan",
                             email = "priya@vit.ac.in", note = "Met at PCBWay")
        val incoming = Handshake(id = 2, receivedAt = 200, vcard = "new", displayName = "Priya Raghavan",
                                 phones = listOf(Phone("+91 98410 23117")), email = "priya@vit.ac.in",
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

    /** Merging the two rows [Merge.pick] kept apart must not lose a number. */
    @Test
    fun mergeKeepsBothNumbers() {
        val kept = Handshake(id = 1, receivedAt = 200, vcard = "", displayName = "Björn Smári",
                             phones = listOf(Phone("+354 555 1234")), email = "bjorn@gmail.com")
        val other = Handshake(id = 2, receivedAt = 100, vcard = "", displayName = "Björn Smári",
                              phones = listOf(Phone("+354 555 1543")), email = "bjorn@gmail.com")

        val m = Merge.merge(into = kept, from = other)

        assertEquals(listOf("+354 555 1234", "+354 555 1543"), m.phones.map { it.number })
        assertEquals("545551234", m.phoneKey)      // identity is still the first
    }

    /** Every number of both rows, this row's first, capped at what a card holds. */
    @Test
    fun mergeKeepsThisRowsNumbersFirstAndCapsTheRest() {
        val kept = Handshake(id = 1, receivedAt = 200, vcard = "", displayName = "Björn",
                             phones = listOf(Phone("+354 555 1234"), Phone("+354 555 8000", PhoneLabel.WORK)),
                             email = "bjorn@gmail.com")
        val other = Handshake(id = 2, receivedAt = 100, vcard = "", displayName = "Björn",
                              phones = listOf(Phone("+354 555 1543", PhoneLabel.HOME),
                                              Phone("+354 555 2020", PhoneLabel.MAIN)),
                              email = "bjorn@gmail.com")

        val m = Merge.merge(into = kept, from = other)

        assertEquals(Phones.MAX, m.phones.size)
        assertEquals(listOf("+354 555 1234", "+354 555 8000", "+354 555 1543"),
                     m.phones.map { it.number })
        // The label each number arrived with travels with it.
        assertEquals(PhoneLabel.HOME, m.phones[2].label)
    }

    /** A label the wearer chose is never replaced by the one that arrived. */
    @Test
    fun mergeKeepsThisRowsLabelForANumberBothRowsHave() {
        val kept = Handshake(id = 1, receivedAt = 200, vcard = "", displayName = "Björn",
                             phones = listOf(Phone("+354 555 1234", PhoneLabel.CUSTOM, "Reception")),
                             email = "bjorn@gmail.com")
        val other = Handshake(id = 2, receivedAt = 100, vcard = "", displayName = "Björn",
                              phones = listOf(Phone("+354 555 1234", PhoneLabel.WORK)),
                              email = "bjorn@gmail.com")

        val m = Merge.merge(into = kept, from = other)

        assertEquals(1, m.phones.size)
        assertEquals("Reception", m.phones[0].text)
    }

    @Test
    fun aNumberThatIsAlreadyOnTheRowIsNotAddedTwice() {
        val kept = Handshake(id = 1, receivedAt = 200, vcard = "", displayName = "Björn",
                             phones = listOf(Phone("+354 555 1234")), email = "bjorn@gmail.com")
        // The same number, written differently — not a second one.
        val other = Handshake(id = 2, receivedAt = 100, vcard = "", displayName = "Björn",
                              phones = listOf(Phone("00354 555 1234")), email = "bjorn@gmail.com")
        assertEquals(1, Merge.merge(into = kept, from = other).phones.size)
    }

    @Test
    fun mergeKeepsTheOlderCardWhenItIsNewer() {
        val kept = Handshake(id = 1, receivedAt = 300, vcard = "kept", displayName = "A")
        val from = Handshake(id = 2, receivedAt = 200, vcard = "from", displayName = "A")
        assertEquals("kept", Merge.merge(kept, from).vcard)
        assertEquals(300L, Merge.merge(kept, from).receivedAt)
    }
}
