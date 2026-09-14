package com.handoff.band

import com.handoff.band.data.OwnCard
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import com.handoff.band.data.Phones
import com.handoff.band.vcard.VCard
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Phone labels end to end on the app side, design decisions §4a: the column
 * encoding, the vCard the band is given, and the vCard the band delivers.
 *
 * The wire format itself is the firmware's suite (`scripts/test.py`), which
 * cross-checks the C against `tools/vcf.py` on a card carrying every label.
 */
class PhoneTest {

    // ---- the stored column ----

    @Test
    fun everyLabelSurvivesTheColumn() {
        val phones = listOf(
            Phone("+354 555 1234", PhoneLabel.MOBILE),
            Phone("+354 555 8000", PhoneLabel.WORK),
            Phone("+354 555 1543", PhoneLabel.HOME),
        )
        assertEquals(phones, Phones.decode(Phones.encode(phones)))
    }

    @Test
    fun aCustomLabelSurvivesTheColumn() {
        val phones = listOf(Phone("+354 555 9000", PhoneLabel.CUSTOM, "Reception"))
        assertEquals(phones, Phones.decode(Phones.encode(phones)))
    }

    @Test
    fun theSameLabelTwiceIsTwoPhones() {
        val phones = listOf(
            Phone("+354 555 1234", PhoneLabel.MOBILE),
            Phone("+354 555 7777", PhoneLabel.MOBILE),
        )
        assertEquals(phones, Phones.decode(Phones.encode(phones)))
    }

    @Test
    fun blanksAreNotStoredAndNothingIsStoredAsNull() {
        assertNull(Phones.encode(emptyList()))
        assertNull(Phones.encode(listOf(Phone("  ", PhoneLabel.MOBILE))))
        assertEquals(emptyList<Phone>(), Phones.decode(null))
        assertEquals(emptyList<Phone>(), Phones.decode(""))
    }

    @Test
    fun noMoreThanACardCanCarry() {
        val four = (1..4).map { Phone("+354 555 000$it", PhoneLabel.MOBILE) }
        assertEquals(Phones.MAX, Phones.decode(Phones.encode(four)).size)
    }

    /** A label with a separator typed into it must not become two fields. */
    @Test
    fun aLabelCannotBreakTheEncoding() {
        val odd = Phone("+354 555 1234", PhoneLabel.CUSTOM, "Desk\u001Fone\u001Etwo")
        val back = Phones.decode(Phones.encode(listOf(odd)))
        assertEquals(1, back.size)
        assertEquals("+354 555 1234", back[0].number)
        assertEquals("Desk one two", back[0].custom)
    }

    @Test
    fun anUnreadableLabelDoesNotLoseTheNumber() {
        // A row written by a later version, read by this one.
        val back = Phones.decode("SATELLITE\u001F\u001F+354 555 1234")
        assertEquals(1, back.size)
        assertEquals("+354 555 1234", back[0].number)
        assertEquals(PhoneLabel.NONE, back[0].label)
    }

    // ---- the card that goes to the band ----

    @Test
    fun theCardCarriesEveryLabel() {
        val v = OwnCard(
            name = "Björn Smári",
            phones = listOf(
                Phone("+354 555 1234", PhoneLabel.MOBILE),
                Phone("+354 555 1543", PhoneLabel.HOME),
                Phone("+354 555 9000", PhoneLabel.CUSTOM, "Reception"),
            ),
        ).vcard()

        assertTrue(v.contains("TEL;TYPE=CELL:+354 555 1234\r\n"))
        assertTrue(v.contains("TEL;TYPE=HOME:+354 555 1543\r\n"))
        assertTrue(v.contains("TEL;TYPE=X-Reception:+354 555 9000\r\n"))
    }

    @Test
    fun aCustomLabelWithNothingInItIsNotALabel() {
        val v = OwnCard(name = "Bo", phones = listOf(Phone("+354 555 1234", PhoneLabel.CUSTOM)))
            .vcard()
        assertTrue(v.contains("TEL:+354 555 1234\r\n"))
    }

    // ---- the card the band delivers ----

    @Test
    fun everyLabelIsReadBackOffTheWire() {
        val card = VCard.parse(
            "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Björn\r\n" +
                "TEL;TYPE=CELL:+3545551234\r\n" +
                "TEL;TYPE=WORK:+3545558000\r\n" +
                "TEL;TYPE=HOME:+3545551543\r\n" +
                "TEL;TYPE=MAIN:+3545552020\r\n" +
                "TEL;TYPE=X-Reception:+3545559000\r\n" +
                "TEL:+3545556000\r\n" +
                "END:VCARD\r\n"
        )

        assertEquals(
            listOf(PhoneLabel.MOBILE, PhoneLabel.WORK, PhoneLabel.HOME, PhoneLabel.MAIN,
                   PhoneLabel.CUSTOM, PhoneLabel.NONE),
            card.phones.map { it.label },
        )
        assertEquals("Reception", card.phones[4].custom)
        assertEquals("+3545556000", card.phones[5].number)
    }

    /** The order the sender wrote them in is the order they are worth in. */
    @Test
    fun theFirstNumberOnTheCardStaysFirst() {
        val card = VCard.parse(
            "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Björn\r\n" +
                "TEL;TYPE=WORK:+3545558000\r\n" +
                "TEL;TYPE=CELL:+3545551234\r\n" +
                "END:VCARD\r\n"
        )
        assertEquals("+3545558000", card.phones.first().number)
    }

    @Test
    fun aBareTelIsNotCalledAMobile() {
        val card = VCard.parse(
            "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Bo\r\nTEL:+3545551234\r\nEND:VCARD\r\n"
        )
        assertEquals(PhoneLabel.NONE, card.phones.single().label)
        assertEquals("Phone", card.phones.single().text)
    }

    /** What the wearer sees under the number. */
    @Test
    fun theLabelReadsAsAWord() {
        assertEquals("Mobile", Phone("x", PhoneLabel.MOBILE).text)
        assertEquals("Main", Phone("x", PhoneLabel.MAIN).text)
        assertEquals("Reception", Phone("x", PhoneLabel.CUSTOM, "Reception").text)
        assertEquals("Phone", Phone("x", PhoneLabel.CUSTOM, "   ").text)
    }
}
