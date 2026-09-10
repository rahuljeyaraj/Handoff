# Android app

**Not started. This is M2.** Nothing here is built by the Pico SDK or by
`scripts/build.py` — it is a separate Android Studio project that happens to
live in the same repository.

The client is settled as a native Android app: no web client, no iOS. See
[firmware-architecture.md §11](../docs/firmware-architecture.md). The short
version is that a backgrounded browser tab drops the BLE GATT link, and the
phone is in a pocket during a handshake — which is the one condition this link
has to survive.

## What it has to do

The firmware side is already specified and is deliberately insulated from
anything above it. The GATT contract (architecture §11.2) is fixed:

| Characteristic | Access | Purpose |
|---|---|---|
| `my_vcard` | write, chunked | the phone provisions the wristband |
| `rx_vcard` | notify, chunked | received contact, as reconstructed vCard text |
| `status` | notify | link state, last score, fragment bitmap, errors |
| `telemetry` | notify | decimated score stream for the §14.1 body tests |
| `control` | write | carrier select, trigger raw capture, force role |

Chunking: the ATT MTU is not guaranteed above 23 bytes, so every chunked
characteristic carries a 2-byte `seq | total` header and is reassembled on the
client. **Test that at the 23-byte floor**, not at whatever MTU your phone
happens to negotiate — that is an M2 exit criterion precisely because it is the
one that quietly works on the developer's handset and fails on someone else's.

`telemetry` is not a convenience. Design §13 forbids a USB tether to a
mains-powered laptop while anyone is touching an electrode, so from M10 onward
this is the only legal way measurements leave the wristband.

## Interop while it does not exist yet

The wire format is testable today without any of this:

```
python tools/vcf.py encode card.vcf     # compact TLV a wristband would store
python tools/vcf.py decode <hex>        # the vCard the app must hand to Contacts
```

`tools/vcf.py` is an independent reference implementation of the same codec the
firmware uses, and the two are cross-checked against each other in both
directions on every CI run. If the app agrees with `vcf.py`, it agrees with the
wristband.
