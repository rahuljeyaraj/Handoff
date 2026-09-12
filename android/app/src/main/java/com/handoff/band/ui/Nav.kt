package com.handoff.band.ui

import android.app.Activity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Pairing
import com.handoff.band.contacts.Promote
import com.handoff.band.data.HandoffDb
import com.handoff.band.data.Prefs
import com.handoff.band.ui.screens.AdvancedScreen
import com.handoff.band.ui.screens.BandScreen
import com.handoff.band.ui.screens.CardScreen
import com.handoff.band.ui.screens.ContactsScreen
import com.handoff.band.ui.screens.SettingsScreen
import com.handoff.band.vcard.VCard

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
}

@Composable
fun HandoffNavHost(nav: NavHostController = rememberNavController()) {
    val context = LocalContext.current
    val band = LocalBand.current
    val prefs = remember { Prefs.get(context) }

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
            val contacts by HandoffDb.get(context).handshakes().all()
                .collectAsState(initial = emptyList())
            val promote = rememberLauncherForActivityResult(
                ActivityResultContracts.StartActivityForResult()
            ) { /* the system editor owns the outcome */ }

            ContactsScreen(
                contacts = contacts,
                band = view,
                cardSet = cardSet,
                sort = sort,
                onSort = prefs::setSort,
                onContact = { promote.launch(Promote.intentFor(VCard.parse(it.vcard))) },
                onBand = { nav.navigate(Routes.BAND) },
                onSetUpCard = { nav.navigate(Routes.CARD) },
                onSettings = { nav.navigate(Routes.SETTINGS) },
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
