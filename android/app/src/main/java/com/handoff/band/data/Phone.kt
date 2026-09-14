package com.handoff.band.data

/**
 * A phone number and what it is for, design decisions §4a.
 *
 * The band carries the label alongside the number (`TAG_TEL` in `compact.h`),
 * so a card can hold two mobiles, or a number the wearer labelled themselves,
 * and the far end sees what was meant rather than a guess. Before this there
 * was one mobile slot and one work slot, and the label was the slot.
 *
 * Plain Kotlin with no Android dependency, for the same reason as [Keys]: the
 * rule has to be testable without the database and the contacts provider it
 * feeds.
 */
enum class PhoneLabel {
    /** The four the editor offers by name. */
    MOBILE, WORK, HOME, MAIN,

    /** The wearer typed their own word for it; it is in [Phone.custom]. */
    CUSTOM,

    /**
     * A number that arrived wearing no label at all — a bare `TEL:` line. Not
     * the same as MOBILE: calling it a mobile prints a word under somebody's
     * number that nobody ever said.
     */
    NONE,
}

data class Phone(
    val number: String,
    val label: PhoneLabel = PhoneLabel.MOBILE,
    /** Only meaningful when [label] is [PhoneLabel.CUSTOM]. */
    val custom: String = "",
) {
    /** What goes under the number on screen. */
    val text: String
        get() = when (label) {
            PhoneLabel.MOBILE -> "Mobile"
            PhoneLabel.WORK -> "Work"
            PhoneLabel.HOME -> "Home"
            PhoneLabel.MAIN -> "Main"
            PhoneLabel.CUSTOM -> custom.trim().ifEmpty { "Phone" }
            PhoneLabel.NONE -> "Phone"
        }

    /**
     * The vCard TYPE parameter, or null for a bare TEL. A custom label goes
     * out as an X- type, which is how Android writes one and how
     * `firmware/lib/record/vcard.c` reads it back.
     */
    val vcardType: String?
        get() = when (label) {
            PhoneLabel.MOBILE -> "CELL"
            PhoneLabel.WORK -> "WORK"
            PhoneLabel.HOME -> "HOME"
            PhoneLabel.MAIN -> "MAIN"
            PhoneLabel.CUSTOM -> custom.trim().takeIf { it.isNotEmpty() }?.let { "X-$it" }
            PhoneLabel.NONE -> null
        }

    val blank: Boolean get() = number.isBlank()
}

/**
 * The list of phones as one database column and as one stored string.
 *
 * A child table would be the textbook shape, but nothing ever queries a single
 * phone: the dedup identity is `phone_key`/`email_key` on the row itself
 * (design decisions §3), and every screen wants the whole list at once. A
 * column keeps `Flow<List<Handshake>>` — and therefore every screen — exactly
 * as it was, and keeps this rule testable as plain Kotlin.
 *
 * The separators are the ASCII unit and record separators, which no phone
 * keyboard can produce and no phone number contains.
 */
object Phones {

    /** What one card may carry (design decisions §4a). */
    const val MAX = 3

    private const val FIELD = '\u001F'
    private const val RECORD = '\u001E'

    fun encode(phones: List<Phone>): String? = phones
        .filterNot { it.blank }
        .take(MAX)
        .joinToString(RECORD.toString()) {
            listOf(it.label.name, it.custom.replace(FIELD, ' ').replace(RECORD, ' '),
                   it.number).joinToString(FIELD.toString())
        }
        .takeIf { it.isNotEmpty() }

    fun decode(stored: String?): List<Phone> =
        stored?.takeIf { it.isNotEmpty() }
            ?.split(RECORD)
            ?.mapNotNull { record ->
                val parts = record.split(FIELD)
                if (parts.size != 3 || parts[2].isBlank()) return@mapNotNull null
                val label = runCatching { PhoneLabel.valueOf(parts[0]) }
                    .getOrDefault(PhoneLabel.NONE)
                Phone(number = parts[2], label = label, custom = parts[1])
            }
            .orEmpty()
}
