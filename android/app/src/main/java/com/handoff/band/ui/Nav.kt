package com.handoff.band.ui

import android.app.Activity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
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
import com.handoff.band.ble.BandClient
import com.handoff.band.ble.BandCode
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Gatt
import com.handoff.band.ble.Pairing
import com.handoff.band.contacts.Promote
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Handshake
import com.handoff.band.data.Merge
import com.handoff.band.data.OwnCard
import com.handoff.band.data.Prefs
import com.handoff.band.ui.screens.AdvancedScreen
import com.handoff.band.ui.screens.BandScreen
import com.handoff.band.ui.screens.CardEditScreen
import com.handoff.band.ui.screens.CardOnBand
import com.handoff.band.ui.screens.CardViewScreen
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
    const val CARD_EDIT = "card/edit"
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
    // Re-read whenever Pairing changes it: the chooser below, Forget on the
    // Band screen, or the service forgetting a band unpaired from settings.
    val pairingVersion by Pairing.version.collectAsState()
    val address = remember(pairingVersion) { Pairing.storedAddress(context) }
    val bandName = remember(pairingVersion) { Pairing.storedName(context) }
    val view = bandView(state, address, bandName, bandOff)
    val theme by prefs.theme.collectAsState()
    val sort by prefs.sort.collectAsState()

    val ownCard by prefs.ownCard.collectAsState()
    val cardSet = ownCard != null
    // "Rohan Iyer", or "Not set". Never a field count (§1, copy discipline).
    val cardSummary = ownCard?.name?.trim()?.takeIf { it.isNotEmpty() } ?: "Not set"

    // Set by Save on the card editor: the save itself is silent and local,
    // so the screen it pops back to says where the card is going.
    var cardSaved by remember { mutableStateOf<Notice?>(null) }
    fun saveCard(card: OwnCard) {
        prefs.setOwnCard(card)     // the service pushes it (§7)
        cardSaved = Notice(
            System.currentTimeMillis(),
            if (view.connection == BandView.Connection.CONNECTED)
                "Saved — writing it to your band"
            else "Saved — it'll be written to the band once it connects",
        )
    }

    // Set once Forget can't drop the OS bond itself (review item 14) — a
    // one-shot event for whichever screen the wearer lands back on.
    var forgetFailedAt by remember { mutableStateOf<Long?>(null) }

    // First-run pairing, every state of it on screen (pairing-page brief
    // §1). Owned here rather than by the setup page because the chooser
    // result and the service state both land here.
    var step by remember { mutableStateOf<PairStep>(PairStep.Scanning) }
    var locating by remember { mutableStateOf<Pairing.Locate?>(null) }

    fun couldNotFind(code: BandCode) = PairStep.Failed(
        "Couldn't find ${code.name}.\nIs it switched on and close by?", code)
    // The phone's scanner, not the band (Pairing's class comment). The only
    // remedy is the wearer's, so it is named.
    val cannotScan =
        "Bluetooth on this phone isn't finding anything.\nTurn it off and on, then try again."

    // The CompanionDeviceManager chooser. RESULT_OK carries one band, which
    // is remembered and connected to; anything else is the wearer dismissing
    // the sheet, and the page has to say so — the chooser says nothing.
    val chooser = rememberLauncherForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult()
    ) { result ->
        val code = (step as? PairStep.Looking)?.code ?: return@rememberLauncherForActivityResult
        if (result.resultCode != Activity.RESULT_OK) {
            step = PairStep.Failed("Pairing was cancelled", code)
            return@rememberLauncherForActivityResult
        }
        val found = Pairing.foundFrom(result.data)
        if (found == null) {
            step = PairStep.Failed("Couldn't connect to ${code.name}", code)
            return@rememberLauncherForActivityResult
        }
        // The label's name is authoritative; the chooser's display name is
        // whatever the stack had cached, which can be nothing.
        Pairing.remember(context, Pairing.Found(found.address, code.name))
        prefs.setBandOff(false)
        BandService.start(context, found.address)
        band.bind()
        step = PairStep.Connecting(code.name, code)
    }

    // Reached only after locate() heard the band seconds earlier, so a
    // chooser that then times out is the phone's scanning giving out
    // between the two, not a band that went away.
    fun associate(code: BandCode, located: Boolean) = Pairing.associate(
        context as Activity, code,
        onChooser = { chooser.launch(IntentSenderRequest.Builder(it).build()) },
        onFailure = { step = if (located) PairStep.Failed(cannotScan, code) else couldNotFind(code) },
    )

    // The same path whether the code was scanned or typed. Our own scan
    // confirms the band is on the air before the chooser is asked (brief
    // §2); a handset that cannot scan goes straight to the chooser.
    fun pair(code: BandCode) {
        locating?.cancel()
        step = PairStep.Looking(code)
        locating = Pairing.locate(
            context, code,
            onFound = { locating = null; associate(code, located = true) },
            onNotFound = { heard ->
                locating = null
                step = if (heard == 0) PairStep.Failed(cannotScan, code) else couldNotFind(code)
            },
        ) ?: run { associate(code, located = false); null }
    }

    // Connecting is the service's to finish: the link comes up and bonds
    // (ready), or the bond is refused, or nothing answers for 15 s. A band
    // remembered and then not connected is forgotten again, so that a retry
    // — or the next visit — starts clean rather than on top of a service
    // still trying.
    val connecting = step as? PairStep.Connecting
    LaunchedEffect(connecting, state?.ready, state?.lastError, state?.notFoundAt) {
        if (connecting == null) return@LaunchedEffect
        val s = state ?: return@LaunchedEffect
        val failed = when {
            s.ready -> { step = PairStep.Connected(connecting.name); return@LaunchedEffect }
            s.lastError == BandClient.ERR_BOND_REFUSED -> "Pairing was cancelled"
            s.lastError != null || s.notFoundAt != null -> "Couldn't connect to ${connecting.name}"
            else -> return@LaunchedEffect
        }
        BandService.forget(context)
        step = PairStep.Failed(failed, connecting.code)
    }

    // Home is the list, paired or not: unpaired, its status line is a single
    // "Pair a band" button into setup, and setup pops back to it when done.
    NavHost(nav, startDestination = Routes.CONTACTS) {
        composable(Routes.SETUP) {
            // A fresh attempt each visit, and a scan that cannot outlive the
            // page.
            LaunchedEffect(Unit) { step = PairStep.Scanning }
            DisposableEffect(Unit) { onDispose { locating?.cancel(); locating = null } }
            SetupScreen(
                step = step,
                onCode = { pair(it) },
                onSetUpCard = {
                    // Home underneath, the editor on top: back from the
                    // editor lands on the list, not on setup again.
                    nav.popBackStack()
                    nav.navigate(Routes.CARD)
                },
                onSkip = { nav.popBackStack() },
            )
        }

        composable(Routes.CONTACTS) {
            val contacts by db.handshakes().all().collectAsState(initial = emptyList())

            ContactsScreen(
                contacts = contacts,
                band = view,
                sort = sort,
                onSort = prefs::setSort,
                incompleteAt = state?.lastIncompleteAt,
                notFoundAt = state?.notFoundAt,
                forgetFailedAt = forgetFailedAt,
                cardSaved = cardSaved,
                onContact = { nav.navigate(Routes.contact(it.id)) },
                onDeleteMany = { ids -> scope.launch { db.handshakes().deleteMany(ids) } },
                onAddContact = { nav.navigate(Routes.CONTACT_NEW) },
                onBand = { nav.navigate(Routes.BAND) },
                onPair = { nav.navigate(Routes.SETUP) },
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
                hapticOn = view.hapticOn,
                onTheme = prefs::setTheme,
                onSort = prefs::setSort,
                onSetHaptic = { on -> band.service?.control(Gatt.haptic(on)) },
                onAdvanced = { nav.navigate(Routes.ADVANCED) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.BAND) {
            // The band can go from under this screen — the service forgets
            // one that was unpaired in Bluetooth settings — and unpaired
            // there is no Band screen (review item 12). Popping to home is
            // a no-op when Forget below already did it.
            LaunchedEffect(address) {
                if (address == null) nav.popBackStack(Routes.CONTACTS, inclusive = false)
            }
            BandScreen(
                band = view,
                cardSet = cardSet,
                cardSummary = cardSummary,
                firmware = view.firmware,
                notFoundAt = state?.notFoundAt,
                cardSaved = cardSaved,
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
                    nav.popBackStack()
                },
                onCard = { nav.navigate(Routes.CARD) },
                onBack = { nav.popBackStack() },
            )
        }

        // Your contact card: the saved card read-only, or the editor when
        // there is none yet. Both deletes pop to whatever is underneath
        // (Band, or home from setup); the card is cleared after the pop so
        // this destination never flashes the empty editor on the way out.
        composable(Routes.CARD) {
            val card = ownCard
            if (card == null) {
                CardEditScreen(
                    initial = null,
                    onSave = { saveCard(it); nav.popBackStack() },
                    onDelete = {},
                    onBack = { nav.popBackStack() },
                )
            } else {
                val pushed by prefs.pushedCard.collectAsState()
                CardViewScreen(
                    card = card,
                    onBand = CardOnBand.of(card, pushed, view.cardOnBand),
                    bandName = view.name,
                    cardSaved = cardSaved,
                    onEdit = { nav.navigate(Routes.CARD_EDIT) },
                    onDelete = { nav.popBackStack(); prefs.setOwnCard(null) },
                    onBack = { nav.popBackStack() },
                )
            }
        }

        composable(Routes.CARD_EDIT) {
            CardEditScreen(
                initial = ownCard,
                onSave = { saveCard(it); nav.popBackStack() },
                onDelete = {
                    nav.popBackStack(Routes.CARD, inclusive = true)
                    prefs.setOwnCard(null)
                },
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
