package com.handoff.band.ui

import android.app.Activity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.handoff.band.ble.BandCode
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Pairing
import com.handoff.band.contacts.Promote
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.data.Merge
import com.handoff.band.data.Prefs
import com.handoff.band.ui.screens.AdvancedScreen
import com.handoff.band.ui.screens.BandScreen
import com.handoff.band.ui.screens.CardScreen
import com.handoff.band.ui.screens.ContactDetailScreen
import com.handoff.band.ui.screens.ContactEditScreen
import com.handoff.band.ui.screens.ContactsScreen
import com.handoff.band.ui.screens.SettingsScreen
import com.handoff.band.ui.screens.SetupScreen
import kotlinx.coroutines.launch

/**
 * The screen set from design decisions §1. Contacts is home; there is no
 * bottom navigation because the app has two destinations and Material wants
 * three to five.
 */
object Routes {
    const val CONTACTS = "contacts"
    const val SETTINGS = "settings"
    const val BAND = "band"
    const val CARD = "card"
    const val ADVANCED = "advanced"
    const val SETUP = "setup"
    const val CONTACT = "contact/{id}"
    const val CONTACT_EDIT = "contact/{id}/edit"
    const val CONTACT_NEW = "contact_new"

    fun contact(id: Long) = "contact/$id"
    fun contactEdit(id: Long) = "contact/$id/edit"
}

@Composable
fun HandoffNavHost(nav: NavHostController = rememberNavController()) {
    val context = LocalContext.current
    val band = LocalBand.current
    val prefs = remember { Prefs.get(context) }

    val db = remember { HandoffDb.get(context) }
    val scope = rememberCoroutineScope()

    val state by band.state.collectAsState()
    val bandOff by prefs.bandOff.collectAsState()
    var address by remember { mutableStateOf(Pairing.storedAddress(context)) }
    var bandName by remember { mutableStateOf(Pairing.storedName(context)) }
    val view = bandView(state, address, bandName, bandOff)
    val theme by prefs.theme.collectAsState()
    val sort by prefs.sort.collectAsState()

    val ownCard by prefs.ownCard.collectAsState()
    val cardSet = ownCard != null
    // "Rohan Iyer", or "Not set". Never a field count (§1, copy discipline).
    val cardSummary = ownCard?.name?.trim()?.takeIf { it.isNotEmpty() } ?: "Not set"

    // Set once Forget can't drop the OS bond itself (review item 14) — a
    // one-shot event for whichever screen the wearer lands back on.
    var forgetFailedAt by remember { mutableStateOf<Long?>(null) }

    // A CompanionDeviceManager chooser, shared by first-run setup and the
    // Band screen. The result is one band, remembered and connected to.
    val chooser = rememberLauncherForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult()
    ) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@rememberLauncherForActivityResult
        Pairing.foundFrom(result.data)?.let { found ->
            Pairing.remember(context, found)
            prefs.setBandOff(false)
            address = found.address
            bandName = found.name
            BandService.start(context, found.address)
            band.bind()
        }
    }
    fun pair(target: BandCode?) = Pairing.associate(
        context as Activity, target,
        onChooser = { chooser.launch(IntentSenderRequest.Builder(it).build()) },
        onFailure = { /* the chooser reports its own failure */ },
    )

    // First run lands on setup; a paired phone lands on the list.
    val start = remember { if (Pairing.storedAddress(context) == null) Routes.SETUP else Routes.CONTACTS }

    NavHost(nav, startDestination = start) {
        composable(Routes.SETUP) {
            SetupScreen(
                pairedName = if (address != null) (bandName ?: "Handoff band") else null,
                onCode = { pair(it) },
                onSetUpCard = {
                    // Home underneath, the editor on top: back from the
                    // editor lands on the list, not on setup again.
                    nav.navigate(Routes.CONTACTS) { popUpTo(Routes.SETUP) { inclusive = true } }
                    nav.navigate(Routes.CARD)
                },
                onSkip = {
                    nav.navigate(Routes.CONTACTS) { popUpTo(Routes.SETUP) { inclusive = true } }
                },
            )
        }

        composable(Routes.CONTACTS) {
            val contacts by db.handshakes().all().collectAsState(initial = emptyList())

            ContactsScreen(
                contacts = contacts,
                band = view,
                cardSet = cardSet,
                sort = sort,
                onSort = prefs::setSort,
                incompleteAt = state?.lastIncompleteAt,
                notFoundAt = state?.notFoundAt,
                forgetFailedAt = forgetFailedAt,
                onContact = { nav.navigate(Routes.contact(it.id)) },
                onDeleteMany = { ids -> scope.launch { db.handshakes().deleteMany(ids) } },
                onAddContact = { nav.navigate(Routes.CONTACT_NEW) },
                onBand = { nav.navigate(Routes.BAND) },
                onPair = { nav.navigate(Routes.SETUP) },
                onSetUpCard = { nav.navigate(Routes.CARD) },
                onSettings = { nav.navigate(Routes.SETTINGS) },
            )
        }

        composable(
            Routes.CONTACT,
            arguments = listOf(navArgument("id") { type = NavType.LongType }),
        ) { entry ->
            val id = entry.arguments?.getLong("id") ?: return@composable
            val contact by db.handshakes().observe(id).collectAsState(initial = null)
            val c = contact ?: return@composable
            val duplicates by db.handshakes()
                .possibleDuplicates(c.id, c.phoneKey, c.emailKey)
                .collectAsState(initial = emptyList())

            // The system editor returns OK only when the person actually
            // saved, and hands back the new contact's URI so a later delete
            // in Contacts can be noticed (review item 16).
            val promote = rememberLauncherForActivityResult(
                ActivityResultContracts.StartActivityForResult()
            ) { result ->
                if (result.resultCode == Activity.RESULT_OK) {
                    val uri = result.data?.data?.toString()
                    scope.launch { db.handshakes().markPromoted(id, uri) }
                }
            }

            // Checked once per visit, not continuously: a deleted contact
            // clears "Saved to your phone" the next time this screen opens.
            LaunchedEffect(c.id) {
                val uri = c.contactUri
                if (c.promoted && uri != null && !Promote.exists(context, android.net.Uri.parse(uri))) {
                    db.handshakes().update(c.copy(promoted = false, contactUri = null))
                }
            }

            ContactDetailScreen(
                contact = c,
                duplicates = duplicates,
                onEdit = { nav.navigate(Routes.contactEdit(id)) },
                onSaveToPhone = { promote.launch(Promote.intentFor(c)) },
                onMerge = { other ->
                    scope.launch {
                        db.handshakes().update(Merge.merge(into = c, from = other))
                        db.handshakes().delete(other.id)
                    }
                },
                onDelete = {
                    scope.launch { db.handshakes().delete(id) }
                    nav.popBackStack(Routes.CONTACTS, inclusive = false)
                },
                onBack = { nav.popBackStack() },
            )
        }

        composable(
            Routes.CONTACT_EDIT,
            arguments = listOf(navArgument("id") { type = NavType.LongType }),
        ) { entry ->
            val id = entry.arguments?.getLong("id") ?: return@composable
            var loaded by remember { mutableStateOf<Handshake?>(null) }
            LaunchedEffect(id) { loaded = db.handshakes().byId(id) }
            val c = loaded ?: return@composable

            ContactEditScreen(
                contact = c,
                onSave = { edited ->
                    scope.launch { db.handshakes().update(edited) }
                    nav.popBackStack()
                },
                onDelete = {
                    scope.launch { db.handshakes().delete(id) }
                    nav.popBackStack(Routes.CONTACTS, inclusive = false)
                },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.CONTACT_NEW) {
            ContactEditScreen(
                contact = null,
                onSave = { created ->
                    scope.launch { db.handshakes().insert(created) }
                    nav.popBackStack()
                },
                onDelete = null,
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.SETTINGS) {
            SettingsScreen(
                theme = theme,
                sort = sort,
                appVersion = appVersion(context),
                onTheme = prefs::setTheme,
                onSort = prefs::setSort,
                onAdvanced = { nav.navigate(Routes.ADVANCED) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.BAND) {
            BandScreen(
                band = view,
                cardSummary = cardSummary,
                firmware = view.firmware,
                notFoundAt = state?.notFoundAt,
                onDisconnect = {
                    if (bandOff) {
                        BandService.reconnect(context)
                        band.bind()
                    } else {
                        BandService.disconnect(context)
                    }
                },
                // Pops back to the list (review item 12): with the unpaired
                // Band screen gone, there is nothing left here to show once
                // the band is forgotten.
                onForget = {
                    if (!BandService.forget(context)) forgetFailedAt = System.currentTimeMillis()
                    address = null
                    bandName = null
                    nav.popBackStack()
                },
                onCard = { nav.navigate(Routes.CARD) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.CARD) {
            CardScreen(
                initial = ownCard,
                onSave = { card ->
                    prefs.setOwnCard(card)     // the service pushes it (§7)
                    nav.popBackStack()
                },
                // Stays on the editor rather than popping back (review O2):
                // the empty form is its own confirmation, and every route
                // here is one tap away regardless.
                onRemove = { prefs.setOwnCard(null) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.ADVANCED) {
            AdvancedScreen(state = state, ownCard = ownCard, onBack = { nav.popBackStack() })
        }
    }
}

private fun appVersion(context: android.content.Context): String =
    runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull() ?: "—"
