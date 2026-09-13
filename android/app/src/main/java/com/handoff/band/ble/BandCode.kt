package com.handoff.band.ble

/**
 * The label on the underside of the band, design decisions §2a.
 *
 * Its content is the band's identity as the radio advertises it: the four
 * hex digits `ble.c` puts after "Handoff " in the Complete Local Name, taken
 * from the last two bytes of `pico_get_unique_board_id()`. Optionally the
 * MAC address, when a production label has it. Either way it is generated
 * per board from the same id the firmware uses, never typed by hand.
 *
 *     HANDOFF:7A3C
 *     HANDOFF:7A3C:28:CD:C1:0A:1B:2C
 *
 * The manual fallback accepts the bare four digits, for a label that has
 * worn off. Plain Kotlin, tested on the JVM.
 */
data class BandCode(val suffix: String, val address: String? = null) {

    /** The advertised name, which is what the scan filter matches on. */
    val name: String get() = "Handoff $suffix"

    override fun toString(): String =
        if (address != null) "$PREFIX:$suffix:$address" else "$PREFIX:$suffix"

    companion object {
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
