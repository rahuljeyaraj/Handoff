# Android app — decisions for the customer-facing build

Decided 12 Sep 2026. This records what the app should become and why, so the
implementation does not have to re-derive it. It is a decision log, not a spec:
where it says "already exists", that was verified against the tree on the day.

The driving constraint: **time is short and the demo is the deadline.** Build the
customer-facing side; the M2 bench tools stay in the build but move out of sight.
Nothing here requires a PCB change.

---

## 1. Navigation and screens

**Option A was chosen.** Home is the contact list. No bottom navigation —
Material wants 3–5 destinations and this app has two, and a two-tab bar reads as
a product that has not decided what it is. Band state collapses to one tappable
line under the top app bar; everything else lives behind the gear.

**There is no separate "contacts page" to navigate to.** The home screen *is*
the contact list. Opening the app lands you on it. This is the point of the
layout, not an omission.

**The band status line navigates; it never expands.** Tapping it opens the Band
screen. It is explicitly not an inline expander, because an expander pushes the
list down and leaves the home screen doing two jobs — which is the problem with
the current `BandPanel` (`MainActivity.kt:160`), only tidier. One line, one tap,
a real screen.

**The line carries four things, all readable without tapping:** band name,
connection state, **whether your card is on the band**, and battery.

The card indicator is an ID-card glyph — green when the band holds your card,
amber with a diagonal slash through it when it does not. The slash is doing the
work: it is the same "this is switched off" convention as a muted microphone, so
it needs no legend. Without it, the only way to find out that your band has
nothing to give away is to go looking, and a conference is the wrong place to
discover that.

Screen set:

| Screen | Purpose |
|---|---|
| Contacts (home) | The list, a band status line, search / sort / settings |
| Contact detail | View a received card, note, Save to phone, Merge |
| Contact edit | In-app editing: name, prefix, suffix, fields, note |
| Settings | Your card · Band · Contacts · About · Advanced |
| Band | Name, MAC, battery, firmware, disconnect, forget |
| Your card | Per-field send toggles, byte preview, remove |
| Advanced | The four bench tools, behind a deliberate tap |
| Setup 1–2 | Pair the band, then optionally set your card |

Two alternatives were drawn and rejected: a two-tab bottom nav (band gets a
screen, but the nav bar is under-populated), and a hub home with a large band
card (band state unmissable, but only four contacts fit and the real list
becomes a second screen).

Design canvas: <https://claude.ai/code/artifact/7c240d05-595d-4154-b3a2-8a82abf4e1d0>
Working files in `design/android-redesign/`.

### Naming

The list is **"Contacts"** — that is what a user understands, and they are
people, not events. "Handshake" is the *event* word, used inside a card ("Met
14:32 today"). The `Handshake` entity keeps its name in code and in the
database; it is accurate there.

The thing a band carries is a **"contact card"**, not a "card". "Card" on its own
is our shorthand; spelled out it is unambiguous the first time somebody reads it.
So: "Your contact card", "Remove my contact card", "their contact card lands
here".

### Copy discipline

**No sentence may restate what the interface already shows.** This cut a lot out
of the first draft and the rule is worth keeping, because every line of
explanatory text is a line that says the layout failed:

- Don't explain that a permission prompt will appear — Android will show it.
- Don't explain that "Remove" removes something.
- Don't explain a choice the user is about to see as two buttons.
- Don't narrate a constraint the controls can enforce (see the contact-method
  rule under §4a).
- Never surface our vocabulary: no MAC addresses, no "bonded", no byte counts, no
  "synced", no voltages, and **no field counts** — "6 of 8 fields" is a
  developer's unit, not a user's. The Band screen says "Rohan Iyer"; if nothing is
  set it says "Not set". Technical state lives in Advanced.
- **Positive phrasing, and "share" not "send".** "Choose what you share" beats
  "Switch off anything you'd rather not send" — the second is a double negative
  that makes the reader do the logic.
- **Never print the same fact twice on one screen.** The single-device pairing
  dialog names the band in its title, so it carries no device row underneath and
  no selector — with one device there is nothing to select.

---

## 2. The band's identity

**Show the band's name, not its MAC address.** The name already exists:
`ble.c:116` builds `"Handoff %02X%02X"` from the last two bytes of the Pico's
unique board id and sends it as the Complete Local Name in the scan response.
That is what Android's chooser displays, and it is how two bands on one bench
are told apart.

The app currently throws it away — `Pairing.addressFrom()` extracts only the MAC
from the association result. Store `AssociationInfo.getDisplayName()` alongside
the address and show that.

Do **not** read the name from the GATT GAP characteristic: that one is the
generic `"Handoff"` for every board, because `gap_set_local_name()` belongs to
BTstack's Classic half (`ble.c:457`).

**The MAC address appears nowhere in the customer-facing UI** — not even as
secondary detail on the Band screen. It is an identifier for us, not for the
wearer. Advanced is where it belongs if it is shown at all.

---

## 2a. Pairing is by QR code, not by searching

**Rejected: "Search for bands".** At a conference there are a hundred bands in
range and nothing tells the wearer which one is theirs. A list of near-identical
names is a guess, and guessing wrong pairs you to a stranger's wristband.

**Decision: a QR code printed on the underside of the band.** Scan it, and the
app knows exactly which band to connect to. If the band is switched off it
matches nothing and the scan fails — which is the correct failure, and a far
clearer one than an empty list.

**The constraint that shapes this: CompanionDeviceManager cannot associate
silently.** Android always draws a confirmation. So the flow is:

1. Scan the code → it yields that band's identity.
2. Build the `AssociationRequest` with a `ScanFilter` for that specific
   device, plus `setSingleDevice(true)`.
3. Android shows **one** entry, not a list. One tap on one band.

That keeps the OS guarantee (the user consents to a named device) while removing
the guesswork entirely. The dialog's wording is Android's and not ours to change.

**The code's content:** the band's name and address, so the filter can match on
both. The name is already derived from `pico_get_unique_board_id()`, so the label
must be generated **per board, from the same id the firmware uses**. That is a
production step — for the demo, print a label from the id read over USB.

**Fallback:** a text button to enter the band code by hand, for a label that has
worn off or will not scan. One small secondary action, not a second equal path.

---

## 3. Duplicate contacts

Today there is **no deduplication at all**: `BandService.onCardReceived` calls
`insert()` unconditionally, so meeting the same person twice is two rows.

**Decision — match on phone and email, never on name.** The display name is
user-editable (prefix, suffix, corrections), so it cannot be part of identity.

Two nullable, indexed columns rather than one composite key:

- `phone_key` — digits only, last 9 kept (so `+91 98410 23117`, `098410 23117`
  and `9841023117` are one person)
- `email_key` — lowercased

A new card matches an existing row if **either** key matches. Two columns rather
than one concatenated key because that handles progressive enrichment: a
name+email card today merges with a name+email+phone card next week.

On match, **merge**: fill fields that were blank, bump `received_at`, and never
touch the user's own edits — note, prefix, suffix, corrected name.

Cards arriving with neither key are not stored at all (see §4a), so there is no
name-only pile-up to engineer around. A manual **"Merge into…"** action remains
the escape hatch for everything else — surfaced as a possible-duplicate banner on
the contact detail screen, and as a long-press option in the list.

---

## 4. Editing contacts in the app

Today, tapping a card fires `Promote.intentFor` and hands everything to the
system contact editor. The app owns no editing at all, so there is nowhere to
put a note.

**Decision — the app gets its own contact detail and edit screens.** New local
columns: `note`, plus a display-name override. Editing is in-app and local.

**No prefix or suffix fields.** An earlier draft had them, from a first reading
of "I may need to add a prefix or suffix to a contact". What that actually meant
is that people will *alter the name* before saving — so the name field alone does
the job, and two extra boxes just crowd the form.

**Two phone numbers on the edit screen**, Mobile and Work, because that is what
the codec carries (see §4a). Not a repeating list: a third number would need a
schema change here and a compact-encoding change in the firmware, and two covers
the case.

"Save to phone contacts" becomes a *secondary* action on the detail screen
rather than the only thing a tap can do. It keeps using
`ContactsContract.Intents.Insert` — no permission asked, the system editor
confirms — which is the existing, deliberate design.

> **Revised 14 Sep 2026.** The button under a received contact is one button
> in three states, and the caption beneath it is gone (the wearer: a button
> and a line saying the same thing is bad UI):
>
> - *Save to phone contacts* — not saved yet; the Insert intent as before.
> - *Saved to phone contacts*, disabled — saved, and nothing has changed
>   since.
> - *Update phone contact* — saved, then edited here (`edited_since_promote`,
>   set by a real change in the editor or a merge that filled a blank; a
>   Save with nothing changed does not set it).
>
> Update is a **direct write** to the raw contact the save created
> (`raw_contact_id`, recorded from the editor's result), replacing its name,
> phones, email, organisation/title and note rows and leaving anything else
> on that contact alone. This is the one place the app holds `WRITE_CONTACTS`,
> requested at that tap (on a phone where READ_CONTACTS is already granted
> the group grant makes it silent). The intent routes were tried and rejected:
> `INSERT_OR_EDIT` is "add these values to a contact you pick", so a changed
> name cannot find its own contact and every value must be retyped; a plain
> Insert makes a second contact. If the write cannot happen — permission
> refused, contact gone — the system editor opens on that contact. Saving a
> *new* contact still never writes directly, and the auto-save setting stays
> unbuilt.

**The note and the prefix/suffix do reach the phone contact.** `Promote` already
passes `NOTES` (`Promote.kt:53`); it has simply never had a note to pass. And
`Insert.NAME` is a single string, so a prefixed or suffixed display name carries
as written. Both arrive prefilled in the system editor for the user to confirm.

The raw vCard is already stored and stays stored. Edits never overwrite it; the
detail screen's overflow can show it.

---

## 4a. Your own card — authored in the app, not only imported

> **Revised 14 Sep 2026** after the wearer's review of the built page
> (`design/your-card/`). Three of the decisions below are reversed; the
> original text is kept beneath for the reasoning it records.
>
> - **No per-field switches.** A field you would rather not share is a field
>   you leave empty. Every non-blank field is sent; the switches, and the
>   sentence that explained them, are gone.
> - **No "Fill from a phone contact".** Five fields is not much typing, and on
>   a card that already exists it would overwrite what was written.
> - **Phone is a number plus a label**, the way the phone's Contacts app has
>   it, with *Add another phone* for a second row — two at most. The label
>   menu offers Mobile and Work only, because those are the two typed slots
>   the band carries; Home and Other would need a codec change.
>
>   **Wanted, not yet built (wearer, 14 Sep 2026):** the four labels the
>   design board draws — Mobile, Work, Home, Other — and the same label on
>   both rows (two mobiles). This is a wire-format change, not an editor
>   change: `compact.h` has exactly `TAG_TEL_CELL` and `TAG_TEL_WORK`, and
>   the app's own card, the received-contact row (`mobile`/`work` columns),
>   the contact editor, the dedup key and the phone-contacts insert/update
>   are all shaped as one mobile plus one work. Doing it means two new tags
>   (or a label byte on the TEL value) in the firmware codec and its tests,
>   reflashing the band, and number+label pairs end to end in the app.
>   Deferred by the wearer until it can be its own change.
> - **A saved card opens read-only**, the same page a received contact gets:
>   pen and bin in the app bar, the header, the rows, and one chip under the
>   name for the fact only this page knows — *On Handoff band 93D1*, *Not on
>   the band yet* (band off, away, or still being written; the toast after
>   Save already covers the writing), or *Band has an older copy*. Editing is
>   a page of its own, titled *Edit your card*.
> - **Delete is in both places**, bin on the view and the red line at the
>   bottom of the editor, as in Contacts. Either lands on the Band page.
> - **Organisation appears once**, in the role line under the name, on this
>   page and on a received contact's. No Organisation row.
> - **"Handed over when you shake hands."** is the one sentence, on setup
>   step 2, the first-time editor, and the foot of the saved card.
>
> A work number counts as a way to be reached: `complete` is a name plus a
> phone or an email.

**The card editor is a real editor.** Every field is an editable text field, and
"Fill from a phone contact" is a *prefill convenience* at the top of that
screen — not a fork in the road. Picking a contact drops its values into the
fields, and you carry on editing. A contact record and a card you want to hand
out are not always the same thing.

`ProvisionActivity` already supports manual entry; the first draft of this
redesign regressed it into read-only rows with toggles. That was wrong.

**Per-field send toggles stay.** They are a good feature: the card crosses a
stranger's skin and the wearer should be able to send a name and a mobile without
also sending their job title.

**A name-only card is never *sent*.** The name is always included, but at least
one of mobile or email must stay switched on — a card with neither gives the
recipient no way to reach anybody, so there is no point putting it on the band.

**Enforce it with the control, not with a warning.** When only one contact method
is left on, that toggle goes disabled: you physically cannot switch it off, and no
sentence has to explain why. An earlier draft carried a paragraph about keeping a
phone number on, which is exactly the kind of copy §1's copy-discipline rule
exists to delete.

**A name-only card is not *received* either — it is a failed handshake.** A short
handshake can truncate to nothing but FN, and an earlier draft showed those in the
list as "Name only — 1 of 8 fields". That was wrong: a name with no phone and no
email is not a contact. You cannot reach the person, you cannot deduplicate it,
and you cannot usefully save it.

**Decision: a card arriving with no phone and no email never becomes a list
entry.** Treat it as an incomplete transfer — a transient "handshake didn't
complete" notice, and nothing in Contacts. The raw text can still be kept for
diagnostics, surfaced in Advanced, not in the user's list.

This removes the one case §4's dedup could not handle, so the "name-only rows
pile up" limitation goes away with it.

> **Confirm before building:** this silently discards a handshake the wearer may
> have felt the band buzz for. The alternative is a visible "try again" prompt.
> Either is defensible; do not let it fail silently with no feedback at all.

**The raw vCard preview moves to Advanced.** "What gets sent" with a byte count
is a developer's view. The toggles already tell the user what is being sent, in
their own language. The mono dump belongs with the other bench tools.

### Phone numbers — two, and two bugs

`VCard` handles `TEL;TYPE=CELL` and `TEL;TYPE=WORK`, so **two** numbers
(`VCard.kt:119-120`). `ContactReader` reads both, `Promote` writes both as
`PHONE` and `SECONDARY_PHONE`. Three or more are not supported and will not be.

Two places drop one of them today:

- `ProvisionActivity`'s form exposes only mobile, so a picked contact's work
  number is silently discarded on the way out. **Fix: a Work phone field on the
  card editor, with its own toggle.**
- The `Handshake` entity has only a `mobile` column, so a *received* work number
  survives in the raw vCard with nowhere to display. **Fix: add a `work`
  column.**

---

## 4b. Search

**Search replaces the app bar in place.** Tapping the search icon turns the bar
into a text field — back arrow, the query, a clear button — and the list filters
beneath it as you type. No new screen, no separate search destination. This is
Android's standard search-view pattern.

**It matches on name *and* organisation.** "Who was that person from PCBWay" is
the question someone actually has after a conference, and matching only names
cannot answer it.

---

## 5. Ordering

Both, with a sort toggle in the top app bar:

- **Newest first (default)** — already the DAO's behaviour,
  `ORDER BY received_at DESC`. Right after a conference this is the order you
  want.
- **A–Z** by display name.

Date section headers ("Today", "Yesterday", "Earlier") in the newest-first view.

---

## 6. Connecting and disconnecting

There is currently **no way to disconnect or unpair**. `Pairing.forget()` exists
but nothing calls it, and `BandService` has no stop entry point.

**Two distinct actions, not one:**

- **Disconnect** — drop the link, keep the pairing.
- **Forget this band** — `Pairing.forget()` +
  `CompanionDeviceManager.disassociate()` + stop the service.

**Watch out:** the service is `START_STICKY` and `MainActivity.onCreate`
restarts it from the stored address on every launch. Disconnect therefore needs
a persisted "user turned this off" flag, or it reconnects immediately.

**One phone pairs with one band** — already enforced three times over:
`setSingleDevice(true)` on the association request, a single `band_address`
preference key, and `onStartCommand` closing the old client when the address
changes. Nothing to build — and nothing to *say* either: the Band screen shows
one band, which is the statement. Forgetting it is how you swap.

---

## 7. Provisioning the wearer's own card

Today provisioning only happens when someone presses "Write to band" in
`ProvisionActivity`, and the service does not retain the card.

**Decision — self-provision on connect.** Persist the user's own card, then in
`onConnectionChanged(ready = true)` push it if the band does not already have
it. The `status` payload gives `provisioned` and `ownBlobLen` but no content
hash, so the rule is: keep a local hash of the last card pushed, and push when
either the local card changed or the band reports `provisioned == false`.

**Decision — removing your card clears the band.** If the local card is empty
and `status.provisioned` is true, send `CTRL_FORGET`. The primitive already
exists — `BLE_CTRL_FORGET = 0x06`, "erase the provisioned record"
(`ble.h:73`), already wrapped as `Gatt.forget()`. No firmware change.

**Caveat:** `ble.c:249` silently drops `FORGET` unless the link is encrypted, so
this must be retried after bonding rather than fired blindly.

**Setting your own card is an optional setup step.** Setup is: pair the band →
optionally set your card → done.

**Step 2 offers two actions, not three.** "Set up my card" and "Skip for now".
An earlier draft offered *Pick from contacts / Type it in myself / Skip*, which
forced a choice before the user knew what either meant, and then stranded anyone
who picked a contact on a screen with no way to edit it. Pick-versus-type is not
a decision worth a screen: it is a button at the top of the editor. One primary
action leads to the editor; the editor handles both.

Four routes to the card editor, because "where do I set my card" should never be
a puzzle:

1. Settings → **Your card** (the first row)
2. The nudge banner on the home screen, while no card is set
3. Step 2 of first-run setup
4. The Band screen's **"Your card on the band"** row

The nudge banner sits above the list **whenever no card is set** — not only
while the list is empty. Someone can collect a dozen cards before realising they
never set their own, and a conference is the wrong place to discover it.

**Unprovisioned is a normal state.** Without a card you still receive other
people's cards; yours just is not sent. Nothing in the code fights this — the
firmware treats unprovisioned as normal (`handoff.c:217`) and the RX path never
consults `BLE_ST_PROVISIONED`. Worth a bench confirmation once the real
body-channel exchange exists; it is not built yet, the fake-RX opcode stands in
for it.

---

## 8. Battery indicator

**No PCB change. The circuit is already there** — the Pico module's VSYS/3
divider on GP29 / ADC3, documented at `hardware/README.md:327`.

**Three blocks**, the way a phone used to show it, plus two states that are not
levels:

| State | Blocks | Colour | Cell voltage |
|---|---|---|---|
| Full | 3 | green | 4.0 V and up |
| Good | 2 | green | 3.7 – 4.0 V |
| Low | 1 | amber | 3.5 – 3.7 V |
| Critical | 0, empty | red | below 3.5 V |
| Unknown | dashed outline, dash | grey | no status yet |
| USB power | plug glyph, no battery | neutral | VSYS above ~4.3 V |

Critical is an empty outline rather than one red block — an empty battery reads
as empty instantly, and it removes the "is that one block or two" squint at
16px.

**Unknown is its own state and must never render as Critical.** Before the first
status notify arrives — or on firmware without the field — you genuinely do not
know, and a false Critical sends someone chasing a dead cell that is not dead.

Thresholds are deliberately coarse. A Li-ion discharge curve is nearly flat
between 3.7 and 3.9 V, so a percentage would be fiction.

Implementation notes:

- The reading is VSYS, **after D1**. Add ~0.35 V for cell voltage (0.3 V idle,
  0.4 V with the radio on), or calibrate once against J1 pin 2.
- This is a Pico 2 W: GP29 is shared with the CYW43 SPI clock. Read it the way
  `pico-examples/adc/read_vsys` does, not with a naive `adc_read()`.
- **USB is not charging, and the app must never say it is.** The TP4056 is
  off-board and plugs into J5 (`hardware/README.md:96`); the app cannot see it
  at all. USB into the Pico goes VBUS → the Pico's internal Schottky → VSYS and
  only carries the board. VSYS then reads about 4.6 V, which is above any Li-ion
  voltage and is therefore how you detect the condition — and the cell itself is
  unmeasurable, because D1 blocks it. The honest state is **"USB power — cell
  not measured"**, drawn as a plug rather than a battery with a lightning bolt.
- **`BandStatus` is exactly 16 bytes and full.** Adding battery means struct
  version 2, and the app's parser currently rejects any version it does not
  recognise. Firmware and app must ship in the same commit.

---

## 9. Firmware version skew

**`BandStatus.parse` must stop hard-failing on a higher version.** Today it
returns `null` on any mismatch (`Gatt.kt:110`), so a newer band makes the entire
status line vanish silently with no error.

**Decision — forward-compatible parsing:** accept `version >= 1`, read the
fields you know by offset, ignore trailing bytes you do not. A newer band then
degrades to "battery unknown" rather than going blank. Reserve a "please update
the app" message for a version whose required fields genuinely cannot be read.

Also add a **firmware version field** to the status struct and show it on the
Band screen — same struct bump as the battery field, one commit.

---

## 10. Firmware updates from the phone — not building this

There is **no OTA, no DFU, no bootloader and no firmware version constant** in
the tree. Updating is BOOTSEL plus a `.uf2` over USB.

A BLE OTA on the RP2350 would need a custom bootloader, a second flash slot (the
flash already has a record sector bound), a chunked multi-hundred-kB transfer
over a protocol whose floor is 23 bytes, and a rollback path. That is a
milestone of its own.

**Decision:** firmware is a bench/USB job. The app *displays* the band's firmware
version, on the Band screen and nowhere else, so you know what is on the wrist.
It does not explain that updates happen over USB — anyone who can flash a board
knows, and anyone who cannot is not helped by the sentence.

---

## 11. Where the bench tools go

All four move to **Settings → Advanced**, behind a caption saying they are
bring-up tools and some will drop the connection. They stay in the build —
they are M2 exit criteria, not debug toys.

- Force the 23-byte ATT MTU floor (switch)
- Run a Bluetooth scan (with its result line)
- Send a fake card in 10 s
- Erase the card on the band
- The raw `status` dump: encrypted, provisioned, flash, record id, own blob
  length, chunk errors, frame errors, link state
- **The card as written to the band** — raw vCard text and byte count, moved here
  off the Your card screen, where it was a developer's view wearing a user's hat

Nothing from this list appears on the home screen.

---

## 12. Visual language

The app has no design system today — bare `MaterialTheme {}` with stock M3
defaults, no colour or typography file. Established here:

- Material 3, standard Android conventions, because intuitive beats clever.
- Roboto (the platform face) and Roboto Mono for MACs, byte previews and the
  status dump.
- Deep indigo primary `#40518B`, warm-neutral surfaces (`#FCF9F6` light,
  `#141413` dark). Primary deliberately avoids green, amber and red so those
  stay available for connection and battery semantics.
- Semantic colours: `#2E6B4F` ok, `#8A5A00` warning, `#BA1A1A` error.
- Dark mode ships. **Theme is a Settings row** — System default / Light / Dark,
  defaulting to System. Never a toggle in the app bar: that bar holds actions, not
  preferences.
- **Use the platform's own settings cog**, the same glyph Android shows in Quick
  Settings — in the real app, Material Symbols `settings`. Do not draw one by
  hand: a circle with thin radial lines is a *sun*, and in an earlier draft that
  made Settings genuinely unfindable.
- **Battery is always the icon, never words or volts.** The Settings row for the
  band shows "Connected" plus the battery glyph, exactly as the home screen's
  status line does. "3.9 V" is a bench number and belongs in Advanced.
- Partial cards are shown honestly ("Name only — 1 of 8 fields") rather than
  looking broken. A card that crossed a body in 250 ms is partial by design.

---

## 13. Charging — a hardware note that the app must reflect

**Plugging USB into the Pico does not charge the cell.** The charger is
off-board. J5 (charger) is in parallel with J1 (cell), **both upstream of SW1**
(`hardware/README.md:249`), then SW1 → D1 → JP5 → VSYS. Charging only happens
when the TP4056 is plugged into J5, and the app has no visibility of it.

**Charging with SW1 on is safe but makes termination unreliable.** The TP4056
has no power-path, so board current adds to charge current and the ~1/10 C taper
threshold may never be reached: charging runs long, the CHRG LED flickers rather
than going solid, and it can oscillate against the 4.05 V recharge threshold. A
100 mA haptic pulse during the CV taper makes the LED bounce. Nothing
back-feeds — D1 blocks VSYS from driving the cell, and the Pico's internal
Schottky ORs VBUS in — so there is no damage path.

**Recommended practice, for the bring-up instructions:** plug USB into the Pico
as well while charging. VBUS then carries the board, D1 keeps the cell out of
it, the TP4056 sees a battery-only load and terminates correctly. Otherwise
switch SW1 off while charging.

---

## Open items

- Sort toggle placement: top app bar action vs. Settings. Currently drawn as
  both (action on home, remembered preference in Settings).
- "Save to phone automatically" is drawn in Settings, off by default. It needs
  a provider insert and is deliberately not built yet. (`WRITE_CONTACTS` is
  now declared for "Update phone contact", §4 — the setting would reuse it.)
- Four phone labels with duplicates allowed on the own card — a firmware
  codec change, see §4a.
- Search is drawn in the app bar but its behaviour is unspecified.
