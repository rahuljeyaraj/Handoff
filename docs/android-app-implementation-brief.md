# Android app — implementation brief

Instructions for whoever picks up the customer-facing rebuild, in this session or
a later one. Written 13 Sep 2026, alongside the design it implements.

**Status: Phase 1 in progress on `app/customer-facing` — steps 1–5 done.** Keep this line current — when you finish a phase, tick
its boxes and say so here, because the next session starts from this file.

---

## Read first

`docs/android-app-decisions.md` is **authoritative**. It records every decision
from the design session with the reasoning and the file:line evidence behind each
claim. Read it fully before writing code. Do not re-litigate what it settles — if
something there looks wrong, say so and ask rather than quietly doing it
differently.

Visual reference, 17 artboards, Material 3, light and dark:
<https://claude.ai/code/artifact/7c240d05-595d-4154-b3a2-8a82abf4e1d0>

Source for those artboards is in `design/android-redesign/`. Read the `.dc.html`
files for exact spacing, type sizes, colours and icons. They are mockups, not code
to port — match the values, not the markup. The canvas payload itself is generated
and gitignored; rebuild it with the `design` skill's `seed-canvas.mjs` if you need
to change an artboard.

## Where things are

- **App:** `android/app/src/main/java/com/handoff/band/` — Compose, Material 3,
  currently a bare `MaterialTheme {}` with no colour or typography file at all.
- **Firmware BLE contract:** `firmware/lib/hal_pico/ble.c`, `ble.h`, and
  `ble_service.gatt`. The `.gatt` file is the authoritative contract with the app;
  changing a UUID or property on one side only fails silently.
- **Design commit:** `f215b3e` on `hardware/pre-layout-review`. That is a hardware
  branch — **cut an app branch off it before you start.**

---

## Phase 1 — app only

Nothing in firmware changes. Each step should build and run on its own.

- [x] **1. A real theme.** Colour tokens, typography, light and dark. Values are in
  the artboards; see §12 of the decisions doc.
- [x] **2. Navigation.** Contacts as home with the band status line; Settings;
  Band; Your contact card; Advanced. Move all four bench tools out of
  `MainActivity` into Advanced — keep them working, they are M2 exit criteria.
- [x] **3. Search and sort.** Search replaces the app bar in place and matches
  name *and* organisation. Sort toggles between newest-first and A–Z.
- [x] **4. Contact detail and edit screens.** New `note` column. "Save to phone"
  becomes a secondary action and keeps using `Intents.Insert` with no
  `WRITE_CONTACTS`.
- [x] **5. Dedup.** `phone_key` and `email_key` as two independent indexed columns,
  merging on either; a `work` column for the second number. Cards with neither a
  phone nor an email are discarded. **Decide and state your Room migration
  strategy** — the database is at version 1 with `exportSchema = false`.
- [ ] **6. Disconnect and Forget band.** Note the trap: the service is
  `START_STICKY` and `MainActivity.onCreate` restarts it from the stored address,
  so Disconnect needs a persisted "user turned this off" flag or it reconnects
  instantly.
- [ ] **7. The card editor.** Every field editable; "Fill from a phone contact" is
  prefill only; a Work phone field; per-field send toggles; and the last remaining
  contact-method toggle disabled, so a name-only card cannot be authored.
- [ ] **8. Band name** from `AssociationInfo.getDisplayName()`. The MAC address
  appears nowhere in the customer-facing UI.
- [ ] **9. Self-provision on connect**, and send `CTRL_FORGET` when the card is
  removed. Both primitives already exist.
- [ ] **10. Forward-compatible `BandStatus.parse`** — accept `version >= 1`, read
  known offsets, ignore trailing bytes. **Do this before Phase 2** or a newer band
  blanks the status line entirely.

## Phase 2 — firmware and app in one commit

The status struct is exactly 16 bytes and full, so this cannot be split.

- [ ] **11. Status struct version 2**, adding battery and firmware version. Read
  VSYS the way `pico-examples/adc/read_vsys` does, because the Pico 2 W shares
  GP29 with the CYW43 SPI clock. Add ~0.35 V for D1's drop. Treat VSYS above
  ~4.3 V as **USB power**, which is *not* charging — the charger is off-board on
  J5 and invisible to the app.

## Phase 3 — QR pairing

- [ ] **12. Scan to pair.** The code yields the band's identity; use it to build a
  `ScanFilter` plus `setSingleDevice(true)` so Android's unavoidable confirmation
  shows exactly one device. The label must be generated per board from the same
  `pico_get_unique_board_id()` bytes the firmware already uses for the name. For
  the demo, print one from the id read over USB.

---

## Do not

- **Do not build OTA or DFU.** Firmware updates are a USB job; the app only
  displays the version. §10 of the decisions doc explains why.
- **Do not touch `hardware/`.** The board is routed, DRC-clean and at PCBWay.
  Nothing in this work needs a board change.
- **Do not add `WRITE_CONTACTS`.** The auto-save setting is drawn but deliberately
  unbuilt.
- **Do not remove** the per-field send toggles, or the raw-vCard view in Advanced.

## Decide with the user before building

- When a handshake delivers a name with no phone or email we discard it, so a buzz
  on the wrist produces nothing visible. A brief "handshake didn't complete"
  notice is the leading suggestion — propose, don't assume.
- The artboards use invented sample people. Ask before seeding any real data.

## How to start

Read the decisions doc and the existing code, then propose a plan with the commit
boundaries you intend. Do not write code until the user has agreed to the plan.
