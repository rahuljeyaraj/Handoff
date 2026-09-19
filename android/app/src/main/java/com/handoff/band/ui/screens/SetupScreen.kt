package com.handoff.band.ui.screens

import android.Manifest
import android.content.pm.PackageManager
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.google.zxing.BarcodeFormat
import com.handoff.band.ble.BandCode
import com.handoff.band.ui.PairStep
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.components.HandoffSnackbarHost
import com.handoff.band.ui.theme.semantic
import com.journeyapps.barcodescanner.BarcodeCallback
import com.journeyapps.barcodescanner.BarcodeResult
import com.journeyapps.barcodescanner.BarcodeView
import com.journeyapps.barcodescanner.DefaultDecoderFactory

/**
 * First-run setup, design decisions §2a and §7: pair the band, then
 * optionally set your card.
 *
 * Step 1 is a scan, not a search. At a conference there are a hundred bands
 * in range and nothing tells the wearer which is theirs; the code on the
 * underside says exactly which one, and Android's unavoidable confirmation
 * then shows one device. A text button offers the code by hand for a label
 * that will not scan.
 *
 * The viewfinder is embedded on the page itself (review item 1) — no
 * separate landscape-locked activity and no extra tap. CAMERA is asked for
 * here, since zxing's own capture activity — which used to ask for it — is
 * no longer in the loop.
 *
 * Where the pairing has got to is [PairStep], owned by the nav host — the
 * chooser result and the service's state both land there. Every change of
 * it is a snackbar (pairing-page brief §1; the wearer asked for toasts):
 * "Looking for…" and "Connecting to…" stay up for as long as they are true,
 * a failure says what happened and offers "Try again", which repeats the
 * same code. The viewfinder is re-armed on failure too, so scanning the
 * label afresh is a retry as well. Scanned or typed, the code takes the
 * same path from there. Step 2 is the [PairStep.Connected] state.
 *
 * Step 2 offers two actions, not three: the editor handles pick-versus-type.
 */
@Composable
fun SetupScreen(
    step: PairStep,
    onCode: (BandCode) -> Unit,
    onSetUpCard: () -> Unit,
    onSkip: () -> Unit,
) {
    val snackbar = remember { SnackbarHostState() }
    // One snackbar per state. Changing state cancels this effect, and a
    // cancelled showSnackbar dismisses its snackbar, so the message on
    // screen is always the current one and never a stale one.
    LaunchedEffect(step) {
        when (step) {
            is PairStep.Looking -> snackbar.showSnackbar(
                "Looking for ${step.code.name}…", duration = SnackbarDuration.Indefinite)
            is PairStep.Connecting -> snackbar.showSnackbar(
                "Connecting to ${step.name}…", duration = SnackbarDuration.Indefinite)
            is PairStep.Failed -> {
                val r = snackbar.showSnackbar(
                    step.reason, actionLabel = "Try again", duration = SnackbarDuration.Long)
                if (r == SnackbarResult.ActionPerformed) onCode(step.code)
            }
            else -> {}
        }
    }

    Scaffold(snackbarHost = { HandoffSnackbarHost(snackbar) }) { padding ->
        when (step) {
            is PairStep.Connected -> StepTwo(step.name, onSetUpCard, onSkip, Modifier.padding(padding))
            else -> StepOne(step, snackbar, onCode, onSkip, Modifier.padding(padding))
        }
    }
}

/** The artboard's viewfinder: 248 dp square, 28 dp corners. */
private val ViewfinderSize = 248.dp
private val ViewfinderCorner = 28.dp

/**
 * The layout is the Setup1 artboard: the step label alone in the top bar,
 * then heading, viewfinder and the secondary action as one block centred
 * in what is left — the action directly under the viewfinder rather than
 * pinned to the bottom edge, where it read as unrelated to the scan. State
 * is told by snackbar and by the viewfinder itself (dimmed, with a spinner,
 * while the pairing is busy), so nothing in the block ever moves.
 */
@Composable
private fun StepOne(
    step: PairStep,
    snackbar: SnackbarHostState,
    onCode: (BandCode) -> Unit,
    onSkip: () -> Unit,
    modifier: Modifier,
) {
    var manual by remember { mutableStateOf(false) }
    var wrongLabelAt by remember { mutableStateOf<Long?>(null) }
    val context = LocalContext.current
    val haptics = LocalHapticFeedback.current

    // A QR that decoded but is not a band label would otherwise be
    // indistinguishable from the scanner not working at all.
    LaunchedEffect(wrongLabelAt) {
        if (wrongLabelAt != null) snackbar.showSnackbar("That QR is not a Handoff band label.")
    }

    var hasCamera by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED
        )
    }
    val requestCamera = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasCamera = granted }

    val busy = step is PairStep.Looking || step is PairStep.Connecting

    Column(modifier.fillMaxSize()) {
        StepLabel("Step 1 of 2")

        Column(
            Modifier.weight(1f).fillMaxWidth().padding(horizontal = 32.dp, vertical = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Text("Switch on the band and scan its QR code",
                 style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
            Spacer(Modifier.height(32.dp))

            if (hasCamera) {
                // One code at a time. The decode runs on every frame, and a
                // label held in view produced a fresh associate() each time
                // — every one of which cancelled the OS scan the previous
                // one had started, so the chooser never got its twenty
                // seconds. Armed again once the attempt has ended.
                val armed = !busy
                Viewfinder(
                    busy = busy,
                    onResult = { text ->
                        Log.i("HandoffSetup", "decoded: $text (armed=$armed)")
                        if (!armed) return@Viewfinder
                        val code = BandCode.parse(text)
                        if (code != null) {
                            // A tick, so a read that takes a moment to act on
                            // is not mistaken for a scanner that has not seen
                            // the label.
                            haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                            onCode(code)
                        } else wrongLabelAt = System.currentTimeMillis()
                    },
                    modifier = Modifier.size(ViewfinderSize).clip(RoundedCornerShape(ViewfinderCorner)),
                )
            } else {
                CameraAsk(onAsk = { requestCamera.launch(Manifest.permission.CAMERA) })
            }

            Spacer(Modifier.height(16.dp))
            TextButton(onClick = { manual = true }, enabled = !busy) {
                Text("Enter the band code instead")
            }
        }

        // The band can come later — no band to hand, or one that will not
        // pair — through "Pair a band" on home. Where step 2 keeps its skip.
        TextButton(onClick = onSkip, enabled = !busy,
                   modifier = Modifier.align(Alignment.CenterHorizontally).padding(bottom = 20.dp)) {
            Text("Skip for now")
        }
    }

    if (manual) {
        CodeDialog(onCode = { manual = false; onCode(it) }, onDismiss = { manual = false })
    }
}

/** The artboard's top bar, with only the step in it. */
@Composable
private fun StepLabel(text: String) {
    Box(Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 16.dp),
        contentAlignment = Alignment.Center) {
        Text(text.uppercase(), style = MaterialTheme.typography.labelMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** Holds the viewfinder's place, so the page does not reflow when access is granted. */
@Composable
private fun CameraAsk(onAsk: () -> Unit) {
    Column(
        Modifier.size(ViewfinderSize)
            .clip(RoundedCornerShape(ViewfinderCorner))
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text("Camera access is needed to scan the code.",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        Spacer(Modifier.height(16.dp))
        Button(onClick = onAsk) { Text("Allow camera access") }
    }
}

/**
 * The live QR decode, embedded rather than handed off to zxing's own
 * full-screen activity. Paused/resumed with the lifecycle so the camera is
 * released whenever the app is backgrounded mid-scan.
 *
 * The bare [BarcodeView], not zxing's decorated one: that adds a 1-D laser
 * line, a framing overlay and a "place a barcode inside the viewfinder"
 * caption, none of which belong on a QR scanner. Only QR is decoded. The
 * only guide drawn is the artboard's four corner brackets; [busy] dims the
 * picture under a spinner while a read is being acted on.
 */
@Composable
private fun Viewfinder(busy: Boolean, onResult: (String) -> Unit, modifier: Modifier = Modifier) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val latestOnResult = rememberUpdatedState(onResult)
    var barcodeView by remember { mutableStateOf<BarcodeView?>(null) }

    Box(modifier) {
        AndroidView(
            factory = { ctx ->
                BarcodeView(ctx).also { view ->
                    view.decoderFactory = DefaultDecoderFactory(listOf(BarcodeFormat.QR_CODE))
                    view.decodeContinuous(object : BarcodeCallback {
                        override fun barcodeResult(result: BarcodeResult) {
                            latestOnResult.value(result.text)
                        }
                    })
                    barcodeView = view
                }
            },
            modifier = Modifier.fillMaxSize(),
        )
        CornerBrackets(Modifier.fillMaxSize())
        if (busy) {
            Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.55f)),
                contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = BracketColor, strokeWidth = 3.dp)
            }
        }
    }

    DisposableEffect(lifecycleOwner, barcodeView) {
        val view = barcodeView
        if (view == null) return@DisposableEffect onDispose {}
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> view.resume()
                Lifecycle.Event.ON_PAUSE -> view.pause()
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            view.pause()
        }
    }
}

/**
 * Four L-shaped brackets, 16 dp in from the edge, 40 dp long, 3 dp thick,
 * 12 dp rounded — the artboard's, in its fixed pale indigo. Not a theme
 * colour: they sit over camera footage, not over the surface, and the dark
 * theme's container indigo would vanish against it.
 */
private val BracketColor = Color(0xFFDDE1FF)

@Composable
private fun CornerBrackets(modifier: Modifier = Modifier) {
    val color = BracketColor
    Canvas(modifier) {
        val inset = 16.dp.toPx()
        val len = 40.dp.toPx()
        val stroke = 3.dp.toPx()
        val r = 12.dp.toPx()
        val w = size.width
        val h = size.height
        val style = Stroke(width = stroke, cap = StrokeCap.Round)

        // Each bracket is the corner arc of a rounded rectangle with the two
        // straight runs extended to `len`.
        fun bracket(cx: Float, cy: Float, sx: Float, sy: Float) {
            val path = Path().apply {
                moveTo(cx, cy + sy * len)
                lineTo(cx, cy + sy * r)
                quadraticTo(cx, cy, cx + sx * r, cy)
                lineTo(cx + sx * len, cy)
            }
            drawPath(path, color, style = style)
        }
        bracket(inset, inset, 1f, 1f)
        bracket(w - inset, inset, -1f, 1f)
        bracket(inset, h - inset, 1f, -1f)
        bracket(w - inset, h - inset, -1f, -1f)
    }
}

@Composable
private fun CodeDialog(onCode: (BandCode) -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf("") }
    val code = BandCode.parse(text)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Band code") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("The four characters under the QR code.",
                     style = MaterialTheme.typography.bodyMedium,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
                OutlinedTextField(
                    text, { text = it.uppercase() }, singleLine = true,
                    placeholder = { Text("7A3C") },
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(enabled = code != null, onClick = { onCode(code!!) }) { Text("Pair") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun StepTwo(name: String, onSetUpCard: () -> Unit, onSkip: () -> Unit, modifier: Modifier) {
    Column(modifier.fillMaxSize()) {
        StepLabel("Step 2 of 2")
        Column(
            Modifier.weight(1f).fillMaxWidth().padding(horizontal = 32.dp, vertical = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Filled.Check, contentDescription = null,
                     tint = MaterialTheme.semantic.ok, modifier = Modifier.size(20.dp))
                Text("$name paired", style = MaterialTheme.typography.bodyLarge,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            Spacer(Modifier.weight(1f))
            Icon(HandoffIcons.Card, contentDescription = null,
                 tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(72.dp))
            Spacer(Modifier.height(20.dp))
            Text("Your contact card", style = MaterialTheme.typography.titleLarge,
                 textAlign = TextAlign.Center)
            Spacer(Modifier.height(8.dp))
            Text("Handed over when you shake hands.",
                 style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            Spacer(Modifier.weight(1f))

            Button(onClick = onSetUpCard, modifier = Modifier.fillMaxWidth()) {
                Text("Set it up")
            }
            TextButton(onClick = onSkip) { Text("Skip for now") }
        }
    }
}
