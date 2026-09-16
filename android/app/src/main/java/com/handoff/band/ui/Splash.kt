package com.handoff.band.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.handoff.band.R
import com.handoff.band.ui.components.HandoffIcons
import kotlinx.coroutines.delay

/**
 * The launch screen.
 *
 * Android 12 draws a splash of its own while the process starts, scales its
 * icon in, and offers no way to put a name under it. The wearer wanted
 * neither the hands alone nor that zoom, so the system splash
 * (Theme.Handoff.Starting) is the bare surface colour and this overlay is
 * the first thing with anything on it: the handshake with the wordmark
 * beneath, fading in together on the first frame, held long enough to be
 * read, then fading into the app already composed behind it.
 *
 * The handshake is the launcher icon's alpha mask tinted the primary — the
 * launcher foreground is a fixed navy that vanishes on the dark surface —
 * drawn at the size the system splash would have used (a 108 dp adaptive
 * layer at 288 dp), which puts the lower edge of the hands 40 dp below
 * centre.
 *
 * Shown only once per activity life: the flag survives rotation, so turning
 * the phone does not replay it.
 */
@Composable
fun SplashOverlay() {
    var done by rememberSaveable { mutableStateOf(false) }
    if (done) return
    // Two layers on purpose. The surface is opaque from the first frame —
    // the app is composed behind this, and a sheet that itself faded in
    // showed the contacts list for a few frames. Only the mark fades in.
    var covering by remember { mutableStateOf(true) }
    val mark = remember { MutableTransitionState(false) }
    LaunchedEffect(Unit) {
        mark.targetState = true
        delay(FADE_IN_MS + HOLD_MS)
        covering = false
        delay(FADE_OUT_MS)
        done = true
    }
    AnimatedVisibility(covering, exit = fadeOut(tween(FADE_OUT_MS.toInt()))) {
        Box(
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.surface)
        ) {
            AnimatedVisibility(mark, enter = fadeIn(tween(FADE_IN_MS.toInt()))) {
                Box(Modifier.fillMaxSize()) {
                    Icon(
                        painterResource(R.mipmap.ic_launcher_monochrome),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.align(Alignment.Center).size(ICON_BOX),
                    )
                    Icon(
                        HandoffIcons.Wordmark,
                        contentDescription = "Handoff",
                        tint = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier
                            .align(Alignment.Center)
                            .offset(y = WORDMARK_OFFSET)
                            .width(150.dp)
                            .height(28.dp),
                    )
                }
            }
        }
    }
}

private const val FADE_IN_MS = 250L
private const val HOLD_MS = 900L
private const val FADE_OUT_MS = 350L

private val ICON_BOX = 288.dp

/** Hands end 40 dp below centre; 20 dp of air; then half the wordmark height. */
private val WORDMARK_OFFSET = 74.dp
