package com.handoff.band.data

import android.content.ContentValues
import android.content.Context
import androidx.room.ColumnInfo
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Entity
import androidx.room.Index
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.Update
import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase
import kotlinx.coroutines.flow.Flow

/**
 * The handshake history, architecture §11.3.
 *
 * EVERY card that arrives on `rx_vcard` with a way to reach the person is
 * written here, with no permission needed and no user action. Promoting one
 * into the system address book is a separate, explicit step — see
 * `contacts/Promote.kt` — because a conference handshake is not necessarily
 * somebody the wearer wants permanently in an account that syncs to every
 * device they own.
 *
 * The raw vCard text is kept alongside the parsed fields and is never edited.
 * A card that crossed a body in 250 ms is a partial card by design (design
 * §9.5), and showing the wearer exactly what arrived is more honest than
 * showing them the subset this app happened to understand. The parsed columns
 * are what the user edits.
 *
 * `phone_key` and `email_key` are the dedup identity (design decisions §3):
 * two independent indexed columns rather than one composite, so a name+email
 * card today merges with a name+email+phone card next week.
 */
@Entity(
    tableName = "handshakes",
    indices = [Index("phone_key"), Index("email_key")],
)
data class Handshake(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,

    @ColumnInfo(name = "received_at") val receivedAt: Long,

    /** Reconstructed vCard text, exactly as it came off the characteristic. */
    @ColumnInfo(name = "vcard") val vcard: String,

    @ColumnInfo(name = "display_name") val displayName: String,
    @ColumnInfo(name = "mobile") val mobile: String? = null,
    @ColumnInfo(name = "work") val work: String? = null,
    @ColumnInfo(name = "email") val email: String? = null,
    @ColumnInfo(name = "org") val org: String? = null,
    @ColumnInfo(name = "title") val title: String? = null,

    /** The wearer's own words about this person. Never from the band. */
    @ColumnInfo(name = "note") val note: String? = null,

    @ColumnInfo(name = "phone_key") val phoneKey: String? = Keys.phone(mobile),
    @ColumnInfo(name = "email_key") val emailKey: String? = Keys.email(email),

    /** How many properties survived — the completeness badge (§8.4). */
    @ColumnInfo(name = "field_count") val fieldCount: Int = 0,

    /** Set once the wearer promotes it into Contacts. */
    @ColumnInfo(name = "promoted") val promoted: Boolean = false,

    /**
     * The URI the system contact editor returned for the promoted contact
     * (review item 16). Checked for existence when the detail screen opens,
     * so deleting the contact in Contacts clears "Saved to your phone" here.
     */
    @ColumnInfo(name = "contact_uri") val contactUri: String? = null,

    /**
     * The raw contact the save created — the exact rows "Update phone
     * contact" rewrites, so a later edit here lands on this person and
     * nobody else. Null for rows saved before this was recorded.
     */
    @ColumnInfo(name = "raw_contact_id") val rawContactId: Long? = null,

    /**
     * Entered by hand on the "+" screen rather than received over the body
     * link (review item 8) — "Added" versus "Met" on the detail screen (O4).
     */
    @ColumnInfo(name = "added_by_hand") val addedByHand: Boolean = false,

    /**
     * Edited here after it was saved to the phone, so the phone's copy is
     * behind. The detail screen's one button reads "Update phone contact"
     * while this is set; cleared by the next save.
     */
    @ColumnInfo(name = "edited_since_promote") val editedSincePromote: Boolean = false,
) {
    /** Recompute the keys from the current mobile and email. Call after any edit. */
    fun rekeyed(): Handshake = copy(phoneKey = Keys.phone(mobile), emailKey = Keys.email(email))

    /** After a change to the fields: the phone's copy, if there is one, is now stale. */
    fun edited(): Handshake = if (promoted) copy(editedSincePromote = true) else this
}

@Dao
interface HandshakeDao {
    @Query("SELECT * FROM handshakes ORDER BY received_at DESC")
    fun all(): Flow<List<Handshake>>

    @Query("SELECT * FROM handshakes WHERE id = :id")
    suspend fun byId(id: Long): Handshake?

    @Query("SELECT * FROM handshakes WHERE id = :id")
    fun observe(id: Long): Flow<Handshake?>

    /** The first row sharing either key. Callers pass null for a key they lack. */
    @Query(
        "SELECT * FROM handshakes WHERE (:phoneKey IS NOT NULL AND phone_key = :phoneKey) " +
            "OR (:emailKey IS NOT NULL AND email_key = :emailKey) ORDER BY received_at DESC LIMIT 1"
    )
    suspend fun matching(phoneKey: String?, emailKey: String?): Handshake?

    /** Other rows that share a key with [id] — the possible-duplicate banner. */
    @Query(
        "SELECT * FROM handshakes WHERE id != :id AND (" +
            "(:phoneKey IS NOT NULL AND phone_key = :phoneKey) OR " +
            "(:emailKey IS NOT NULL AND email_key = :emailKey)) ORDER BY received_at DESC"
    )
    fun possibleDuplicates(id: Long, phoneKey: String?, emailKey: String?): Flow<List<Handshake>>

    @Insert
    suspend fun insert(h: Handshake): Long

    @Update
    suspend fun update(h: Handshake)

    @Query(
        "UPDATE handshakes SET promoted = 1, contact_uri = :uri, raw_contact_id = :rawId, " +
            "edited_since_promote = 0 WHERE id = :id"
    )
    suspend fun markPromoted(id: Long, uri: String?, rawId: Long?)

    @Query("DELETE FROM handshakes WHERE id = :id")
    suspend fun delete(id: Long)

    /** Batch delete for selection mode (review item 7). */
    @Query("DELETE FROM handshakes WHERE id IN (:ids)")
    suspend fun deleteMany(ids: Collection<Long>)
}

@Database(entities = [Handshake::class], version = 6, exportSchema = false)
abstract class HandoffDb : RoomDatabase() {
    abstract fun handshakes(): HandshakeDao

    companion object {
        @Volatile private var instance: HandoffDb? = null

        fun get(context: Context): HandoffDb = instance ?: synchronized(this) {
            instance ?: Room.databaseBuilder(
                context.applicationContext, HandoffDb::class.java, "handoff.db"
            ).addMigrations(MIGRATION_1_2, MIGRATION_2_3, MIGRATION_3_4, MIGRATION_4_5, MIGRATION_5_6)
                .build().also { instance = it }
        }

        /**
         * Version 1 was the M2 bench schema. Version 2 adds the second phone,
         * the title, the note, and the two dedup keys, and backfills the keys
         * from the rows already there so a phone that has been on the bench
         * keeps its history.
         *
         * A real migration rather than `fallbackToDestructiveMigration()`,
         * because "the app update deleted my contacts" is not a demo-day risk
         * worth taking to save twenty lines. `exportSchema` stays false: the
         * schema JSON needs a KSP argument and a directory wired in, and at
         * version 2 the columns are all listed right here.
         */
        val MIGRATION_1_2 = object : Migration(1, 2) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE handshakes ADD COLUMN work TEXT")
                db.execSQL("ALTER TABLE handshakes ADD COLUMN title TEXT")
                db.execSQL("ALTER TABLE handshakes ADD COLUMN note TEXT")
                db.execSQL("ALTER TABLE handshakes ADD COLUMN phone_key TEXT")
                db.execSQL("ALTER TABLE handshakes ADD COLUMN email_key TEXT")
                db.execSQL("CREATE INDEX IF NOT EXISTS index_handshakes_phone_key ON handshakes(phone_key)")
                db.execSQL("CREATE INDEX IF NOT EXISTS index_handshakes_email_key ON handshakes(email_key)")

                db.query("SELECT id, mobile, email FROM handshakes").use { c ->
                    while (c.moveToNext()) {
                        val id = c.getLong(0)
                        val values = ContentValues().apply {
                            put("phone_key", Keys.phone(c.getString(1)))
                            put("email_key", Keys.email(c.getString(2)))
                        }
                        db.update("handshakes", android.database.sqlite.SQLiteDatabase.CONFLICT_NONE,
                                  values, "id = ?", arrayOf(id))
                    }
                }
            }
        }

        /** Version 3 adds the promoted contact's URI (review item 16). */
        val MIGRATION_2_3 = object : Migration(2, 3) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE handshakes ADD COLUMN contact_uri TEXT")
            }
        }

        /**
         * Version 4 adds "entered by hand" (review item 8). Existing rows all
         * came off the body link, so the default of 0 is already correct for
         * every row already on the phone.
         */
        val MIGRATION_3_4 = object : Migration(3, 4) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE handshakes ADD COLUMN added_by_hand INTEGER NOT NULL DEFAULT 0")
            }
        }

        /**
         * Version 5 adds "edited since saved to the phone". Nothing on the
         * phone knows whether an old row was edited after its save, so every
         * row starts as up to date and the flag is earned by the next edit.
         */
        val MIGRATION_4_5 = object : Migration(4, 5) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE handshakes ADD COLUMN edited_since_promote INTEGER NOT NULL DEFAULT 0")
            }
        }

        /**
         * Version 6 records which raw contact a save created, so an update
         * rewrites that one. Rows saved earlier have null and resolve it at
         * update time when the phone contact has a single raw contact.
         */
        val MIGRATION_5_6 = object : Migration(5, 6) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE handshakes ADD COLUMN raw_contact_id INTEGER")
            }
        }
    }
}
