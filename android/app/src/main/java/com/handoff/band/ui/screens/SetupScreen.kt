package com.handoff.band.ui.screens

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.compose.foundation.text.KeyboardOptions
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.google.zxing.BarcodeFormat
import com.handoff.band.ble.BandCode
import com.handoff.band.ui.components.HandoffIcons
import com.handoff.band.ui.theme.semantic
import com.journeyapps.barcodescanner.BarcodeCallback
import com.journeyapps.barcodescanner.BarcodeResult
import com.journeyapps.barcodescanner.DecoratedBarcodeView
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
 * Step 2 offers two actions, not three: the editor handles pick-versus-type.
 */
@Composable
fun SetupScreen(
    pairedName: String?,
    onCode: (BandCode) -> Unit,
    onSetUpCard: () -> Unit,
    onSkip: () -> Unit,
) {
    Scaffold { padding ->
        if (pairedName == null) StepOne(onCode, Modifier.padding(padding))
        else StepTwo(pairedName, onSetUpCard, onSkip, Modifier.padding(padding))
    }
}

@Composable
private fun StepOne(onCode: (BandCode) -> Unit, modifier: Modifier) {
    var manual by remember { mutableStateOf(false) }
    val context = LocalContext.current
    var hasCamera by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED
        )
    }
    val requestCamera = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasCamera = granted }

    Column(
        modifier.fillMaxSize().padding(horizontal = 32.dp, vertical = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("Step 1 of 2", style = MaterialTheme.typography.labelMedium,
             color = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(12.dp))
        Icon(HandoffIcons.BandUnderside, contentDescription = null,
             tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(72.dp))
        Spacer(Modifier.height(12.dp))
        Text("Switch on and scan the QR code",
             style = MaterialTheme.typography.titleLarge, textAlign = TextAlign.Center)
        Spacer(Modifier.height(20.dp))

        if (hasCamera) {
            Viewfinder(
                onResult = { text -> BandCode.parse(text)?.let(onCode) },
                modifier = Modifier.weight(1f).fillMaxWidth().clip(RoundedCornerShape(24.dp)),
            )
        } else {
            Spacer(Modifier.weight(1f))
            Text("Camera access is needed to scan the code.",
                 style = MaterialTheme.typography.bodyMedium,
                 color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            Spacer(Modifier.height(16.dp))
            Button(onClick = { requestCamera.launch(Manifest.permission.CAMERA) }) {
                Text("Allow camera access")
            }
            Spacer(Modifier.weight(1f))
        }

        Spacer(Modifier.height(12.dp))
        TextButton(onClick = { manual = true }) { Text("Enter the band code instead") }
    }

    if (manual) {
        CodeDialog(onCode = { manual = false; onCode(it) }, onDismiss = { manual = false })
    }
}

/**
 * The live QR decode, embedded rather than handed off to zxing's own
 * full-screen activity. Paused/resumed with the lifecycle so the camera is
 * released whenever the app is backgrounded mid-scan.
 */
@Composable
private fun Viewfinder(onResult: (String) -> Unit, modifier: Modifier = Modifier) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val latestOnResult = rememberUpdatedState(onResult)
    var barcodeView by remember { mutableStateOf<DecoratedBarcodeView?>(null) }

    AndroidView(
        factory = { ctx ->
            DecoratedBarcodeView(ctx).also { view ->
                view.barcodeView.decoderFactory = DefaultDecoderFactory(listOf(BarcodeFormat.QR_CODE))
                view.decodeContinuous(object : BarcodeCallback {
                    override fun barcodeResult(result: BarcodeResult) {
                        latestOnResult.value(result.text)
                    }
                })
                barcodeView = view
            }
        },
        modifier = modifier,
    )

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
    Column(
        modifier.fillMaxSize().padding(horizontal = 32.dp, vertical = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("Step 2 of 2", style = MaterialTheme.typography.labelMedium,
             color = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(20.dp))
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
        Text("The band hands it over when you shake hands.",
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        Spacer(Modifier.weight(1f))

        Button(onClick = onSetUpCard, modifier = Modifier.fillMaxWidth()) {
            Text("Set it up")
        }
        TextButton(onClick = onSkip) { Text("Skip for now") }
    }
}
