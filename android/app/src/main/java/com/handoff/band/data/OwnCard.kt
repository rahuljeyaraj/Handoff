package com.handoff.band.data

import com.handoff.band.vcard.VCard

/**
 * The wearer's own contact card, as authored in the app (design decisions
 * §4a). Persisted locally so it can be pushed to the band again after a
 * reconnect or an erase, and so the app can say whose card it is.
 *
 * Every field is editable text; the `send*` flags are the per-field toggles.
 * The name is always sent. A card is only worth putting on a band if it
 * gives the recipient a way to reach somebody, so [complete] demands at
 * least one of mobile or email switched on and non-blank.
 */
data class OwnCard(
    val name: String = "",
    val mobile: String = "",
    val work: String = "",
    val email: String = "",
    val org: String = "",
    val title: String = "",
    val sendMobile: Boolean = true,
    val sendWork: Boolean = true,
    val sendEmail: Boolean = true,
    val sendOrg: Boolean = true,
    val sendTitle: Boolean = true,
) {
    val mobileShared: Boolean get() = sendMobile && mobile.isNotBlank()
    val emailShared: Boolean get() = sendEmail && email.isNotBlank()

    /** Name plus at least one way to be reached. */
    val complete: Boolean get() = name.isNotBlank() && (mobileShared || emailShared)

    /** The exact bytes that go to `my_vcard`. */
    fun vcard(): String = VCard.build(
        fullName = name.trim(),
        structuredName = name.trim().split(' ').takeIf { it.size >= 2 }
            ?.let { "${it.last()};${it.dropLast(1).joinToString(" ")};;;" },
        mobile = mobile.takeIf { sendMobile },
        work = work.takeIf { sendWork },
        email = email.takeIf { sendEmail },
        org = org.takeIf { sendOrg },
        title = title.takeIf { sendTitle },
    )

    /** A flat form for storage and for surviving rotation: six strings, five flags. */
    fun toList(): List<Any> = listOf(name, mobile, work, email, org, title,
                                     sendMobile, sendWork, sendEmail, sendOrg, sendTitle)

    companion object {
        fun fromList(l: List<Any?>): OwnCard? = runCatching {
            OwnCard(
                name = l[0] as String, mobile = l[1] as String, work = l[2] as String,
                email = l[3] as String, org = l[4] as String, title = l[5] as String,
                sendMobile = l[6] as Boolean, sendWork = l[7] as Boolean,
                sendEmail = l[8] as Boolean, sendOrg = l[9] as Boolean, sendTitle = l[10] as Boolean,
            )
        }.getOrNull()
    }
}
