# Brief: one band, one phone — reset by the button, reset by Forget

Prompt for the next session. Read this whole file before touching code.
Decided with the wearer on 15 Sep 2026 after a look at how shipped
products handle a band that changes hands (headphones' "clear device
list" hold; Oura's one-phone rule and factory reset; Garmin's Forget
Phone). Bench bands **93D1** and **379E**; bench phone OnePlus CPH2569
(remember its scan-filter-slot reset, `oneplus-ble-filter-slots.md`).
A second phone is needed for §7.

Working tree at the time of writing has uncommitted, unrelated work:
`CMakeLists.txt` and `firmware/ld/` (the core-0 stack move, see
`core0-stack-in-ram.md`). Leave it alone or commit it first; do not mix
it into this change.

---

## 1. The model

Bands are handed out. A wearer has **one band and one phone**. A band
that reaches a new wearer may be fresh, or it may still remember a
previous wearer's phone and carry the previous wearer's contact card.
The new wearer has the band and their own phone, and nothing else — the
previous phone is not available and must not be needed.

So the band has exactly two states, both persistent in flash and both
surviving reboot and power loss:

| state | bond in `le_device_db` | card in the store | advertising | pairing |
|---|---|---|---|---|
| **no owner** | none | none | open, forever | accepted from any phone |
| **owned** | exactly one | the owner's, if written | open (the phone must be able to find and connect) | accepted only from the owner's phone |

"Forever" is literal: a band with no owner is discoverable and pairable
indefinitely, with no window and no timeout, and a reboot in that state
comes back in that state. There is no third state.

## 2. What the button does

Gestures after this change:

| press | meaning |
|---|---|
| short | battery check (unchanged) |
| held past 6 s, released | **reset for a new wearer** |

The 2 s "dev mode" hold is **removed** (see §5). Hold feedback stays:
a tap at 6 s so the wearer knows to let go. Nothing happens at 2 s.

**Reset for a new wearer** puts the band in the *no owner* state from
whatever it was in. It must clear everything the previous wearer left:

1. every bond (`ble_forget_bonds()`, already exists);
2. the stored contact card (`store_forget()`, today only reachable from
   the app's `BLE_CTRL_FORGET`);
3. any received card being held for a phone that never came
   (`s_rx_pending` and the buffer behind it in `handoff.c`);
4. the haptic preference, back to its default;
5. anything else in the store that is per-wearer — check `store.h`.

Then the bond-cleared pattern (purple x4 + buzz, exists), then the blue
"pairing" background because the band now has no owner (exists: it is
driven by `!connected && !bonded` in `wear.c`).

Today the 6 s hold does step 1 only. That is the bug this brief exists
for: a band that changes hands hands the previous wearer's card to the
new phone at first connect.

## 3. What the app's *Forget this band* does

The same thing, from the other side. `Forget this band`
(`BandScreen.kt`, `BandService.forget`) must:

1. if the band is connected and the link is encrypted, send it the reset
   (new opcode, e.g. `BLE_CTRL_RESET`; it does everything §2 does) and
   wait for the status notification that follows, as `CTRL_FORGET`
   already does;
2. then clear the phone side exactly as today (prefs, CDM association,
   `removeBond`).

If the band is not reachable, do step 2 only and say nothing special —
the band will be cleaned by the button when it next changes hands.

Both roads end in the same state: no owner, no card, blinking blue.

`Erase the card on the band` in Advanced stays as it is (card only).

## 4. The band belongs to one phone

`ble.c` `SM_EVENT_JUST_WORKS_REQUEST` confirms every request today, so
a second phone can bond to an owned band without any reset, and a
previous phone can bond back after one. Replace it with the rule:

- no bond stored → `sm_just_works_confirm`
- the requesting peer resolves to the stored bond → `sm_just_works_confirm`
  (this is the wearer who did *Forget* on the phone but not the band, or
  a phone whose bond got lost; they can re-pair without touching the
  band. BTstack resolves the peer's identity on connect, before pairing
  starts — `sm_le_device_index(handle)` or the
  `SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED` seen for this connection)
- anyone else → `sm_bonding_decline`

No filter accept list, no resolving-list dependency: any phone may
*connect* (the `control` characteristic is deliberately open for the
bench), only pairing is gated. Keep the existing rule that `FAKE_RX`,
`FORGET` and the new `RESET` need an encrypted link.

`MAX_NR_LE_DEVICE_DB_ENTRIES` stays 4 (BTstack wants headroom) but the
band never holds more than one bond by this rule; assert that in
`ble_has_bond()` or leave a comment.

## 5. Remove dev mode

There is no such feature. Remove:

- `wear.c`: `s_dev`, `wear_dev_mode()`, the `BUTTON_HOLD1` case;
- `button.h`/`button.c`: `BUTTON_HOLD1`, `BUTTON_HOLD1_REACHED`,
  `BUTTON_HOLD1_MS` — the state machine becomes short / hold reached /
  hold, with one threshold at 6 s. Keep the tests honest
  (`firmware/test` if button has one);
- `ui.h`/`ui.c`: `UI_EV_DEV_ON`, `UI_EV_DEV_OFF`, the purple-blip
  background, the `dev` field, and its row in the priority list and the
  table in the `ui.h` header comment;
- `ble.h`/`handoff.c`: `BLE_ST_DEV_MODE` (0x40) and the line that sets
  it. Leave the bit unused rather than renumbering the others; the app
  parses the flags;
- app: `Gatt.kt`/`BandStatus` if the bit is decoded anywhere, and the
  `StatusDump` row if there is one. Check `BandStatusTest.kt`.

The wearer-UI table in `ui.h` and the console `u`/`b` walk recipe in the
auto-memory (`wearer-ui.md`) both list the row; update both.

## 6. Copy

**New wearer's phone, pairing refused** (`BandClient.ERR_BOND_REFUSED`
reaching the pairing page's failed state). Decided by the wearer, use
verbatim:

    Band in use
    Hold the button for 6 s to reset.

The same two lines go into the 15 s not-found hint, since a band the
chooser cannot find is very often one that is simply not in range, but
the reset is the only thing the wearer can *do*.

**Previous wearer's phone.** No message that claims to know what
happened — the phone cannot know the band was reset, it only observes
that the band no longer accepts its key (encryption failure on
reconnect, or a declined re-pair). On either, `BandService` forgets the
band exactly as it does when the bond is removed in Bluetooth settings
(`BandService.kt` ~L118) and the app is back on *Pair a band*. Nothing
else on screen.

## 7. Bench, before calling it done

Two phones, two bands.

1. **Reset wipes the wearer.** Pair 93D1 to phone A, write a card.
   Hold 6 s. Pair to phone B: status must show *not provisioned*, no
   card arrives, `my_vcard` reads empty. Repeat with a held received
   card (use `FAKE_RX` with the phone away, then reset).
2. **One owner.** With 93D1 owned by phone A, pair from phone B without
   a reset: B must fail with the §6 message within a few seconds, A must
   stay connected or reconnect untouched.
3. **Owner re-pairs.** Forget in Bluetooth *settings* on A (not the
   app), then pair again from A: must succeed without a reset.
4. **Forget = button.** *Forget this band* on A while connected, then
   pair from B: same result as test 1, no button touched.
5. **The old phone in range.** Own 93D1 by A, reset by button with A
   still in range and its app running. Watch A with
   `adb logcat -v time | grep -iE "btif_dm|bond_state|create_bond|remove_bond|sec"`:
   A's reconnect must fail on the missing key and A's app must fall
   back to *Pair a band*. If A's stack re-pairs on its own with no
   dialog, the fix is on A's side (drop the bond the first time the
   band refuses), **not** a time limit on the band — §1 is not
   negotiable.
6. **Reboot in no-owner state.** Reset, power-cycle, confirm blue
   blink and open pairing from B.
7. **Dev mode is gone.** Hold 3 s and release: nothing but the hold
   tap at 6 s if held that long; no purple; status bit 0x40 never set.

Log the runs the way the pairing brief did (timestamps, what happened).

## 8. Out of scope

- Switching a band between two phones a wearer owns (the headphone
  case). Not the product.
- An accept list / closed advertising while owned. Not needed once
  pairing is gated, and it drags in controller address resolution.
- Identify-from-the-app and the wearer-side app work already listed in
  `wearer-ui.md`.
