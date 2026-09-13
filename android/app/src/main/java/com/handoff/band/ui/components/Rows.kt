package com.handoff.band.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/**
 * The list vocabulary every screen shares: a section header, a settings-style
 * row, and the initials avatar. Sizes are the artboards' — 72 dp rows, 40 dp
 * avatars, 16 dp side gutters.
 */

/** Small uppercase header in the primary colour, as on Settings. */
@Composable
fun SectionHeader(text: String, modifier: Modifier = Modifier) {
    Box(
        modifier.fillMaxWidth().height(40.dp).padding(horizontal = 16.dp),
        contentAlignment = Alignment.BottomStart,
    ) {
        Text(
            text.uppercase(),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(bottom = 8.dp),
        )
    }
}

/** The muted uppercase date header on the contact list ("Today"). */
@Composable
fun DateHeader(text: String, modifier: Modifier = Modifier) {
    Box(
        modifier.fillMaxWidth().height(32.dp).padding(horizontal = 16.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        Text(
            text.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/**
 * Icon, title, optional subtitle, optional trailing slot. A chevron when the
 * row navigates and nothing was put in the slot. [leading] takes the icon's
 * place for the rows whose glyph is drawn, not a vector — the battery — and
 * is centred in the same 24 dp cell so the titles line up.
 */
@Composable
fun SettingsRow(
    title: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    iconTint: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    subtitle: String? = null,
    onClick: (() -> Unit)? = null,
    chevron: Boolean = onClick != null,
    leading: (@Composable () -> Unit)? = null,
    trailing: (@Composable () -> Unit)? = null,
) {
    Row(
        modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .defaultMinSize(minHeight = 72.dp)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (leading != null) {
            Box(Modifier.size(24.dp), contentAlignment = Alignment.Center) { leading() }
        } else if (icon != null) {
            Icon(icon, contentDescription = null, tint = iconTint,
                 modifier = Modifier.size(24.dp))
        }
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (subtitle != null) {
                Text(subtitle, style = MaterialTheme.typography.bodyMedium,
                     color = MaterialTheme.colorScheme.onSurfaceVariant,
                     maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
        }
        trailing?.invoke()
        if (chevron) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
                 tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(24.dp))
        }
    }
}

/**
 * The top of a card page, a received contact's or your own: a large initials
 * avatar, the name, the role line ("Hardware lead · Handoff" — the one place
 * organisation and title appear), and [below] for the one line that differs
 * between the two pages.
 */
@Composable
fun PersonHeader(
    name: String,
    title: String?,
    org: String?,
    modifier: Modifier = Modifier,
    below: @Composable ColumnScope.() -> Unit = {},
) {
    Column(
        modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(72.dp).background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(initials(name), style = MaterialTheme.typography.titleLarge,
                 color = MaterialTheme.colorScheme.onPrimaryContainer)
        }
        Text(name, style = MaterialTheme.typography.titleLarge,
             textAlign = TextAlign.Center, modifier = Modifier.padding(top = 12.dp))
        val role = listOfNotNull(title?.takeIf { it.isNotBlank() }, org?.takeIf { it.isNotBlank() })
            .joinToString(" · ")
        if (role.isNotEmpty()) {
            Text(role, style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center,
                 modifier = Modifier.padding(top = 2.dp))
        }
        below()
    }
}

/**
 * A value over its label, the read-only counterpart of a text field, with the
 * artboards' glyph for the kind of value on the left.
 */
@Composable
fun DetailRow(icon: ImageVector, value: String, label: String) {
    Row(
        Modifier.fillMaxWidth().height(72.dp).padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant,
             modifier = Modifier.size(22.dp))
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(value, style = MaterialTheme.typography.bodyLarge)
            Text(label, style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** Two initials on the primary container. */
@Composable
fun Avatar(name: String, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(40.dp)
            .background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            initials(name),
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.onPrimaryContainer,
        )
    }
}

fun initials(name: String): String {
    val parts = name.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
    return when {
        parts.isEmpty() -> "?"
        parts.size == 1 -> parts[0].take(1).uppercase()
        else -> (parts.first().take(1) + parts.last().take(1)).uppercase()
    }
}
