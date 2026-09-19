package com.handoff.band

import com.handoff.band.contacts.Promote
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The name handed to the phone's contact editor as given + family: joined
 * with one space it must be the name exactly as the wearer wrote it.
 */
class NamePartsTest {

    @Test
    fun aRenameInBracketsStaysWithTheFamilyName() {
        assertEquals("Vikram" to "Sharma (Vivado License)",
                     Promote.nameParts("Vikram Sharma (Vivado License)"))
    }

    @Test
    fun plainNamesSplitAtTheLastWord() {
        assertEquals("Savithri" to "Raghavan", Promote.nameParts("Savithri Raghavan"))
        assertEquals("Anne Marie" to "Smith", Promote.nameParts("Anne Marie Smith"))
    }

    @Test
    fun oneWordIsAGivenNameAlone() {
        assertEquals("Björn" to "", Promote.nameParts("  Björn "))
        assertEquals("(front desk)" to "", Promote.nameParts("(front desk)"))
    }

    @Test
    fun partsRejoinToTheNameAsWritten() {
        for (n in listOf("Vikram Sharma (Vivado License)", "Savithri Raghavan (front desk)",
                         "Anne Marie Smith", "Björn")) {
            val (g, f) = Promote.nameParts(n)
            assertEquals(n, listOf(g, f).filter { it.isNotEmpty() }.joinToString(" "))
        }
    }
}
