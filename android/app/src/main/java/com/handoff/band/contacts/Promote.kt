package com.handoff.band.contacts

import android.content.ContentProviderOperation
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.provider.ContactsContract
import com.handoff.band.data.Handshake
import com.handoff.band.data.Phone
import com.handoff.band.data.PhoneLabel
import com.handoff.band.data.Phones
import com.handoff.band.vcard.VCard

/**
 * Moving a received card into the system address book, architecture §11.3.
 *
 * PROMOTION IS AN EXPLICIT USER ACTION AND NEVER AUTOMATIC, and it is done
 * with `ContactsContract.Intents.Insert` rather than a ContentProvider write.
 * Two consequences, both deliberate:
 *
 *   - it needs NO permission at all. WRITE_CONTACTS is declared only for
 *     [update] below, is asked for at that tap and never at a save, so the
 *     app cannot silently add anybody to an account that syncs to every
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

    /**
     * The label as ContactsContract knows it. [custom] says whether the caller
     * can carry a LABEL string alongside: the provider write can, the Insert
     * intent cannot, and TYPE_CUSTOM with no label shows as a blank word.
     */
    internal fun contactsType(phone: Phone, custom: Boolean): Int = when (phone.label) {
        PhoneLabel.MOBILE -> ContactsContract.CommonDataKinds.Phone.TYPE_MOBILE
        PhoneLabel.WORK -> ContactsContract.CommonDataKinds.Phone.TYPE_WORK
        PhoneLabel.HOME -> ContactsContract.CommonDataKinds.Phone.TYPE_HOME
        PhoneLabel.MAIN -> ContactsContract.CommonDataKinds.Phone.TYPE_MAIN
        PhoneLabel.CUSTOM ->
            if (custom && phone.custom.isNotBlank())
                ContactsContract.CommonDataKinds.Phone.TYPE_CUSTOM
            else ContactsContract.CommonDataKinds.Phone.TYPE_OTHER
        PhoneLabel.NONE -> ContactsContract.CommonDataKinds.Phone.TYPE_OTHER
    }

    /**
     * Given and family name such that "given family" is the name exactly as
     * written. The last word before any bracket is the family name and the
     * bracket rides with it: "Vikram Sharma (Vivado License)" is Vikram /
     * Sharma (Vivado License), "Anne Marie Smith" is Anne Marie / Smith, and a
     * single word is a given name alone.
     */
    internal fun nameParts(name: String): Pair<String, String> {
        val n = name.trim().replace(Regex("\\s+"), " ")
        val cut = n.indexOf('(').let { if (it < 0) n.length else it }
        val words = n.substring(0, cut).trim().split(" ").filter { it.isNotEmpty() }
        if (words.size < 2) return n to ""
        val given = words.dropLast(1).joinToString(" ")
        return given to n.substring(given.length).trim()
    }

    fun intentFor(card: VCard): Intent = intentFor(
        name = card.displayName, phones = card.phones, email = card.email,
        org = card.org, title = card.title, note = card.note,
    )

    /**
     * From the stored row rather than the raw vCard, so the wearer's edits and
     * their note go into the system editor. A name they have corrected goes
     * as given + family ([nameParts]), so it carries exactly as written.
     */
    fun intentFor(h: Handshake): Intent = intentFor(
        name = h.displayName, phones = h.phones, email = h.email,
        org = h.org, title = h.title, note = h.note,
    )

    /**
     * The fallback when [update] cannot do the write: the system editor open
     * on the saved contact itself, so the wearer at least lands on the right
     * person. Same result contract as the Insert intent.
     */
    fun editIntentFor(h: Handshake): Intent? =
        h.contactUri?.let { Intent(Intent.ACTION_EDIT, Uri.parse(it)) }

    /**
     * The three phone slots the Insert intent has are exactly [Phones.MAX], so
     * every number on a card reaches the editor.
     *
     * ONE THING DOES NOT SURVIVE THIS PATH: a custom label. The Insert extras
     * carry a phone TYPE but no LABEL string, so a number the wearer called
     * "Reception" arrives in the editor as Other. [update] — the direct
     * provider write behind "Update phone contact" — writes TYPE_CUSTOM with
     * the label itself, so the word comes back the first time that runs.
     */
    fun intentFor(
        name: String,
        phones: List<Phone> = emptyList(),
        email: String? = null,
        org: String? = null,
        title: String? = null,
        note: String? = null,
    ): Intent = Intent(ContactsContract.Intents.Insert.ACTION).apply {
        type = ContactsContract.RawContacts.CONTENT_TYPE

        // The name as a structured row, not Insert.NAME: the editor splits a
        // single string on every space, so a renamed "Vikram Sharma (Vivado
        // License)" arrived as middle name "(Vivado", surname "License)".
        val (given, family) = nameParts(name)
        putParcelableArrayListExtra(ContactsContract.Intents.Insert.DATA, arrayListOf(
            ContentValues().apply {
                put(ContactsContract.Data.MIMETYPE,
                    ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE)
                put(ContactsContract.CommonDataKinds.StructuredName.GIVEN_NAME, given)
                if (family.isNotEmpty())
                    put(ContactsContract.CommonDataKinds.StructuredName.FAMILY_NAME, family)
            },
        ))

        val slots = listOf(
            ContactsContract.Intents.Insert.PHONE to
                ContactsContract.Intents.Insert.PHONE_TYPE,
            ContactsContract.Intents.Insert.SECONDARY_PHONE to
                ContactsContract.Intents.Insert.SECONDARY_PHONE_TYPE,
            ContactsContract.Intents.Insert.TERTIARY_PHONE to
                ContactsContract.Intents.Insert.TERTIARY_PHONE_TYPE,
        )
        phones.filterNot { it.blank }.take(slots.size).forEachIndexed { i, phone ->
            val (number, type) = slots[i]
            putExtra(number, phone.number)
            putExtra(type, contactsType(phone, custom = false))
        }
        email?.takeIf { it.isNotBlank() }?.let {
            putExtra(ContactsContract.Intents.Insert.EMAIL, it)
            putExtra(ContactsContract.Intents.Insert.EMAIL_TYPE,
                ContactsContract.CommonDataKinds.Email.TYPE_WORK)
        }
        org?.takeIf { it.isNotBlank() }?.let { putExtra(ContactsContract.Intents.Insert.COMPANY, it) }
        title?.takeIf { it.isNotBlank() }?.let { putExtra(ContactsContract.Intents.Insert.JOB_TITLE, it) }
        note?.takeIf { it.isNotBlank() }?.let { putExtra(ContactsContract.Intents.Insert.NOTES, it) }
    }

    /**
     * Whether the URI a previous Insert returned still resolves to a contact
     * (review item 16) — READ_CONTACTS is already held for provisioning.
     */
    fun exists(context: Context, uri: Uri): Boolean = runCatching {
        context.contentResolver.query(uri, arrayOf(ContactsContract.Contacts._ID), null, null, null)
            ?.use { it.moveToFirst() } ?: false
    }.getOrDefault(false)

    // ---- updating a saved contact ---------------------------------------

    /**
     * The raw contact behind [contactUri] that the save just created, to
     * remember for [update]. The editor hands back the aggregate contact; a
     * fresh save has one raw contact under it, and if the phone aggregated
     * it with somebody already there, the newest raw contact is ours.
     */
    fun rawContactIdOf(context: Context, contactUri: Uri): Long? =
        rawContactsOf(context, contactUri).maxOrNull()

    /**
     * Rewrite the phone's copy of [h] in place.
     *
     * THIS IS THE ONE PLACE THE APP WRITES TO CONTACTS, and it is the wearer
     * pressing "Update phone contact" on a person they already saved: the
     * Insert intent above can only create, and INSERT_OR_EDIT can only add
     * values to a contact the wearer picks by hand — a changed name will not
     * even find them. So the update is a direct write, with WRITE_CONTACTS
     * asked for at that tap and never before. Saving a new contact still goes
     * through the system editor with no permission.
     *
     * Only the raw contact the save created is touched, and only the kinds
     * of data a card carries — name, the two phones, email, organisation and
     * title, note. Those rows are replaced with the card's; a photo, an
     * address, a birthday the wearer added in Contacts stay as they are.
     *
     * @return false when the contact cannot be found or the write fails — the
     *   caller falls back to opening it in the system editor.
     */
    fun update(context: Context, h: Handshake): Boolean {
        val uri = h.contactUri?.let(Uri::parse) ?: return false
        val rawId = h.rawContactId ?: bestRawContactFor(context, uri, h) ?: return false

        val ops = arrayListOf<ContentProviderOperation>()
        ops += ContentProviderOperation.newDelete(ContactsContract.Data.CONTENT_URI)
            .withSelection(
                "${ContactsContract.Data.RAW_CONTACT_ID} = ? AND ${ContactsContract.Data.MIMETYPE} IN (?,?,?,?,?)",
                arrayOf(
                    rawId.toString(),
                    ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE,
                    ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE,
                    ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE,
                    ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE,
                    ContactsContract.CommonDataKinds.Note.CONTENT_ITEM_TYPE,
                ),
            ).build()

        fun row(mime: String, vararg values: Pair<String, Any>) {
            ops += ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                .withValue(ContactsContract.Data.MIMETYPE, mime)
                .apply { values.forEach { (k, v) -> withValue(k, v) } }
                .build()
        }

        // Given and family from [nameParts], as the Insert does, so a renamed
        // contact reads back exactly as the wearer wrote it.
        val (given, family) = nameParts(h.displayName)
        row(ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE,
            ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME to h.displayName,
            ContactsContract.CommonDataKinds.StructuredName.GIVEN_NAME to given,
            ContactsContract.CommonDataKinds.StructuredName.FAMILY_NAME to family)
        // One row per number, each with its own label. A custom one is
        // TYPE_CUSTOM plus the word itself, which is how the phone's own
        // Contacts app stores a label somebody typed.
        h.phones.filterNot { it.blank }.forEach { phone ->
            val fields = mutableListOf<Pair<String, Any>>(
                ContactsContract.CommonDataKinds.Phone.NUMBER to phone.number,
                ContactsContract.CommonDataKinds.Phone.TYPE to contactsType(phone, custom = true),
            )
            if (phone.label == PhoneLabel.CUSTOM && phone.custom.isNotBlank())
                fields += ContactsContract.CommonDataKinds.Phone.LABEL to phone.custom.trim()
            row(ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE, *fields.toTypedArray())
        }
        h.email?.takeIf { it.isNotBlank() }?.let {
            row(ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE,
                ContactsContract.CommonDataKinds.Email.ADDRESS to it,
                ContactsContract.CommonDataKinds.Email.TYPE to ContactsContract.CommonDataKinds.Email.TYPE_WORK)
        }
        if (!h.org.isNullOrBlank() || !h.title.isNullOrBlank()) {
            val fields = mutableListOf<Pair<String, Any>>(
                ContactsContract.CommonDataKinds.Organization.TYPE to
                    ContactsContract.CommonDataKinds.Organization.TYPE_WORK)
            h.org?.takeIf { it.isNotBlank() }?.let {
                fields += ContactsContract.CommonDataKinds.Organization.COMPANY to it }
            h.title?.takeIf { it.isNotBlank() }?.let {
                fields += ContactsContract.CommonDataKinds.Organization.TITLE to it }
            row(ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE, *fields.toTypedArray())
        }
        h.note?.takeIf { it.isNotBlank() }?.let {
            row(ContactsContract.CommonDataKinds.Note.CONTENT_ITEM_TYPE,
                ContactsContract.CommonDataKinds.Note.NOTE to it)
        }

        return runCatching {
            context.contentResolver.applyBatch(ContactsContract.AUTHORITY, ops)
            true
        }.getOrDefault(false)
    }

    /**
     * For a row saved before the raw contact id was recorded: of the raw
     * contacts under the phone contact, the one whose name, mobile or email
     * agrees most with the card — ties to the newest. The common case is a
     * single raw contact; the next is ours aggregated with a synced account.
     */
    private fun bestRawContactFor(context: Context, contactUri: Uri, h: Handshake): Long? {
        val candidates = rawContactsOf(context, contactUri)
        if (candidates.size <= 1) return candidates.firstOrNull()
        val contactId = contactIdOf(context, contactUri) ?: return null

        val score = candidates.associateWith { 0 }.toMutableMap()
        runCatching {
            context.contentResolver.query(
                ContactsContract.Data.CONTENT_URI,
                arrayOf(ContactsContract.Data.RAW_CONTACT_ID, ContactsContract.Data.MIMETYPE,
                        ContactsContract.Data.DATA1),
                "${ContactsContract.Data.CONTACT_ID} = ?", arrayOf(contactId.toString()), null,
            )?.use { c ->
                while (c.moveToNext()) {
                    val raw = c.getLong(0)
                    if (raw !in score) continue
                    val value = c.getString(2) ?: continue
                    val hit = when (c.getString(1)) {
                        ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE ->
                            value.equals(h.displayName, ignoreCase = true)
                        ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE ->
                            digits(value).isNotEmpty() &&
                                h.phones.any { digits(value) == digits(it.number) }
                        ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE ->
                            value.equals(h.email, ignoreCase = true)
                        else -> false
                    }
                    if (hit) score[raw] = score.getValue(raw) + 1
                }
            }
        }
        return candidates.maxWithOrNull(compareBy({ score.getValue(it) }, { it }))
    }

    private fun digits(s: String?): String = s.orEmpty().filter { it.isDigit() }

    private fun contactIdOf(context: Context, contactUri: Uri): Long? = runCatching {
        context.contentResolver.query(
            contactUri, arrayOf(ContactsContract.Contacts._ID), null, null, null,
        )?.use { if (it.moveToFirst()) it.getLong(0) else null }
    }.getOrNull()

    /** Raw contacts (not deleted) under the aggregate contact at [contactUri]. */
    private fun rawContactsOf(context: Context, contactUri: Uri): List<Long> = runCatching {
        val contactId = contactIdOf(context, contactUri) ?: return emptyList()
        context.contentResolver.query(
            ContactsContract.RawContacts.CONTENT_URI,
            arrayOf(ContactsContract.RawContacts._ID),
            "${ContactsContract.RawContacts.CONTACT_ID} = ? AND ${ContactsContract.RawContacts.DELETED} = 0",
            arrayOf(contactId.toString()), null,
        )?.use { c ->
            buildList { while (c.moveToNext()) add(c.getLong(0)) }
        } ?: emptyList()
    }.getOrDefault(emptyList())
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
        val phones: List<Phone>,
        val email: String?,
        val org: String?,
        val title: String?,
    ) {
        fun toVCard(
            includePhones: Boolean = true,
            includeEmail: Boolean = true,
            includeOrg: Boolean = true,
            includeTitle: Boolean = true,
        ): String = VCard.build(
            fullName = displayName,
            structuredName = structuredName,
            phones = if (includePhones) phones else emptyList(),
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
        val phones = mutableListOf<Phone>()
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
                        val custom = c.str(ContactsContract.CommonDataKinds.Phone.LABEL).orEmpty()
                        val label = when (c.getInt(c.getColumnIndexOrThrow(
                            ContactsContract.CommonDataKinds.Phone.TYPE))) {
                            ContactsContract.CommonDataKinds.Phone.TYPE_MOBILE -> PhoneLabel.MOBILE
                            ContactsContract.CommonDataKinds.Phone.TYPE_WORK -> PhoneLabel.WORK
                            ContactsContract.CommonDataKinds.Phone.TYPE_HOME -> PhoneLabel.HOME
                            ContactsContract.CommonDataKinds.Phone.TYPE_MAIN -> PhoneLabel.MAIN
                            ContactsContract.CommonDataKinds.Phone.TYPE_CUSTOM ->
                                if (custom.isNotBlank()) PhoneLabel.CUSTOM else PhoneLabel.NONE
                            else -> PhoneLabel.NONE
                        }
                        if (!number.isNullOrBlank() && phones.size < Phones.MAX)
                            phones += Phone(number, label, custom)
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
        return Fields(displayName, structured, phones, email, org, title)
    }

    private fun Cursor.str(column: String): String? {
        val i = getColumnIndex(column)
        return if (i >= 0) getString(i) else null
    }
}
