package com.handoff.band.data

/**
 * Merging two rows for one person, design decisions §3.
 *
 * FILL BLANKS, NEVER OVERWRITE. The row that stays is [into]; anything it
 * lacks is taken from [from]; anything it already has is left exactly as it
 * is. That single rule is what keeps the user's own edits safe: a corrected
 * name, a note, a number they retyped are all non-blank, so nothing from the
 * band or from the other row can replace them.
 *
 * `received_at` moves to the later of the two, so the person surfaces where
 * the most recent handshake would have. The raw vCard is the one from the
 * later card — that is the most recent thing the band actually delivered.
 */
object Merge {

    /**
     * Which existing row [incoming] is the same person as — the row to merge
     * into, or null to insert it as a new contact. [candidates] is every row
     * sharing a key, newest first (`HandshakeDao.candidates`).
     *
     * A shared phone number is the same person, full stop.
     *
     * A shared email is the same person ONLY while the phone numbers do not
     * contradict each other. One side blank still merges — that is the
     * name+email card meeting the name+email+phone card next week (design
     * decisions §3). Two different non-blank numbers do not: the wearer
     * edited this person's number to something else, or a shared family or
     * office address is on two people's cards, and in both cases silently
     * folding the card in would keep the old number and DROP the arriving
     * one, since [merge] only ever fills blanks. Two rows and the
     * possible-duplicate banner let the wearer decide; one row loses a
     * number that came over the link.
     */
    fun pick(candidates: List<Handshake>, incoming: Handshake): Handshake? =
        candidates.firstOrNull { it.phoneKey != null && it.phoneKey == incoming.phoneKey }
            ?: candidates.firstOrNull {
                it.emailKey != null && it.emailKey == incoming.emailKey &&
                    (incoming.phoneKey == null || it.phoneKey == null)
            }

    fun merge(into: Handshake, from: Handshake): Handshake {
        // Phones are the one field that is not fill-blanks: a number the
        // other row has and this one does not is not a blank being filled, it
        // is a second way to reach the same person, and dropping it loses
        // something that crossed the link (14 Sep). [mergePhones] keeps both
        // lists, this row's first.
        val merged = into.copy(
            phones = mergePhones(into.phones, from.phones),
            email = into.email ?: from.email,
            org = into.org ?: from.org,
            title = into.title ?: from.title,
            note = into.note ?: from.note,
            receivedAt = maxOf(into.receivedAt, from.receivedAt),
            vcard = if (from.receivedAt > into.receivedAt) from.vcard else into.vcard,
            fieldCount = maxOf(into.fieldCount, from.fieldCount),
            promoted = into.promoted || from.promoted,
            contactUri = into.contactUri ?: from.contactUri,
        ).rekeyed()
        // A blank that got filled is a fact the phone's copy does not have;
        // and if only the other row was saved, the phone has its name, not
        // this one's.
        val filled = merged.fields() != into.fields()
        return merged.copy(
            editedSincePromote = merged.promoted &&
                (into.editedSincePromote || from.editedSincePromote || filled || !into.promoted),
        )
    }

    /**
     * The numbers of both rows, [into]'s first and in its order, with anything
     * from [from] that is genuinely new appended — matched by [Keys.phone], so
     * the same number written two ways is not added twice, and a label the
     * wearer chose is never replaced by the one that arrived.
     *
     * Capped at [Phones.MAX], which is what a card the band carries can hold.
     * What is dropped is the far end of the other row's list, never this
     * row's own.
     */
    private fun mergePhones(into: List<Phone>, from: List<Phone>): List<Phone> {
        val keys = into.mapNotNull { Keys.phone(it.number) }.toMutableSet()
        val merged = into.toMutableList()
        for (p in from) {
            val key = Keys.phone(p.number) ?: continue
            if (keys.add(key) && merged.size < Phones.MAX) merged += p
        }
        return merged
    }

    private fun Handshake.fields() =
        listOf(phones.map { it.number to it.text }, email, org, title, note)
}
