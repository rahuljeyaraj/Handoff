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
    const val CONTACT = "contact/{id}"
    const val CONTACT_EDIT = "contact/{id}/edit"

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
    val view = bandView(state)
    val theme by prefs.theme.collectAsState()
    val sort by prefs.sort.collectAsState()

    // Until the card is persisted locally (a later step), the band's own word
    // is the only source: nag only when it positively reports no card.
    val cardSet = state?.status?.provisioned != false
    val cardSummary = when (state?.status?.provisioned) {
        true -> "On the band"
        false -> "Not set"
        null -> "—"
    }

    NavHost(nav, startDestination = Routes.CONTACTS) {
        composable(Routes.CONTACTS) {
            val contacts by db.handshakes().all().collectAsState(initial = emptyList())

            ContactsScreen(
                contacts = contacts,
                band = view,
                cardSet = cardSet,
                sort = sort,
                onSort = prefs::setSort,
                onContact = { nav.navigate(Routes.contact(it.id)) },
                onBand = { nav.navigate(Routes.BAND) },
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
            // saved, so that is when the tick appears in the list.
            val promote = rememberLauncherForActivityResult(
                ActivityResultContracts.StartActivityForResult()
            ) { result ->
                if (result.resultCode == Activity.RESULT_OK) {
                    scope.launch { db.handshakes().markPromoted(id) }
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

        composable(Routes.SETTINGS) {
            SettingsScreen(
                band = view,
                cardSummary = cardSummary,
                theme = theme,
                sort = sort,
                appVersion = appVersion(context),
                onTheme = prefs::setTheme,
                onSort = prefs::setSort,
                onCard = { nav.navigate(Routes.CARD) },
                onBand = { nav.navigate(Routes.BAND) },
                onAdvanced = { nav.navigate(Routes.ADVANCED) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.BAND) {
            val chooser = rememberLauncherForActivityResult(
                ActivityResultContracts.StartIntentSenderForResult()
            ) { result ->
                if (result.resultCode != Activity.RESULT_OK) return@rememberLauncherForActivityResult
                Pairing.addressFrom(result.data)?.let { address ->
                    Pairing.remember(context, address)
                    BandService.start(context, address)
                    band.bind()
                }
            }
            BandScreen(
                band = view,
                cardSummary = cardSummary,
                firmware = null,
                onPair = {
                    Pairing.associate(
                        context as Activity,
                        onChooser = { chooser.launch(IntentSenderRequest.Builder(it).build()) },
                        onFailure = { /* the chooser reports its own failure */ },
                    )
                },
                onCard = { nav.navigate(Routes.CARD) },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.CARD) {
            CardScreen(
                onSave = { vcard ->
                    band.service?.provision(vcard)
                    nav.popBackStack()
                },
                onBack = { nav.popBackStack() },
            )
        }

        composable(Routes.ADVANCED) {
            AdvancedScreen(state = state, onBack = { nav.popBackStack() })
        }
    }
}

private fun appVersion(context: android.content.Context): String =
    runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull() ?: "—"
