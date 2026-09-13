package com.handoff.band.contacts

import android.content.Context
import android.telephony.PhoneNumberUtils
import android.telephony.TelephonyManager
import java.util.Locale

/**
 * Uniform "+91 98860 41225" grouping for any number this app shows or saves
 * (review item 5), rather than whatever spacing a phone contact happened to
 * carry. `PhoneNumberUtils.formatNumber` does the actual work with the
 * platform's own libphonenumber; this only picks the default region.
 */
object PhoneFormat {

    fun format(context: Context, number: String): String {
        if (number.isBlank()) return number
        return PhoneNumberUtils.formatNumber(number, countryIso(context)) ?: number
    }

    /** SIM country first, then the network, then the device locale. */
    private fun countryIso(context: Context): String {
        val tm = context.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
        val iso = tm?.simCountryIso?.takeIf { it.isNotBlank() }
            ?: tm?.networkCountryIso?.takeIf { it.isNotBlank() }
            ?: Locale.getDefault().country
        return iso.uppercase(Locale.ROOT)
    }
}
