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

    fun merge(into: Handshake, from: Handshake): Handshake {
        val merged = into.copy(
            mobile = into.mobile ?: from.mobile,
            work = into.work ?: from.work,
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

    private fun Handshake.fields() = listOf(mobile, work, email, org, title, note)
}
