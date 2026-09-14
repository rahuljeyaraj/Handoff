package com.handoff.band.ui.screens

import android.content.Intent
import android.provider.Settings as SystemSettings
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.handoff.band.data.Handshake
import com.handoff.band.data.Prefs
import com.handoff.band.ui.BandView
import com.handoff.band.ui.Notice
import com.handoff.band.ui.components.Avatar
import com.handoff.band.ui.components.BandStatusLine
import com.handoff.band.ui.components.DateHeader
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.HandoffSnackbarHost
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

/**
 * Home. The contact list IS the home screen (design decisions §1) — there is
 * no separate destination to navigate to. Band state is one tappable line
 * under the app bar; everything else lives behind the cog.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
    contacts: List<Handshake>,
    band: BandView,
    sort: Prefs.Sort,
    onSort: (Prefs.Sort) -> Unit,
    incompleteAt: Long?,
    notFoundAt: Long?,
    forgetFailedAt: Long?,
    cardSaved: Notice?,
    onContact: (Handshake) -> Unit,
    onDeleteMany: (Set<Long>) -> Unit,
    onAddContact: () -> Unit,
    onBand: () -> Unit,
    onPair: () -> Unit,
    onSettings: () -> Unit,
) {
    val context = LocalContext.current
    var searching by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }
    var selection by remember { mutableStateOf(emptySet<Long>()) }
    var confirmDeleteSelection by remember { mutableStateOf(false) }
    val selecting = selection.isNotEmpty()
    val snackbar = remember { SnackbarHostState() }

    fun toggleSelect(h: Handshake) {
        selection = if (h.id in selection) selection - h.id else selection + h.id
    }

    BackHandler(enabled = selecting) { selection = emptySet() }
    BackHandler(enabled = searching) { searching = false; query = "" }

    // A handshake that arrived with no phone and no email is not a contact,
    // but the band buzzed for it, so it does not vanish without a word.
    LaunchedEffect(incompleteAt) {
        if (incompleteAt != null && System.currentTimeMillis() - incompleteAt < 60_000) {
            snackbar.showSnackbar("Handshake didn't complete — try again")
        }
    }

    // "Looking for the band" that never gives up, said out loud (review item
    // 15, O8) — one-shot per timestamp, same shape as the effect above.
    LaunchedEffect(notFoundAt) {
        if (notFoundAt != null && System.currentTimeMillis() - notFoundAt < 60_000) {
            snackbar.showSnackbar(band.statusLabel)
        }
    }

    // Back from the card editor: the save is local, and this is where it
    // says whether the band has it yet (the status line's card glyph says
    // so afterwards).
    LaunchedEffect(cardSaved) {
        cardSaved?.take()?.let { snackbar.showSnackbar(it) }
    }

    // Forget couldn't drop the OS bond by itself (review item 14) — point
    // the wearer at Bluetooth settings rather than leave it silently stale.
    LaunchedEffect(forgetFailedAt) {
        if (forgetFailedAt != null && System.currentTimeMillis() - forgetFailedAt < 60_000) {
            val result = snackbar.showSnackbar(
                message = "Couldn't remove the Bluetooth pairing",
                actionLabel = "Settings",
            )
            if (result == SnackbarResult.ActionPerformed) {
                context.startActivity(Intent(SystemSettings.ACTION_BLUETOOTH_SETTINGS))
            }
        }
    }

    Scaffold(
        snackbarHost = { HandoffSnackbarHost(snackbar) },
        topBar = {
            if (selecting) {
                TopAppBar(
                    navigationIcon = {
                        IconButton(onClick = { selection = emptySet() }) {
                            Icon(Icons.Filled.Close, contentDescription = "Cancel selection")
                        }
                    },
                    title = { Text("${selection.size} selected") },
                    actions = {
                        IconButton(onClick = { confirmDeleteSelection = true }) {
                            Icon(Icons.Filled.Delete, contentDescription = "Delete")
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface),
                )
            } else if (searching) {
                SearchBar(query, onQuery = { query = it },
                          onClose = { searching = false; query = "" })
            } else {
                TopAppBar(
                    title = {
                        Icon(HandoffIcons.Wordmark, contentDescription = "Handoff",
                             tint = MaterialTheme.colorScheme.onSurface,
                             modifier = Modifier.height(20.dp).width(107.dp))
                    },
                    actions = {
                        // Create is an app-bar action beside search, not a
                        // pill heading the list: the wearer found the pill
                        // too much on a home that already has one for the
                        // band. Four actions fit beside the wordmark.
                        IconButton(onClick = onAddContact) {
                            Icon(HandoffIcons.PersonAdd, contentDescription = "Create contact")
                        }
                        IconButton(onClick = { searching = true }) {
                            Icon(Icons.Filled.Search, contentDescription = "Search")
                        }
                        IconButton(onClick = {
                            onSort(if (sort == Prefs.Sort.NEWEST) Prefs.Sort.AZ else Prefs.Sort.NEWEST)
                        }) {
                            Icon(HandoffIcons.Sort, contentDescription = "Sort: ${sort.label}")
                        }
                        IconButton(onClick = onSettings) {
                            Icon(Icons.Filled.Settings, contentDescription = "Settings")
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface),
                )
            }
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            if (searching) {
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                val hits = remember(contacts, query) { contacts.filter { matches(it, query) } }
                LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 24.dp)) {
                    items(hits, key = { it.id }) { h ->
                        ContactRow(h, timeLabel(h.receivedAt, sectioned = false),
                                   highlight = query,
                                   selecting = selecting, selected = h.id in selection,
                                   onClick = { if (selecting) toggleSelect(h) else onContact(h) },
                                   onLongClick = { toggleSelect(h) })
                    }
                }
                return@Column
            }

            BandStatusLine(band, onClick = onBand, onPair = onPair)

            if (contacts.isEmpty()) {
                EmptyState()
            } else {
                ContactList(
                    contacts, sort,
                    onClick = { if (selecting) toggleSelect(it) else onContact(it) },
                    selecting = selecting, selected = { it.id in selection },
                    onLongClick = { toggleSelect(it) },
                )
            }
        }
    }

    if (confirmDeleteSelection) {
        val n = selection.size
        AlertDialog(
            onDismissRequest = { confirmDeleteSelection = false },
            title = { Text("Delete $n ${if (n == 1) "contact" else "contacts"}?") },
            confirmButton = {
                TextButton(onClick = {
                    confirmDeleteSelection = false
                    onDeleteMany(selection)
                    selection = emptySet()
                }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDeleteSelection = false }) { Text("Cancel") } },
        )
    }
}

/**
 * Search replaces the app bar in place (§4b): back arrow, the query, a clear
 * button, and the list filters beneath it as you type.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SearchBar(query: String, onQuery: (String) -> Unit, onClose: () -> Unit) {
    val focus = remember { FocusRequester() }
    TopAppBar(
        navigationIcon = {
            IconButton(onClick = onClose) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Close search")
            }
        },
        title = {
            BasicTextField(
                value = query,
                onValueChange = onQuery,
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(
                    color = MaterialTheme.colorScheme.onSurface),
                cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                modifier = Modifier.fillMaxWidth().focusRequester(focus),
                decorationBox = { inner ->
                    if (query.isEmpty()) {
                        Text("Search contacts", style = MaterialTheme.typography.bodyLarge,
                             color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    inner()
                },
            )
        },
        actions = {
            if (query.isNotEmpty()) {
                IconButton(onClick = { onQuery("") }) {
                    Icon(Icons.Filled.Clear, contentDescription = "Clear")
                }
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.surface),
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}

/** Name and organisation, because "who was that person from PCBWay" is the real question. */
fun matches(h: Handshake, query: String): Boolean {
    val q = query.trim()
    if (q.isEmpty()) return true
    return h.displayName.contains(q, ignoreCase = true) ||
        h.org?.contains(q, ignoreCase = true) == true
}

/** Newest-first under Today / Yesterday / Earlier; A–Z flat. */
@Composable
private fun ContactList(
    contacts: List<Handshake>,
    sort: Prefs.Sort,
    onClick: (Handshake) -> Unit,
    selecting: Boolean,
    selected: (Handshake) -> Boolean,
    onLongClick: (Handshake) -> Unit,
) {
    val now = System.currentTimeMillis()
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(top = 4.dp, bottom = 24.dp)) {
        when (sort) {
            Prefs.Sort.NEWEST -> {
                val groups = contacts
                    .sortedByDescending { it.receivedAt }
                    .groupBy { section(it.receivedAt, now) }
                for ((title, rows) in groups) {
                    item(key = "hdr-$title") { DateHeader(title) }
                    items(rows, key = { it.id }) { h ->
                        ContactRow(h, timeLabel(h.receivedAt, now),
                                   selecting = selecting, selected = selected(h),
                                   onClick = { onClick(h) }, onLongClick = { onLongClick(h) })
                    }
                }
            }
            Prefs.Sort.AZ -> {
                val groups = contacts
                    .sortedBy { it.displayName.lowercase() }
                    .groupBy { azHeader(it.displayName) }
                    .toSortedMap(compareBy({ it == "#" }, { it }))
                for ((letter, rows) in groups) {
                    item(key = "hdr-$letter") { DateHeader(letter) }
                    items(rows, key = { it.id }) { h ->
                        ContactRow(h, timeLabel(h.receivedAt, now, sectioned = false),
                                   selecting = selecting, selected = selected(h),
                                   onClick = { onClick(h) }, onLongClick = { onLongClick(h) })
                    }
                }
            }
        }
    }
}

private fun section(at: Long, now: Long): String = when (daysBetween(at, now)) {
    0 -> "Today"
    1 -> "Yesterday"
    else -> "Earlier"
}

/** The initial letter, uppercased; anything that doesn't start with one groups under "#". */
private fun azHeader(name: String): String {
    val c = name.trim().firstOrNull()?.uppercaseChar()
    return if (c != null && c in 'A'..'Z') c.toString() else "#"
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ContactRow(
    h: Handshake,
    time: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    highlight: String = "",
    selecting: Boolean = false,
    selected: Boolean = false,
    onLongClick: (() -> Unit)? = null,
) {
    Row(
        modifier
            .fillMaxWidth()
            .then(
                if (selected) Modifier.background(MaterialTheme.colorScheme.secondaryContainer)
                else Modifier
            )
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
            .height(72.dp)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (selecting) Checkbox(checked = selected, onCheckedChange = null)
        else Avatar(h.displayName)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(highlighted(h.displayName, highlight), style = MaterialTheme.typography.bodyLarge,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(highlighted(secondaryLine(h), highlight), style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Text(time, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** The matched run in the primary colour, as the Search artboard draws it. */
@Composable
private fun highlighted(text: String, query: String): AnnotatedString {
    val q = query.trim()
    val at = if (q.isEmpty()) -1 else text.indexOf(q, ignoreCase = true)
    if (at < 0) return AnnotatedString(text)
    return buildAnnotatedString {
        append(text.substring(0, at))
        withStyle(SpanStyle(color = MaterialTheme.colorScheme.primary,
                            fontWeight = FontWeight.Medium)) {
            append(text.substring(at, at + q.length))
        }
        append(text.substring(at + q.length))
    }
}

/** "Org · reach-them-by", the two facts a list line has room for. */
fun secondaryLine(h: Handshake): String =
    listOfNotNull(h.org, h.phones.firstOrNull()?.number ?: h.email).joinToString(" · ")

// No "set your card" nudge here: the status line's red slashed card is
// the whole signal, and the Band page is where the card gets set.
@Composable
private fun EmptyState() {
    Column(
        Modifier.fillMaxSize().padding(horizontal = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.weight(1f))
        Icon(HandoffIcons.Band, contentDescription = null,
             tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(56.dp))
        Spacer(Modifier.height(20.dp))
        Text("No handshakes yet", style = MaterialTheme.typography.titleLarge,
             textAlign = TextAlign.Center)
        Spacer(Modifier.height(8.dp))
        Text("Shake hands to exchange contact cards.",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        Spacer(Modifier.weight(1f))
    }
}

// ---- time ------------------------------------------------------------

/**
 * Time of day for today, "8 Sep" for anything older. Yesterday is a time
 * under a "Yesterday" header and the word itself where there is no header.
 */
fun timeLabel(at: Long, now: Long = System.currentTimeMillis(), sectioned: Boolean = true): String =
    when (daysBetween(at, now)) {
        0 -> DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(at))
        1 -> if (sectioned) DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(at))
             else "Yesterday"
        else -> SimpleDateFormat("d MMM", Locale.getDefault()).format(Date(at))
    }

/** Whole calendar days from [at] to [now], in the device's zone. */
fun daysBetween(at: Long, now: Long): Int {
    fun dayStart(t: Long): Long = Calendar.getInstance().apply {
        timeInMillis = t
        set(Calendar.HOUR_OF_DAY, 0); set(Calendar.MINUTE, 0)
        set(Calendar.SECOND, 0); set(Calendar.MILLISECOND, 0)
    }.timeInMillis
    return ((dayStart(now) - dayStart(at)) / 86_400_000L).toInt()
}
