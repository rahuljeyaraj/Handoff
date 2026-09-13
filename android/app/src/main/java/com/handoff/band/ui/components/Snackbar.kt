package com.handoff.band.ui.components

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/**
 * The one snackbar host every screen uses, so a message looks the same
 * wherever it appears. Material's 4 dp corner looked blocky next to the
 * app's 16 dp rows and its pills; this is the row's radius. Text stays
 * start-aligned with the action at the end, as Material lays a snackbar out.
 */
@Composable
fun HandoffSnackbarHost(state: SnackbarHostState, modifier: Modifier = Modifier) {
    SnackbarHost(state, modifier) { data -> Snackbar(data, shape = RoundedCornerShape(16.dp)) }
}
