# Android app — first hands-on review

Feedback from the first install on a phone (OnePlus CPH2569, Android 15),
13 Sep 2026, against the branch `app/customer-facing` at `442e2a6`. The band
was still on the version-1 firmware, so battery and firmware version were not
exercised.

Recorded before any code changes, in the order the feedback came. Each item
says what was seen, what it means in the code, and where a decision is still
open. Items marked **decide** need the user's word before building; the rest
are straightforward.

**Status: documented and decided, not started.** The user's decisions are
recorded under each item as **Decided:**; copy choices are in §Copy at the end,
with the chosen line marked. The questions from the second pass and their
answers are in §Still open, now closed; the one thing that grew out of them
into a firmware feature is in §Carried forward.

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

**Decided (third pass):** "Handoff band 7A3C" — everywhere, *including the
advertised Bluetooth name*, which keeps the product name where the OS shows
it. 17 characters: `ble.c`'s name buffer grows from 13 to 18, and the scan
response from 14 to 19 bytes, well inside the 31-byte limit. That makes it a firmware change (`ble.c`'s `s_name`, the banner line)
and a contract change with `BandCode.name`, which the scan filter matches on:
one commit, both sides, as with the status struct. The QR payload's
`HANDOFF:` prefix is machine-read only and can stay.

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

**Decided:** no clear-all. One *Delete my contact card* button with a bin
icon, which erases the card on the band (the existing sync does that when the
local card goes) and clears the fields. Whether the page then closes or stays
open is still open — see §Still open.

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

**Decided (third pass):** no batch save at all — keep it simple. Selection
mode offers batch **delete** only; saving to the phone stays a single-contact
action through the system editor, and `WRITE_CONTACTS` is *not* added after
all. The brief's rule stands.

## 8. Contacts: add one by hand

**Wanted:** a "+" to create a contact manually, as a contacts app does.

**In code:** a FAB on the home screen opening the existing contact editor on
an empty row; on save, insert with `vcard` built from the fields (`VCard.build`)
so the "card as received" view stays honest ("entered by hand" rather than a
band card). Received-vs-manual is worth a column (`source`) so the detail
screen can say "Added by hand" where it would say "Met today, 14:32".

**Decided:** no "card as received" view on the detail screen at all — it is a
developer's view, and the brief's "do not remove the raw-vCard view" refers
to the *own card* bytes under Advanced, which stay. Nothing on screen ever
says "entered by hand"; a manual contact simply has no band card behind it.
What replaces the "Met today, 14:32" line for one is in §Still open.

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

**Decided:** remove the feature completely.

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

**Decided:** (b), the band's motor only. The band vibrates when a card is
successfully shared and when one is received, with a different pattern for
each event; the switch turns that on or off. Phone-side, a new contact is
already a notification, and the phone's own tone/vibrate settings govern it —
nothing to build there. Settings therefore keeps: Sort order · Vibrate ·
Theme · App version · Advanced. How the setting reaches the band is in §Still
open.

## 12. No "No band paired" page

**Seen:** unpaired, the home status line says "No band paired · Not paired"
and tapping it opens a Band screen whose only content is a "Pair a band"
button.

**Wanted:** no such page. Unpaired, the status line's place holds a single
*Pair a band* button that goes straight to the pairing page. The Band screen's
unpaired state goes away (it is unreachable once the line is a button).

**Decided:** the unpaired Band page is deleted, not merely unreachable. The
Band screen exists only for a paired band.

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

**Decided:** show the failure after the attempt ends — 15 seconds, and it
must be seen: the status line's state, a snackbar on whichever screen is open,
and the foreground-service notification's text, so it reaches a pocketed phone
too. (Note for the
implementer: with `autoConnect = true` the controller does not actually stop
trying — what the user sees as "giving up" is the line staying at "Looking for
the band" forever. The fix is the same either way: a timer, and a "Band 7A3C
not found" state that clears itself the moment the link comes up.)

## 16. "Saved to your phone" goes stale

**Seen:** save a contact to the phone from the app, delete it in the phone's
Contacts, and the app still shows the tick.

**In code:** the Insert intent returns the new contact's URI in its result;
store it (`contact_uri` column) and check it exists (`READ_CONTACTS`, already
held) when the detail screen opens and when the list is shown. One query per
open is cheap; a continuous observer is not needed. Noted for later, not
demo-critical.

**Decided:** the detail-screen check is fine, but the *home list's* tick has
no trigger to refresh, so either the tick goes or it gets an efficient
refresh. Recommendation in §Still open.

## 17. Long press today merges

Duplicate of item 7 — the merge on long press is replaced by selection mode.

---

## Copy

Every line the user asked to change, with options. The rule from decisions
§1 still applies: nothing may restate what the screen already shows.

**A. Setup step 2, the line under the heading.** Was: "Your band hands it over
when you shake someone's hand."

1. **Chosen:** "The band hands it over when you shake hands." — the user's line
   with the article it needs
2. "Handed over with a handshake."
3. "It crosses over in a handshake."
4. Drop the line — the heading and the button already say what the card is
   for

**B. The band's name in the app.** The radio says "Handoff 7A3C".

1. "Handoff 7A3C" — as advertised, as printed on the label, no suffix
2. "Handoff band 7A3C"
3. "Handoff 7A3C band"
4. "Band 7A3C" — "Handoff" is the app's own name and is already on screen

**Chosen (third pass):** "Handoff band 7A3C" — option 2 — in the app and on
the radio (item 3).

**C. Setup step 2, heading versus button.** Was: heading "Set up your contact
card", button "Set up my contact card".

1. **Chosen:** Heading "Your contact card" · button "Set it up" · "Skip for now"
2. Heading "Set up your contact card" · button "Set up" / "Skip for now"
3. Heading "Add your contact card" · button "Add it now"
4. Heading "One more thing" · button "Set up my contact card"

**D. The clear-all button on the card editor.** Withdrawn — there is no
clear-all (item 6). The one button is "Delete my contact card", with a bin.

**E. The vibration row in Settings.** **Chosen:** a vibrate icon and the word
"Vibrate", a switch on the right. It is the band's motor (item 11).

**F. Setup step 1 heading, over a diagram of the band's underside.** Was:
"Switch on the band and scan its QR code".

1. "Scan the code under the band" — the switch is in the picture
2. "Switch on, then scan"
3. "Scan the band's code"
4. "Point the camera at the band's label"

**Chosen:** "Switch on and scan the QR code".

**G. When the band is not found (item 15).**

1. **Chosen, renamed per B:** "Handoff band 7A3C not found" — status line;
   the Band screen adds "Switch it on and keep the phone close."
2. "Can't find Handoff 7A3C"
3. "Band not found"

**H. The unpaired status line's button (item 12).**

1. "Pair a band"
2. "Pair your band"
3. "Set up a band"

---

## Still open — now closed

Questions that survived the second pass, each with the recommendation made
and the answer given (third pass, 13 Sep).

**O1. The advertised name.** "Band 7A3C" is what the phone's Bluetooth
settings and the OS pairing dialog will show, outside the app where nothing
says "Handoff". Next to a "Mi Smart Band 7" in the same list it is anonymous.
Recommendation: advertise "Handoff 7A3C" and display "Band 7A3C" inside the
app. The user asked for the Bluetooth name to change too; needs one
confirmation before the firmware moves.

**Answer:** "Handoff band 7A3C" everywhere, radio included. Item 3 and Copy B updated.

**O2. After "Delete my contact card": stay or leave?** Recommendation: leave.
After the delete, the editor is an empty form, and staying on it reads as if
nothing happened; the screen underneath shows the new state on its own (the
nudge banner comes back, the Band row says "Not set"), and every route to the
editor is one tap away.

**Answer:** leave.

**O3. Which account batch-saved contacts go into.** A provider insert has to
name an account; the system editor used to ask. Inserting with no account
makes "phone-only" contacts that some OEMs hide from sync. Recommendation:
ask once, the first time, and remember it as a Settings row ("Save contacts
to · Google · x@gmail.com"), with the phone's default as the preselected
choice.

**Answer:** moot — batch save is withdrawn (item 7). Selection mode deletes only.

**O4. The "Met today, 14:32" line on a contact added by hand.** Recommendation:
"Added today, 14:32" — same shape, says nothing about how. Needs a `source`
column so the two are told apart.

**Answer:** "Added today, 14:32", with a `source` column.

**O5. How the vibrate setting reaches the band.** Two ways: (a) the band
persists it — a format change to the flash record store, or a second settings
sector; (b) the phone owns it and re-sends it on every connect, exactly as it
re-sends the card, with the band defaulting to on at boot. Recommendation:
(b) — no flash format change, no new status field, and a band that reboots
is back in step within a second of reconnecting. Either way it is a new
control opcode (`BLE_CTRL_HAPTIC`, on/off) and a firmware+app commit. Note
the events that would trigger the buzz — a real shared or received card —
do not exist until M12's body link; for the demo, the fake-card path can
buzz, and that is what proves the opcode.

**Answer (confirmed):** the band persists it in flash, like the card. And a wider point: a handshake received while the phone is out of reach must be persisted on the band until the phone is back. Both are firmware work — see §Carried forward.

**O6. The home list's "saved" tick.** Recommendation: keep it, refreshed by
one query when the list appears — `Contacts._ID IN (…)` over the stored
contact ids, a single cursor however many rows — plus a `ContentObserver` on
Contacts only while the app is in the foreground. That is negligible load and
also feeds the detail screen. If that still feels like too much machinery,
drop the tick.

**Answer (final):** (b) — no marker in the list. The detail screen keeps "Saved to your phone" with the one-query refresh. See §Carried forward, "the tick", for why.

**O7. Step 1 heading grammar.** "Switch on and scan QR code" is missing an
article. Recommendation: "Switch on and scan the QR code".

**Answer:** "Switch on and scan the QR code".

**O8. The not-found timeout.** How long before "Band 7A3C not found" — 30 s
recommended: long enough for a band that is being switched on, short enough
that the answer arrives before anyone reaches for Settings.

**Answer:** 15 seconds, and the failure must be shown, not just a state change (item 15).

---

## Carried forward

**The band must hold what the phone is not there to receive.** Two things
the vibrate question turned up, both firmware, both persisted in flash the
way the card already is:

1. **The vibrate setting lives on the band.** A settings field alongside the
   record in the store (`store.c` / `flash.c`; a format bump), set by a new
   control opcode (`BLE_CTRL_HAPTIC`, on/off) and reported back in `status`
   as a flag bit — the flags byte has room (`0x20`) even though the struct
   itself is full at 20 bytes — so the switch in the app shows the band's
   truth, not the phone's memory. The band defaults to on.

2. **Received handshakes are queued on the band until the phone is back.**
   *Future improvement only — not planned for this round or the next;
   recorded so the idea and its sizing are not lost.* A card that arrives
   while the phone is disconnected must not be lost. That
   is a second flash area — a queue of compact records, drained over
   `rx_vcard` on connect, each dropped only when the phone acknowledges it
   (a new `BLE_CTRL_ACK_RX` opcode with the record's index). Sizing: a compact
   card is 100–200 bytes, so one 4 KB sector holds 16–32 handshakes, which is
   a conference day; the flash bank map in `flash.c` has to be re-checked
   against the BTstack bond bank before a sector is claimed. The app already
   copes with replays — dedup by key makes a re-delivered card a merge, not a
   duplicate. No real receive path exists until M12, but the fake-card path
   can exercise the queue now: arm a fake card while disconnected, connect,
   and it arrives — which is a demo in itself. Not scheduled; a future
   improvement to weigh when M12 is planned.

**The tick.** The user asks whether a bare green tick beside a time reads as
"saved to your phone". Honest answer: no — a tick alone says "done" or
"verified" as easily as "saved", and green is reserved by decisions §12 for
connection and battery, so this tick is also off-palette. Two ways out, for
the next pass to pick: (a) a neutral grey *phone-with-check* glyph, which at
least says "in your phone"; (b) no marker in the list at all — the line
already carries name, organisation, contact and time, and "is it in my phone"
is a question asked on the detail screen, which keeps its "Saved to your
phone" line and gets the one-query refresh. Recommendation: (b).
**Decided:** (b).

---

## Round 2 — the agreed plan

Agreed 13 Sep 2026, to be built in a fresh session. **Nothing below has been
started; the working tree is clean at this commit.** Commit boundaries, in
order; each builds on its own.

App only:

1. **Home without a Band page for the unpaired case.** The status line
   becomes a *Pair a band* button when unpaired; the unpaired Band screen is
   deleted; Settings loses *Your card*, *Band* and the auto-save row and
   gains a *Vibrate* row (switch wired in commit 8). Items 10, 11, 12.
2. **Setup is the viewfinder.** Portrait, an embedded `BarcodeView` on the
   page itself, CAMERA asked for by us; "Switch on and scan the QR code" over
   a diagram of the band's underside; step 2 copy per A and C. Items 1, 2,
   4, 13.
3. **Selection mode.** Long press → checkboxes and a count, batch delete
   only; merge stays on the detail screen's duplicate banner; the list tick
   is removed; the detail screen's "Saved to your phone" refreshes with one
   query on open (store the Insert result's contact id). Items 7, 16, 17.
4. **A–Z letter headers, manual add, "Added today".** Letter groups with
   "#"; a FAB opening the editor on an empty row; a `source` column (schema
   v3, a real migration as before); "Added" versus "Met"; "card as received"
   removed from the detail overflow. Items 8, 9.
5. **Phone-number formatting.** `PhoneNumberUtils.formatNumber` on prefill,
   save and display in both editors. Item 5.
6. **Card editor delete.** One *Delete my contact card* with a bin (and a
   bin on *Delete this contact*), erases on the band through the existing
   sync, leaves the page. Item 6.
7. **Not found, and Forget that forgets.** 15 s timer → "Handoff band 7A3C
   not found" on the status line, a snackbar, and the service notification;
   Forget also drops the OS bond via `removeBond()` by reflection with a
   fallback pointing at Bluetooth settings. Items 14, 15, with item 14's
   bench question checked on the desk.

Firmware and app together:

8. **"Handoff band 7A3C" and the vibrate setting.** Advertised name to
   "Handoff band 7A3C" (name buffer 18, scan response 19 bytes) with
   `BandCode.name` matching; `BLE_CTRL_HAPTIC` opcode; the flag persisted in
   the store (format bump) and reported in `status` flags bit `0x20`; the
   Settings switch reads that bit; the fake-card path buzzes so the opcode
   is provable. Items 3, 11, O5. After this commit the existing pairing shows
   "Handoff band" until it is forgotten and re-paired.

Not in this round: the received-handshake queue (future improvement, above).
