package com.handoff.band.ui

import android.Manifest
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.lifecycleScope
import com.handoff.band.ble.BandService
import com.handoff.band.ble.Pairing
import com.handoff.band.data.Prefs
import com.handoff.band.ui.theme.HandoffTheme

/**
 * The one activity. It owns the service binding and the permission prompt;
 * every screen is a composable under [HandoffNavHost].
 */
class MainActivity : ComponentActivity() {

    private lateinit var band: BandConnection

    private val permissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* the Band screen reports what is still missing */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate, as the library requires. The system splash
        // is a blank surface (Theme.Handoff.Starting); dropping it the instant
        // our first frame exists lets SplashOverlay fade in on the same colour.
        installSplashScreen().setOnExitAnimationListener { it.remove() }
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        permissions.launch(requiredPermissions())

        if (Pairing.storedAddress(this) != null && !Prefs.get(this).bandOff.value) {
            BandService.start(this)
        }
        band = BandConnection(this, lifecycleScope).also { it.bind() }

        setContent {
            val theme by Prefs.get(this).theme.collectAsState()
            HandoffTheme(theme) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    Box {
                        CompositionLocalProvider(LocalBand provides band) {
                            HandoffNavHost()
                        }
                        SplashOverlay()
                    }
                }
            }
        }
    }

    /**
     * Only uiMode reaches here (manifest). enableEdgeToEdge picks the status
     * bar icon colour from the configuration at the time it is called, so
     * a theme change made in Settings re-evaluates it instead of waiting for
     * the next launch.
     */
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        enableEdgeToEdge()
    }

    override fun onDestroy() {
        band.unbind()
        super.onDestroy()
    }

    private fun requiredPermissions(): Array<String> = buildList {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
        }
    }.toTypedArray()
}
