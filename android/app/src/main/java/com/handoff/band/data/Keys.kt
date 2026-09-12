package com.handoff.band.data

/**
 * Identity keys for deduplication, design decisions §3. Match on phone and
 * email, never on name: the display name is user-editable, so it cannot be
 * part of identity.
 *
 * Plain Kotlin with no Android dependency, for the same reason as `Chunk.kt`:
 * the rule has to be testable without the database it feeds.
 */
object Keys {

    /**
     * Digits only, last nine kept, so `+91 98410 23117`, `098410 23117` and
     * `9841023117` are one person. Null when there are no digits at all.
     */
    fun phone(raw: String?): String? =
        raw?.filter { it.isDigit() }?.takeLast(9)?.takeIf { it.isNotEmpty() }

    /** Lowercased and trimmed. Null unless it at least looks like an address. */
    fun email(raw: String?): String? =
        raw?.trim()?.lowercase()?.takeIf { it.contains('@') && it.length > 2 }
}
