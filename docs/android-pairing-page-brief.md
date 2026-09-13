# Brief: make the "Pair a band" page work, and make it good

Prompt for the next session. Read this whole file before touching code.
Branch `app/customer-facing`, working tree has uncommitted changes from
13 Sep (listed at the end). Phone: OnePlus CPH2569, Android 15, on adb.
Bench band: code **93D1**, address `2C:CF:67:D0:CA:7D`, firmware current
(advertises `Handoff band 93D1`, label content `93D1`). Build/flash notes
are in the auto-memory file `android-build-toolchain.md`.

The page fails on three counts and each has a known cause. Fix all three,
then review the page as a product screen, not as a bug list.

---

## 1. Functional: a failure produces no message. Ever.

The wearer has asked four times for a failure message and has never seen
one. The reason is in the code, not the phone:

- `Nav.kt` `pair()` passes `onFailure = { /* the chooser reports its own
  failure */ }` to `Pairing.associate`. The chooser does **not** report it:
  on discovery timeout CDM logs `Sending onFailure to app due to
  reason=discovery_timeout`, finishes with `resultCode=2`, and the app's
  `rememberLauncherForActivityResult` callback returns early on
  `resultCode != RESULT_OK`. Nothing on screen changes.
- Setup step 1 has no failure state at all. Its only feedback line was a
  "Read 93D1 — looking for the band…" text that is cleared on `ON_RESUME`,
  i.e. the instant the chooser closes — so it never survives long enough to
  be read, whether the chooser succeeded or failed.
- The 15 s "not found" snackbar in `BandService` only covers a *stored*
  band that will not connect; it cannot fire during first-run pairing.

Required: step 1 is a small state machine and every state is on screen:

    scanning  →  read (code shown)  →  looking for the band (chooser)
              →  connecting to Handoff band 93D1  →  connected  (→ step 2)
    any of those  →  failed, with a reason and a Retry

Copy the wearer asked for: **"Connecting to Handoff band 93D1"** once the
chooser has handed back a device. Failure copy must say what happened in
plain words — "Couldn't find Handoff band 93D1. Is it switched on and
close by?" for a discovery timeout; "Pairing was cancelled" for a
dismissed chooser or consent dialog; "Couldn't connect to Handoff band
93D1" if bonding or the GATT connect fails. `onFailure` must be wired, the
`RESULT_CANCELED` path must be handled, and `BandService`'s
`lastError`/`notFoundAt` must reach the setup page while it is showing.
The haptic tick on a successful read stays.

## 2. Functional: the OS chooser intermittently cannot find a band that is on the air

Evidence from the phone's Bluetooth dump (`adb shell dumpsys
bluetooth_manager`; note that in the scan history each `└
BluetoothLeScanFilter` line belongs to the entry **above** it — the
previous session misread this once):

| time  | filter (name + service UUID)   | result                      |
|-------|--------------------------------|-----------------------------|
| 20:50 | `Handoff band 93D1`            | found in 5 s → bonded 20:50:23 |
| 20:51 | same                           | found → bonded 20:51:11     |
| 21:07 | same                           | 20 s, **0 results**         |
| 21:29 | same                           | 20 s, **0 results**, `discovery_timeout` |
| 21:34 | same                           | 20 s, **0 results**, `discovery_timeout` |

The band was advertising `Handoff band 93D1` with the service UUID the whole
time — a PC-side `bleak` scan saw it at −35 dBm at 21:15 and 21:21 (script:
`scratchpad/blescan.py` from the previous session; trivial to recreate).
Both bonds above were removed by *Forget* in the app (`btif_dm_remove_bond`
at 20:50:37 and 20:52:29), so pairing and bonding themselves are fine.

Unknown: why the same filter stops matching after the band has been paired
and forgotten once. Candidates, in the order to test:

1. **The name lives only in the scan response** (`ble.c`: the 128-bit UUID
   fills the advertisement, the name goes in the scan response). If the
   stack offloads the `deviceName` filter to the controller and the
   controller matches on the advertising PDU only, the name never matches
   unless something else has already cached it. Test: run the app's
   Advanced → "Run a Bluetooth scan" (unfiltered; logs
   `name=<scan-record name>` under tag `HandoffScan`) and see whether the
   stack delivers the name at all; then try `associate()` with the
   **service UUID only** and no name and see whether the chooser finds it
   immediately.
2. The band is momentarily connected to something (a connected peripheral
   does not advertise). Check `dumpsys bluetooth_manager` ACL history for
   `ca:7d` and the Windows Bluetooth device list (the PC has a Bluetooth
   adapter).
3. Stale identity state on the phone after `removeBond` (the band still
   holds the phone's IRK/LTK from the earlier bond; BTstack accepts a
   fresh pairing from a bonded peer, verified in `sm.c`, so this should be
   irrelevant — but confirm with `adb logcat` around the connect).

Design the fix so it does not depend on the name filter being reliable:
e.g. filter the OS scan on the service UUID only and match the label's
four digits against the scan record name in the app (or run a short
`BluetoothLeScanner` scan of our own — `BLUETOOTH_SCAN` with
`neverForLocation`, no location permission needed on API 31+ — resolve
name → address, then `associate()` with `setDeviceAddress`, which the
controller matches reliably). Keep CDM: it is what gives the foreground
service its companion status and it persists across reinstalls. Keep
`setSingleDevice(true)`: at a conference there are many bands.

Capture logs while testing: `adb logcat -G 32M; adb logcat -c; adb logcat
-v time > pairing.log`, then grep `CDM_|bond_state|create_bond|remove_bond|
ca:7d|HandoffService|BandClient|CameraPreview`. The phone is
password-locked; you cannot drive its UI over adb — ask the wearer to
perform each attempt and describe the screen.

Also verify the decode itself: the wearer reported "no response during QR
scanning" in a session where `associate()` demonstrably ran at 21:29 and
21:34 — it is not known whether those codes came from the camera or from
"Enter the band code instead". Log the decoded text at the callback
(`SetupScreen.kt` Viewfinder) and confirm on the phone that the camera
path fires. The viewfinder is zxing-android-embedded's bare `BarcodeView`
(QR only, continuous decode, one code handed over per visit); this phone's
Camera-1 shim reports a 3168×3168 preview.

## 3. UI/UX: the page looks unfinished

The wearer's words: the heading "Switch on band, scan its QR" is "tucked at
the top, not good looking"; the bottom "Enter the band code instead" button
"feels off". The earlier tall viewfinder squashed the heading; the current
square one leaves the heading stranded above a large empty gap.

Design it properly, against the artboards in `design/android-redesign/`
(`Setup1.dc.html` is this page) and the decisions in
`docs/android-app-decisions.md` §2a. Things to get right:

- The heading, the viewfinder and the status line form one composition,
  vertically balanced, with the step label above. The status line is part
  of the layout from the start (reserve its height), so the page does not
  jump between states.
- Each state in §1 has a look, not just a string: scanning (idle line),
  read (code echoed, tick), looking/connecting (a progress indicator and
  "Connecting to Handoff band 93D1"), connected (step 2), failed (the
  reason in the error colour and a **Retry** that re-arms the scanner).
- "Enter the band code instead" is a secondary action and should read as
  one, placed where it does not compete with the viewfinder. Consider
  whether it belongs under the status line rather than pinned to the
  bottom edge.
- No red laser line, no framing rectangle, no "place a barcode" caption
  (already removed — keep it that way). A subtle corner-bracket guide on
  the viewfinder is fine if it helps the wearer aim; a full overlay is not.
- Check dark theme and a phone with a display cutout (this one has one).
- Home already opens on the contact list; setup is reached from the "Pair
  a band" pill and pops back to it. Do not change that.

## 4. Button geometry: keep the pill

The "Pair a band" button on the home screen must stay Material's default
pill shape — the same shape as the new full-width "Create contact" pill
that now heads the contact list. A previous change gave it the 16 dp
corner box of the paired status row; the wearer asked for that to be
reverted, and the working tree already has the revert in
`BandStatusLine.kt`. Verify on the phone that both pills match, and leave
the colour as it is.

---

## State of the working tree (uncommitted, 13 Sep)

- `SetupScreen.kt` — home-first navigation, heading copy, bare
  `BarcodeView`, square viewfinder, one code per visit, haptic tick, the
  (broken, see §1) status line.
- `Nav.kt` — home is always the start destination; setup pops back;
  `address`/`bandName` derived from `Pairing.version`.
- `BandService.kt` — watches `ACTION_BOND_STATE_CHANGED`; a bond removed
  outside the app (Bluetooth settings) is treated as *Forget*.
- `Pairing.kt` — `version` flow bumped by `remember`/`forget`.
- `BandCode.kt` + test — label content is the bare four digits
  (`93D1`, or `93D1:MAC`); the earlier `HANDOFF:` prefix is still read.
- `BandStatusLine.kt` — pill shape (the §4 revert).
- `ContactsScreen.kt`, `Icons.kt` — "Create contact" pill with a
  person-plus icon at the head of the list; FAB removed; `BandUnderside`
  icon deleted.
- `tools/band_label.py` — bare digits, EC level H, large digits under the
  code. `band-93D1.png` in the repo root is the current label (untracked;
  decide where it lives).
- `firmware/apps/handoff/handoff.c` — banner prints `label 93D1`.
  Already flashed to the bench band.
- `android/README.md` — first-run paragraph and banner example.

Review these with the wearer before committing; nothing here is committed.

---

## Outcome (13 Sep, late evening)

All three counts fixed and confirmed on the phone, scanning and typing both.

**§2, root cause — not the name filter as such.** The phone's Bluetooth
controller (Qualcomm, OnePlus CPH2569) has a handful of advertising-filter
(APCF) slots shared by every app; Play services and HeyTap Accessory fill them,
and once they are full the stack logs
`on_advertising_filter_complete: ... MEMORY_CAPACITY_EXCEEDED(0x07)` and runs
the scan against a filter that was never installed — 0 results for any app,
CDM's chooser included. Even an unfiltered scan needs one slot for its
all-pass parameter. A Bluetooth off/on (`adb shell cmd bluetooth_manager
disable` / `enable`) clears it; on this phone it refilled within ~4 minutes
(22:21, after four pairings in a row had worked). Fix: `Pairing.locate()` —
our own unfiltered 12 s scan, name matched in the app — then `associate()`
with a CDM **name pattern** instead of a `ScanFilter`, so nothing the app does
asks the controller for a filter. A scan that hears nothing at all is reported
as the phone's problem ("Bluetooth on this phone isn't finding anything…"),
distinct from a band that is off. Candidates 2 and 3 were ruled out (no ACL to
`ca:7d` between 20:52 and 21:56; re-bonding after Forget worked every time the
scan did).

**§1.** `PairStep` (`ui/PairStep.kt`) is the state machine, owned by
`Nav.kt`; every change is a snackbar, in-progress ones held until the state
changes, failures with *Try again* (repeats the same code; the viewfinder is
re-armed too). The chooser's `RESULT_CANCELED`, CDM's `onFailure`, the bond
refusal (`BandClient.ERR_BOND_REFUSED`) and the 15 s `notFoundAt` all reach
the page. Step 2 shows on `ready`, not on the chooser's OK.

**§3.** Laid out from `Setup1.dc.html`: centred step label in the top bar,
28 sp heading, 248 dp viewfinder with the corner brackets (dimmed under a
spinner while busy), *Enter the band code instead* under it. 16 dp snackbar
corners, to match the rows. The wearer's review: "both paired, toasts fine".

**Decode** confirmed from the camera (`HandoffSetup: decoded: 93D1`), typically
instant; the first read of the evening took ~40 s (focus), nothing in the app.

**Open:** a production label carrying the MAC (`93D1:MAC`) could skip the scan
entirely and pair with a self-managed CDM association plus a direct GATT
connect, which would sidestep the filter-slot problem altogether — not built.
`band-93D1.png` still needs a home. Nothing committed.
