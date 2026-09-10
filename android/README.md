# Android app

**This is M2.** A separate Android Studio / Gradle project that happens to live
in the same repository — neither the Pico SDK nor `scripts/build.py` knows this
directory exists.

The client is settled as a native Android app: no web client, no iOS. See
[firmware-architecture.md §11](../docs/firmware-architecture.md). The short
version is that a backgrounded browser tab drops the BLE GATT link, and the
phone is in a pocket during a handshake — which is the one condition this link
has to survive. The foreground service in [`ble/BandService.kt`](app/src/main/java/com/handoff/band/ble/BandService.kt)
is the whole reason this is native rather than a web page.

## Build

```
cd android
gradle wrapper                # once — the wrapper JAR is a binary and is not
                              # committed; Android Studio does this on open
./gradlew installDebug
./gradlew test                # ChunkTest, no device needed
```

Needs a JDK 17 and the Android SDK (compileSdk 35). The Gradle version is
pinned in `gradle/wrapper/gradle-wrapper.properties`, so `gradle wrapper` picks
it up whatever Gradle you happen to run it with. CI uses
`gradle/actions/setup-gradle` and skips the wrapper entirely. Versions are pinned in
`gradle/libs.versions.toml` for the same reason the firmware pins its SDK and
toolchain in `CMakeLists.txt`: a build that drifts is a build that cannot be
bisected.

Flash the band with `handoff.uf2` first — see the root
[README](../README.md#build).

## What is here

| File | What it is |
|---|---|
| [`ble/Gatt.kt`](app/src/main/java/com/handoff/band/ble/Gatt.kt) | the UUIDs, control opcodes and `status` layout |
| [`ble/Chunk.kt`](app/src/main/java/com/handoff/band/ble/Chunk.kt) | `seq \| total` framing — the counterpart of `firmware/lib/link/chunk.c` |
| [`ble/BandClient.kt`](app/src/main/java/com/handoff/band/ble/BandClient.kt) | the GATT client, with one serialised operation at a time |
| [`ble/BandService.kt`](app/src/main/java/com/handoff/band/ble/BandService.kt) | the foreground service that holds the link with the screen off |
| [`ble/Pairing.kt`](app/src/main/java/com/handoff/band/ble/Pairing.kt) | `CompanionDeviceManager` — one OS dialog per device, ever |
| [`data/Handshakes.kt`](app/src/main/java/com/handoff/band/data/Handshakes.kt) | the Room database every received card is written to |
| [`vcard/VCard.kt`](app/src/main/java/com/handoff/band/vcard/VCard.kt) | vCard 3.0, both directions |
| [`contacts/Promote.kt`](app/src/main/java/com/handoff/band/contacts/Promote.kt) | the Contacts insert intent, and the contact reader |
| [`ui/`](app/src/main/java/com/handoff/band/ui/) | history list, provisioning form with per-field toggles |

## Walking the M2 exit criteria

The development plan's criteria, and how to check each one:

1. **A fake card reaches the address book.** Pair, then tap *Fake card in 10 s*.
   The band notifies `rx_vcard`; the card appears in the history list; tapping
   it opens the system contact editor prefilled.
2. **Provisioning round-trips.** *Provision* → pick a contact or type one →
   *Write to band*. Power-cycle the Pico. The `status` line in the app shows
   `provisioned true` and the same record id and byte count as before.
3. **Provisioning from the address book.** The same, via *Pick from Contacts*.
   Per-field checkboxes, and the preview is the literal bytes that get written.
4. **Chunked reassembly at the 23-byte MTU floor.** Turn on *Force the 23-byte
   ATT MTU floor* and repeat 1 and 2. `./gradlew test` proves the same thing on
   the framing alone, as does `python scripts/test.py --suite chunk` on the C.
5. **A notify caught with the screen off.** Tap *Fake card in 10 s*, lock the
   phone, put it down. The notification is what the foreground service caught,
   and it is the case a web client structurally could not have handled.
6. **The bond survives a Pico power cycle.** Unplug and replug the band. The
   app reconnects with no fresh OS pairing dialog — the bond is in BTstack's
   flash bank, three sectors from the end.

## The contract

`firmware/lib/hal_pico/ble_service.gatt` is authoritative, and `ble/Gatt.kt` is
the other side of it. **Changing a UUID or a property on one side and not the
other does not fail loudly**: the app connects, discovers nothing it
recognises, and reports no error at all. Change both in one commit or neither.

| Characteristic | Access | Purpose |
|---|---|---|
| `my_vcard` | write, chunked, encrypted | the phone provisions the wristband |
| `rx_vcard` | notify, chunked, encrypted | received contact, as reconstructed vCard text |
| `status` | read + notify | link state, last score, fragment bitmap, errors |
| `telemetry` | notify | decimated score stream for the §14.1 body tests |
| `control` | write | carrier select, trigger raw capture, force role |

Chunking: the ATT MTU is not guaranteed above 23 bytes, so every chunked
characteristic carries a 2-byte `seq | total` header. Every chunk but the last
is payload-full, which is what lets the receiver derive the capacity from chunk
0 and never be told the MTU.

`telemetry` is not a convenience. Design §13 forbids a USB tether to a
mains-powered laptop while anyone is touching an electrode, so from M10 onward
this is the only legal way measurements leave the wristband.

## Interop with the reference codec

The wire format is checkable without a phone at all:

```
python tools/vcf.py encode card.vcf     # compact TLV a wristband would store
python tools/vcf.py decode <hex>        # the vCard the app must hand to Contacts
```

`tools/vcf.py` is an independent reference implementation of the same codec the
firmware uses, and the two are cross-checked against each other in both
directions on every CI run. If the app agrees with `vcf.py`, it agrees with the
wristband.

## Not built, deliberately

- **Auto-save to Contacts.** Architecture §11.3 describes it as an optional
  setting, off by default. It is the only thing that would need
  `WRITE_CONTACTS`, and the manifest does not declare that permission at all.
  Promotion goes through the system contact editor, which needs nothing.
- **Search, sort, and a detail screen.** Worth building when there is a real
  handshake to put in them, which is M12.
- **Re-push when the source contact changes.** The band holds a snapshot;
  §11.3 wants "based on contact X, last synced <when>" and an offer to re-push.
  The provisioning path is there; the bookkeeping is not.
