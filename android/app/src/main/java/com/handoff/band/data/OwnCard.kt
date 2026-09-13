package com.handoff.band.data

import com.handoff.band.vcard.VCard

/**
 * The wearer's own contact card, as authored in the app (design decisions
 * §4a). Persisted locally so it can be pushed to the band again after a
 * reconnect or an erase, and so the app can say whose card it is.
 *
 * Every field is editable text and every non-blank field is sent: a field
 * you would rather not share is a field you leave empty, so there are no
 * per-field switches. A card is only worth putting on a band if it gives
 * the recipient a way to reach somebody, so [complete] demands a phone or
 * an email.
 */
data class OwnCard(
    val name: String = "",
    val mobile: String = "",
    val work: String = "",
    val email: String = "",
    val org: String = "",
    val title: String = "",
) {
    /** Name plus at least one way to be reached. */
    val complete: Boolean
        get() = name.isNotBlank() && (mobile.isNotBlank() || work.isNotBlank() || email.isNotBlank())

    /** The exact bytes that go to `my_vcard`. */
    fun vcard(): String = VCard.build(
        fullName = name.trim(),
        structuredName = name.trim().split(' ').takeIf { it.size >= 2 }
            ?.let { "${it.last()};${it.dropLast(1).joinToString(" ")};;;" },
        mobile = mobile,
        work = work,
        email = email,
        org = org,
        title = title,
    )

    /**
     * Identifies this card's bytes. The band reports whether it holds a card
     * but never which, so the service remembers the hash of the last one it
     * wrote, and the card page compares against the same value.
     */
    val hash: String get() = vcard().hashCode().toString(16)

    /** A flat form for storage and for surviving rotation: six strings. */
    fun toList(): List<String> = listOf(name, mobile, work, email, org, title)

    companion object {
        fun fromList(l: List<Any?>): OwnCard? = runCatching {
            OwnCard(
                name = l[0] as String, mobile = l[1] as String, work = l[2] as String,
                email = l[3] as String, org = l[4] as String, title = l[5] as String,
            )
        }.getOrNull()
    }
}
