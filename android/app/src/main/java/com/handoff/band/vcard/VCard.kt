package com.handoff.band.vcard

/**
 * vCard 3.0, only as much of it as this app trades in.
 *
 * Both directions are needed and they are not symmetrical:
 *
 *   OUT  the wearer's own card, built from a contact they picked, written to
 *        `my_vcard`. The band re-encodes it into compact TLV once, at
 *        provisioning, and never stores the text (architecture §9).
 *
 *   IN   a received card, arriving on `rx_vcard` already reconstructed by
 *        `firmware/lib/record/vcard.c` — BEGIN, VERSION and END synthesised,
 *        N derived from FN when N did not survive the body link. The app
 *        parses it for display and for the Contacts insert, and keeps the raw
 *        text as well, because a partial card is worth showing verbatim.
 *
 * `tools/vcf.py` in the firmware repository is the independent reference for
 * the same codec. If this file and vcf.py agree, this file agrees with the
 * wristband — which is why the round-trip check lives against vcf.py rather
 * than against the C.
 */
data class VLine(val property: String, val params: List<String>, val value: String) {
    fun hasParam(name: String) =
        params.any { it.equals(name, true) || it.substringAfter('=', "").equals(name, true) }
}

class VCard(val lines: List<VLine>, val raw: String) {

    fun first(property: String): String? =
        lines.firstOrNull { it.property.equals(property, true) }?.value

    val fn: String? get() = first("FN")
    val org: String? get() = first("ORG")?.replace(";", " ")?.trim()
    val title: String? get() = first("TITLE")
    val note: String? get() = first("NOTE")
    val url: String? get() = first("URL")
    val email: String? get() = first("EMAIL")

    val mobile: String?
        get() = lines.firstOrNull { it.property.equals("TEL", true) && it.hasParam("CELL") }?.value
            ?: lines.firstOrNull { it.property.equals("TEL", true) }?.value

    val work: String?
        get() = lines.firstOrNull { it.property.equals("TEL", true) && it.hasParam("WORK") }?.value

    /** Family;Given;… as vCard stores it, or null when only FN survived. */
    val structuredName: String? get() = first("N")

    /** A rough "how much of the card made it", for the completeness badge. */
    val fieldCount: Int get() = lines.count { it.property !in SKELETON }

    /**
     * The display name, in the order the wearer would expect to see it. FN is
     * what the priority carousel sends first (architecture §8.4), so on a
     * short handshake it is often the only thing there is.
     */
    val displayName: String
        get() = fn ?: structuredName?.split(';')
            ?.filter { it.isNotBlank() }
            ?.reversed()
            ?.joinToString(" ")
            ?: "(unnamed)"

    companion object {
        private val SKELETON = setOf("BEGIN", "END", "VERSION")

        /**
         * Accepts CRLF or LF, and RFC 2425 line folding — a continuation line
         * begins with a space or a tab and belongs to the line before it.
         */
        fun parse(text: String): VCard {
            val unfolded = buildList<String> {
                for (line in text.replace("\r\n", "\n").split('\n')) {
                    if (line.isEmpty()) continue
                    if ((line[0] == ' ' || line[0] == '\t') && isNotEmpty()) {
                        set(size - 1, last() + line.substring(1))
                    } else {
                        add(line)
                    }
                }
            }

            val lines = unfolded.mapNotNull { line ->
                val colon = line.indexOf(':')
                if (colon <= 0) return@mapNotNull null
                val head = line.substring(0, colon).split(';')
                VLine(head[0].trim(), head.drop(1), line.substring(colon + 1))
            }.filter { it.property.uppercase() !in SKELETON }

            return VCard(lines, text)
        }

        /**
         * Build a card for provisioning. PHOTO is not a parameter here and
         * never will be: `compact.c` rejects it at encode time with an
         * explicit error rather than truncating it (architecture §8.2), so
         * sending one would be a provisioning failure rather than a big card.
         */
        fun build(
            fullName: String,
            structuredName: String? = null,
            mobile: String? = null,
            work: String? = null,
            email: String? = null,
            org: String? = null,
            title: String? = null,
            url: String? = null,
            note: String? = null,
        ): String = buildString {
            fun line(s: String) = append(s).append("\r\n")

            line("BEGIN:VCARD")
            line("VERSION:3.0")
            structuredName?.takeIf { it.isNotBlank() }?.let { line("N:$it") }
            line("FN:$fullName")
            org?.takeIf { it.isNotBlank() }?.let { line("ORG:$it") }
            title?.takeIf { it.isNotBlank() }?.let { line("TITLE:$it") }
            mobile?.takeIf { it.isNotBlank() }?.let { line("TEL;TYPE=CELL:$it") }
            work?.takeIf { it.isNotBlank() }?.let { line("TEL;TYPE=WORK:$it") }
            email?.takeIf { it.isNotBlank() }?.let { line("EMAIL;TYPE=INTERNET:$it") }
            url?.takeIf { it.isNotBlank() }?.let { line("URL:$it") }
            note?.takeIf { it.isNotBlank() }?.let { line("NOTE:$it") }
            line("END:VCARD")
        }
    }
}
