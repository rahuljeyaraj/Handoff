# Android app — first hands-on review

Feedback from the first install on a phone (OnePlus CPH2569, Android 15),
13 Sep 2026, against the branch `app/customer-facing` at `442e2a6`. The band
was still on the version-1 firmware, so battery and firmware version were not
exercised.

Recorded before any code changes, in the order the feedback came. Each item
says what was seen, what it means in the code, and where a decision is still
open. Items marked **decide** need the user's word before building; the rest
are straightforward.

**Status: documented, not started.** Copy choices are in §Copy at the end.

---

## 1. Scanning should be the page, not a step off it

**Seen:** *Scan the code* opens ZXing's `CaptureActivity`, which is a
separate full-screen activity locked to landscape. Two problems: the extra
tap, and the rotation.

**Wanted:** the camera viewfinder is already on the setup page, in portrait,
and the user never leaves it. The square is the page.

**In code:** embed `zxing-android-embedded`'s `DecoratedBarcodeView` (or the
bare `BarcodeView`) in Compose through `AndroidView`, driven by
`decodeContinuous`, paused/resumed with the lifecycle. CAMERA has to be asked
for by us then — the library's activity no longer does it. Drop
`setOrientationLocked`; the activity is portrait anyway.

## 2. Step 2's copy

**Seen:** "Your band hands it over when you shake someone's hand."

**Wanted:** shorter — "band hands it over when you shake hands", grammar
checked. See §Copy A.

## 3. The band's display name

**Seen:** "Handoff 7A3C" (once re-paired; the current pairing predates name
storage and shows "Handoff band").

**Decide:** keep "Handoff 7A3C", or show "Handoff 7A3C band" / "Handoff band
7A3C". The advertised BLE name is fixed by `ble.c` at "Handoff 7A3C" (a
13-byte buffer, and the scan filter matches on it); this is only about what
the app prints. See §Copy B.

## 4. Step 2 says the same thing twice

**Seen:** the heading "Set up your contact card" and the button "Set up my
contact card" on one screen — the copy-discipline rule (decisions §1) broken
on our own setup page.

**Fix:** one of them changes. See §Copy C.

## 5. Phone number formatting

**Seen:** on Your contact card, a number filled from a phone contact carries
whatever spacing the contact had — "+91 98860 41225" in one, "9886041225" in
another.

**Wanted:** uniform, standard formatting: country code, then the national
grouping ("+91 98860 41225" for India).

**In code:** `android.telephony.PhoneNumberUtils.formatNumber(number,
countryIso)` does the standard grouping per country with the platform's
libphonenumber; the SIM or locale country is the default region. Apply on
prefill and on save in the card editor and the contact editor, and to the
display of received numbers. The vCard keeps what the band sends; formatting
is display and editing only. Keys are digits-only already, so dedup is
unaffected.

## 6. Card editor: bin icon and a clear-all

**Seen:** "Remove my contact card" has no icon; "Delete this contact" on the
contact editor has none either.

**Wanted:** a bin icon on both, and a button to clear every field on the card
editor. See §Copy D for its label.

## 7. Contacts: multi-select instead of long-press-to-merge

**Seen:** long press opens "Merge into…". That is not the behaviour wanted.

**Wanted:** long press enters selection mode — checkboxes on every row, a
count in the app bar — with batch **delete** and batch **save to phone
contacts**. Merge stays reachable from the detail screen's possible-duplicate
banner; with exactly two rows selected it can also be a selection action.

**Decide — batch save to phone:** the current save path is the system
editor's Insert intent, one contact per screen, with no `WRITE_CONTACTS`
(decisions §4, brief "Do not"). Batch-saving N contacts through it means N
editors in a row. A true batch needs `WRITE_CONTACTS` and a provider insert.
Options: (a) N editors in sequence, no permission; (b) request
`WRITE_CONTACTS` and insert directly, which also unlocks item 10. The
decision in §10 settles this.

## 8. Contacts: add one by hand

**Wanted:** a "+" to create a contact manually, as a contacts app does.

**In code:** a FAB on the home screen opening the existing contact editor on
an empty row; on save, insert with `vcard` built from the fields (`VCard.build`)
so the "card as received" view stays honest ("entered by hand" rather than a
band card). Received-vs-manual is worth a column (`source`) so the detail
screen can say "Added by hand" where it would say "Met today, 14:32".

## 9. A–Z needs letter headers

**Seen:** newest-first groups under Today / Yesterday / Earlier; A–Z is a flat
list.

**Wanted:** A–Z grouped under the initial letter, like a contacts app. Non-
letters under "#".

## 10. "Save to phone automatically" is disabled

**Seen:** the Settings switch is drawn but disabled.

**Why:** it is deliberately unbuilt — it needs `WRITE_CONTACTS` and a provider
insert, and the decisions doc and the brief both say not to add that
permission (§4, "Do not add WRITE_CONTACTS").

**Decide:** build it (add `WRITE_CONTACTS`, requested only when the switch is
turned on, plus a provider insert — which also gives item 7 its batch save),
or remove the row so nothing on the screen looks broken. Building it reverses
a recorded decision, so it is the user's call, not the implementer's.

## 11. Settings: fewer rows, a vibration switch

**Wanted:** remove *Your contact card* and *Band* from Settings — the band is
one tap from home and the card is one tap from the Band screen. Add a
vibration on/off switch.

**Note:** that removes one of the four routes to the card editor listed in
decisions §7 (Settings → Your card); three remain (nudge banner, setup step 2,
Band screen row).

**Decide — which vibration:** (a) the *phone's* buzz on the handshake
notification, which is a notification-channel setting and trivially ours; or
(b) the *band's* haptic motor (GP28), which needs a new control opcode and a
firmware setting the band persists. If it is (b), it is a firmware change and
a contract change (`ble.h` + `Gatt.kt` in one commit). See §Copy E for the
label.

## 12. No "No band paired" page

**Seen:** unpaired, the home status line says "No band paired · Not paired"
and tapping it opens a Band screen whose only content is a "Pair a band"
button.

**Wanted:** no such page. Unpaired, the status line's place holds a single
*Pair a band* button that goes straight to the pairing page. The Band screen's
unpaired state goes away (it is unreachable once the line is a button).

## 13. Setup step 1 heading is long; use a picture

**Seen:** "Switch on the band and scan its QR code".

**Wanted:** shorter, with part of the instruction carried by a diagram — the
band's underside with the label, and the switch. Ties into item 1: the page is
the viewfinder, with the picture and a short line above it. See §Copy F.

## 14. Forget did not unpair the band at the OS level

**Seen:** after *Forget this band*, the band was still listed as paired in the
phone's Bluetooth settings. The user removed it there by hand; after that the
band no longer appears in the Bluetooth window at all.

**In code:** `BandService.forget` removes the stored address, the
CompanionDeviceManager association and the service, and deliberately leaves
the OS bond ("the next pairing with the same band reuses it"). That is not
what a user expects of "forget". `BluetoothDevice.removeBond()` is a hidden
API but callable by reflection and works on this handset's Android; use it,
and fall back to a snackbar pointing at Bluetooth settings where it throws.

**Investigate on the bench:** why the band vanished from the Bluetooth
scanner once unbonded. The band still holds the old bond keys in BTstack's
flash bank; a phone that has dropped its half will try to pair afresh, and
whether BTstack accepts a re-pair from a known address with mismatched keys
needs checking (`sm_` config in `ble.c`; a `BLE_CTRL_FORGET`-style "forget
bonds" opcode may be needed). Also confirm whether the OS settings scanner
shows a BLE-only peripheral at all — it may only ever have shown the band
because it was bonded.

## 15. "Looking for the band" never gives up

**Seen:** with the band off, the status line says "Looking for the band"
indefinitely. When the reconnect attempts end there is no update saying the
band was not found.

**In code:** `autoConnect = true` keeps the controller trying with no
callback. Add a timer in the service: if not connected N seconds after a
connect or reconnect started, flip the state to "not found" and say so on
the status line (and the Band screen), leaving autoConnect running
underneath so it still picks the band up when it appears. See §Copy G.

## 16. "Saved to your phone" goes stale

**Seen:** save a contact to the phone from the app, delete it in the phone's
Contacts, and the app still shows the tick.

**In code:** the Insert intent returns the new contact's URI in its result;
store it (`contact_uri` column) and check it exists (`READ_CONTACTS`, already
held) when the detail screen opens and when the list is shown. One query per
open is cheap; a continuous observer is not needed. Noted for later, not
demo-critical.

## 17. Long press today merges

Duplicate of item 7 — the merge on long press is replaced by selection mode.

---

## Copy

Every line the user asked to change, with options. The rule from decisions
§1 still applies: nothing may restate what the screen already shows.

**A. Setup step 2, the line under the heading.** Was: "Your band hands it over
when you shake someone's hand."

1. "The band hands it over when you shake hands." — the user's line with the
   article it needs
2. "Handed over with a handshake."
3. "It crosses over in a handshake."
4. Drop the line — the heading and the button already say what the card is
   for

**B. The band's name in the app.** The radio says "Handoff 7A3C".

1. "Handoff 7A3C" — as advertised, as printed on the label, no suffix
2. "Handoff band 7A3C"
3. "Handoff 7A3C band"
4. "Band 7A3C" — "Handoff" is the app's own name and is already on screen

**C. Setup step 2, heading versus button.** Was: heading "Set up your contact
card", button "Set up my contact card".

1. Heading "Your contact card" · button "Set it up"
2. Heading "Set up your contact card" · button "Set up" / "Skip for now"
3. Heading "Add your contact card" · button "Add it now"
4. Heading "One more thing" · button "Set up my contact card"

**D. The clear-all button on the card editor.**

1. "Clear all fields"
2. "Clear"
3. "Start over"

**E. The vibration row in Settings.**

1. "Vibrate on a handshake"
2. "Vibration"
3. "Handshake buzz"

(If it turns out to be the band's motor rather than the phone: "Band
vibrates on a handshake".)

**F. Setup step 1 heading, over a diagram of the band's underside.** Was:
"Switch on the band and scan its QR code".

1. "Scan the code under the band" — the switch is in the picture
2. "Switch on, then scan"
3. "Scan the band's code"
4. "Point the camera at the band's label"

**G. When the band is not found (item 15).**

1. "Handoff 7A3C not found" — status line; the Band screen adds "Switch it
   on and keep the phone close."
2. "Can't find Handoff 7A3C"
3. "Band not found"

**H. The unpaired status line's button (item 12).**

1. "Pair a band"
2. "Pair your band"
3. "Set up a band"
