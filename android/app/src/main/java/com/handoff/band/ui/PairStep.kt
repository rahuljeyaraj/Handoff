package com.handoff.band.ui

import com.handoff.band.ble.BandCode

/**
 * Where first-run pairing has got to, for the setup page to draw. Every
 * state is on screen (pairing-page brief §1): the page used to have one
 * line of text that was wiped the instant the OS chooser closed, so a
 * failure never said anything.
 *
 *     Scanning → Looking(code) → Connecting(name) → Connected(name)
 *     any of those → Failed(reason, code)
 *
 * The "read" moment — the label decoded, the tick felt — is the start of
 * [Looking]. [Failed] keeps the code so "Try again" repeats the same
 * attempt; the viewfinder is armed again at the same time, so scanning the
 * label afresh is a retry too. Whether the code was scanned or typed makes
 * no difference from [Looking] on.
 */
sealed interface PairStep {
    /** The viewfinder is armed and nothing has been read. */
    data object Scanning : PairStep

    /** The label read; our own scan and then the OS chooser are looking for it. */
    data class Looking(val code: BandCode) : PairStep

    /**
     * The chooser handed back a device; the service is connecting and
     * bonding. [address] is which one, so the page can tell the service's
     * state for this attempt from what a previous band left behind.
     */
    data class Connecting(val name: String, val code: BandCode, val address: String) : PairStep

    /** The link is up and encrypted — step 2. */
    data class Connected(val name: String) : PairStep

    /** What went wrong, in plain words, and what to try again with. */
    data class Failed(val reason: String, val code: BandCode) : PairStep
}
