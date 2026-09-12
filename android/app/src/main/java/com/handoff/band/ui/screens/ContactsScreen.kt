package com.handoff.band.ui.screens

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
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
import com.handoff.band.ui.components.Avatar
import com.handoff.band.ui.components.BandStatusLine
import com.handoff.band.ui.components.DateHeader
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.theme.semantic
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
    cardSet: Boolean,
    sort: Prefs.Sort,
    onSort: (Prefs.Sort) -> Unit,
    onContact: (Handshake) -> Unit,
    onBand: () -> Unit,
    onSetUpCard: () -> Unit,
    onSettings: () -> Unit,
) {
    var searching by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }

    BackHandler(enabled = searching) { searching = false; query = "" }

    Scaffold(
        topBar = {
            if (searching) {
                SearchBar(query, onQuery = { query = it },
                          onClose = { searching = false; query = "" })
            } else {
                TopAppBar(
                    title = { Text("Handoff") },
                    actions = {
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
                                   highlight = query, onClick = { onContact(h) })
                    }
                }
                return@Column
            }

            BandStatusLine(band, onClick = onBand)

            // The nudge sits above the list whenever no card is set, not only
            // while the list is empty: someone can collect a dozen cards
            // before realising they never set their own (§7).
            if (!cardSet && contacts.isNotEmpty()) {
                CardNudge(onSetUpCard, Modifier.padding(top = 12.dp))
            }

            if (contacts.isEmpty()) {
                EmptyState(cardSet, onSetUpCard)
            } else {
                ContactList(contacts, sort, onContact)
            }
        }
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
private fun ContactList(contacts: List<Handshake>, sort: Prefs.Sort, onContact: (Handshake) -> Unit) {
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
                        ContactRow(h, timeLabel(h.receivedAt, now), onClick = { onContact(h) })
                    }
                }
            }
            Prefs.Sort.AZ -> {
                items(contacts.sortedBy { it.displayName.lowercase() }, key = { it.id }) { h ->
                    ContactRow(h, timeLabel(h.receivedAt, now, sectioned = false),
                               onClick = { onContact(h) })
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

@Composable
fun ContactRow(
    h: Handshake,
    time: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    highlight: String = "",
) {
    Row(
        modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .height(72.dp)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Avatar(h.displayName)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(highlighted(h.displayName, highlight), style = MaterialTheme.typography.bodyLarge,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(highlighted(secondaryLine(h), highlight), style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text(time, style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (h.promoted) {
                Icon(Icons.Filled.Check, contentDescription = "Saved to phone",
                     tint = MaterialTheme.semantic.ok, modifier = Modifier.size(15.dp))
            }
        }
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
    listOfNotNull(h.org, h.mobile ?: h.email).joinToString(" · ")

@Composable
private fun CardNudge(onSetUp: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.primaryContainer)
            .padding(start = 16.dp, end = 8.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Icon(HandoffIcons.CardOff, contentDescription = null,
             tint = MaterialTheme.colorScheme.onPrimaryContainer, modifier = Modifier.size(22.dp))
        Text("You haven't set your contact card",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onPrimaryContainer,
             modifier = Modifier.weight(1f))
        TextButton(onClick = onSetUp) { Text("Set up") }
    }
}

@Composable
private fun EmptyState(cardSet: Boolean, onSetUp: () -> Unit) {
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
        if (!cardSet) CardNudge(onSetUp, Modifier.padding(bottom = 24.dp))
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
