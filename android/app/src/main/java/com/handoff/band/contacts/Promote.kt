package com.handoff.band.contacts

import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.provider.ContactsContract
import com.handoff.band.vcard.VCard

/**
 * Moving a received card into the system address book, architecture §11.3.
 *
 * PROMOTION IS AN EXPLICIT USER ACTION AND NEVER AUTOMATIC, and it is done
 * with `ContactsContract.Intents.Insert` rather than a ContentProvider write.
 * Two consequences, both deliberate:
 *
 *   - it needs NO permission at all. The app never declares WRITE_CONTACTS,
 *     so it cannot silently add anybody to an account that syncs to every
 *     device the wearer owns.
 *   - the system contact editor opens prefilled and the wearer confirms. A
 *     card that crossed a body in 250 ms is a partial card by design (design
 *     §9.5), and a partial card auto-committed as a real contact is exactly
 *     the "visibly wrong name in someone's address book" failure that CRC-16
 *     exists to prevent.
 *
 * The optional "auto-save new handshakes" setting §11.3 describes — a silent
 * provider insert, WRITE_CONTACTS requested at that point — is off by default
 * and deliberately not built at M2. It is one setting and one permission
 * request away when somebody actually wants it.
 */
object Promote {

    fun intentFor(card: VCard): Intent = Intent(ContactsContract.Intents.Insert.ACTION).apply {
        type = ContactsContract.RawContacts.CONTENT_TYPE

        putExtra(ContactsContract.Intents.Insert.NAME, card.displayName)
        card.mobile?.let {
            putExtra(ContactsContract.Intents.Insert.PHONE, it)
            putExtra(ContactsContract.Intents.Insert.PHONE_TYPE,
                ContactsContract.CommonDataKinds.Phone.TYPE_MOBILE)
        }
        card.work?.let {
            putExtra(ContactsContract.Intents.Insert.SECONDARY_PHONE, it)
            putExtra(ContactsContract.Intents.Insert.SECONDARY_PHONE_TYPE,
                ContactsContract.CommonDataKinds.Phone.TYPE_WORK)
        }
        card.email?.let {
            putExtra(ContactsContract.Intents.Insert.EMAIL, it)
            putExtra(ContactsContract.Intents.Insert.EMAIL_TYPE,
                ContactsContract.CommonDataKinds.Email.TYPE_WORK)
        }
        card.org?.let { putExtra(ContactsContract.Intents.Insert.COMPANY, it) }
        card.title?.let { putExtra(ContactsContract.Intents.Insert.JOB_TITLE, it) }
        card.note?.let { putExtra(ContactsContract.Intents.Insert.NOTES, it) }
    }
}

/**
 * The other direction: reading a contact the wearer picked, to provision the
 * band with (architecture §11.3, "a contact chosen from the phone's address
 * book").
 *
 * PHOTO is not read and never will be — `compact.c` rejects it at encode time
 * with an explicit error (§8.2), so carrying one here would turn a
 * provisioning into a failure rather than into a bigger card.
 *
 * The band holds a snapshot, not a live link. Whoever calls this should record
 * "based on contact X, last synced <when>" and offer a re-push, because
 * nothing tells the wristband when the wearer edits their own contact.
 */
object ContactReader {

    data class Fields(
        val displayName: String,
        val structuredName: String?,
        val mobile: String?,
        val work: String?,
        val email: String?,
        val org: String?,
        val title: String?,
    ) {
        fun toVCard(
            includeMobile: Boolean = true,
            includeWork: Boolean = true,
            includeEmail: Boolean = true,
            includeOrg: Boolean = true,
            includeTitle: Boolean = true,
        ): String = VCard.build(
            fullName = displayName,
            structuredName = structuredName,
            mobile = mobile.takeIf { includeMobile },
            work = work.takeIf { includeWork },
            email = email.takeIf { includeEmail },
            org = org.takeIf { includeOrg },
            title = title.takeIf { includeTitle },
        )
    }

    /** Requires READ_CONTACTS. [uri] is what ACTION_PICK handed back. */
    fun read(context: Context, uri: android.net.Uri): Fields? {
        val resolver = context.contentResolver

        val contactId = resolver.query(uri, arrayOf(ContactsContract.Contacts._ID),
            null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        } ?: return null

        var displayName = ""
        var structured: String? = null
        var mobile: String? = null
        var work: String? = null
        var email: String? = null
        var org: String? = null
        var title: String? = null

        resolver.query(
            ContactsContract.Data.CONTENT_URI, null,
            "${ContactsContract.Data.CONTACT_ID} = ?", arrayOf(contactId), null
        )?.use { c ->
            while (c.moveToNext()) {
                when (c.str(ContactsContract.Data.MIMETYPE)) {
                    ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE -> {
                        displayName =
                            c.str(ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME)
                                ?: displayName
                        val family =
                            c.str(ContactsContract.CommonDataKinds.StructuredName.FAMILY_NAME).orEmpty()
                        val given =
                            c.str(ContactsContract.CommonDataKinds.StructuredName.GIVEN_NAME).orEmpty()
                        if (family.isNotBlank() || given.isNotBlank())
                            structured = "$family;$given;;;"
                    }

                    ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE -> {
                        val number = c.str(ContactsContract.CommonDataKinds.Phone.NUMBER)
                        when (c.getInt(c.getColumnIndexOrThrow(
                            ContactsContract.CommonDataKinds.Phone.TYPE))) {
                            ContactsContract.CommonDataKinds.Phone.TYPE_WORK ->
                                work = work ?: number
                            else -> mobile = mobile ?: number
                        }
                    }

                    ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE ->
                        email = email ?: c.str(ContactsContract.CommonDataKinds.Email.ADDRESS)

                    ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE -> {
                        org = org ?: c.str(ContactsContract.CommonDataKinds.Organization.COMPANY)
                        title = title
                            ?: c.str(ContactsContract.CommonDataKinds.Organization.TITLE)
                    }
                }
            }
        }

        if (displayName.isBlank()) return null
        return Fields(displayName, structured, mobile, work, email, org, title)
    }

    private fun Cursor.str(column: String): String? {
        val i = getColumnIndex(column)
        return if (i >= 0) getString(i) else null
    }
}
