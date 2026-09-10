package com.handoff.band.data

import androidx.room.ColumnInfo
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import android.content.Context
import kotlinx.coroutines.flow.Flow

/**
 * The handshake history, architecture §11.3.
 *
 * EVERY card that arrives on `rx_vcard` is written here, always, with no
 * permission needed and no user action. Promoting one into the system address
 * book is a separate, explicit step — see `contacts/Promote.kt` — because a
 * conference handshake is not necessarily somebody the wearer wants
 * permanently in an account that syncs to every device they own.
 *
 * The raw vCard text is kept alongside the parsed fields. A card that crossed
 * a body in 250 ms is a partial card by design (design §9.5), and showing the
 * wearer exactly what arrived is more honest than showing them the subset this
 * app happened to understand.
 */
@Entity(tableName = "handshakes")
data class Handshake(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,

    @ColumnInfo(name = "received_at") val receivedAt: Long,

    /** Reconstructed vCard text, exactly as it came off the characteristic. */
    @ColumnInfo(name = "vcard") val vcard: String,

    @ColumnInfo(name = "display_name") val displayName: String,
    @ColumnInfo(name = "mobile") val mobile: String? = null,
    @ColumnInfo(name = "email") val email: String? = null,
    @ColumnInfo(name = "org") val org: String? = null,

    /** How many properties survived — the completeness badge (§8.4). */
    @ColumnInfo(name = "field_count") val fieldCount: Int = 0,

    /** Set once the wearer promotes it into Contacts. */
    @ColumnInfo(name = "promoted") val promoted: Boolean = false,
)

@Dao
interface HandshakeDao {
    @Query("SELECT * FROM handshakes ORDER BY received_at DESC")
    fun all(): Flow<List<Handshake>>

    @Query("SELECT * FROM handshakes WHERE id = :id")
    suspend fun byId(id: Long): Handshake?

    @Insert
    suspend fun insert(h: Handshake): Long

    @Query("UPDATE handshakes SET promoted = 1 WHERE id = :id")
    suspend fun markPromoted(id: Long)

    @Query("DELETE FROM handshakes WHERE id = :id")
    suspend fun delete(id: Long)
}

@Database(entities = [Handshake::class], version = 1, exportSchema = false)
abstract class HandoffDb : RoomDatabase() {
    abstract fun handshakes(): HandshakeDao

    companion object {
        @Volatile private var instance: HandoffDb? = null

        fun get(context: Context): HandoffDb = instance ?: synchronized(this) {
            instance ?: Room.databaseBuilder(
                context.applicationContext, HandoffDb::class.java, "handoff.db"
            ).build().also { instance = it }
        }
    }
}
