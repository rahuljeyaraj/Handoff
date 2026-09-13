package com.handoff.band.ble

/**
 * The label on the underside of the band, design decisions §2a.
 *
 * Its content is the band's identity as the radio advertises it: the four
 * hex digits `ble.c` puts after "Handoff band " in the Complete Local Name
 * (review item 3 — renamed from "Handoff " so the product name is what the
 * OS shows too), taken from the last two bytes of
 * `pico_get_unique_board_id()`. Optionally the MAC address, when a
 * production label has it. Either way it is generated per board from the
 * same id the firmware uses, never typed by hand.
 *
 *     7A3C
 *     7A3C:28:CD:C1:0A:1B:2C
 *
 * Just the digits: the sticker on a band is small, and four characters at
 * the highest error-correction level still fit the smallest QR there is,
 * where a `HANDOFF:` prefix (an earlier form, still accepted) pushed it
 * up a version. The same four digits are what a person types for a label
 * that will not scan. Plain Kotlin, tested on the JVM.
 */
data class BandCode(val suffix: String, val address: String? = null) {

    /** The advertised name, which is what the pairing matches on. */
    val name: String get() = "$NAME_PREFIX $suffix"

    /** The label's content. */
    override fun toString(): String =
        if (address != null) "$suffix:$address" else suffix

    companion object {
        /** What every band advertises before its four digits (`ble.c`). */
        const val NAME_PREFIX = "Handoff band"
        /** Labels printed before the digits stood alone carried this. */
        private const val PREFIX = "HANDOFF"
        private val SUFFIX = Regex("^[0-9A-F]{4}$")
        private val MAC = Regex("^([0-9A-F]{2}:){5}[0-9A-F]{2}$")

        fun parse(text: String?): BandCode? {
            val t = text?.trim()?.uppercase() ?: return null
            if (t.isEmpty()) return null

            val body = if (t.startsWith("$PREFIX:")) t.removePrefix("$PREFIX:") else t
            val suffix = body.take(4)
            if (!SUFFIX.matches(suffix)) return null

            val rest = body.drop(4)
            return when {
                rest.isEmpty() -> BandCode(suffix)
                rest.startsWith(":") && MAC.matches(rest.drop(1)) -> BandCode(suffix, rest.drop(1))
                else -> null
            }
        }
    }
}
