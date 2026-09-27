# Handoff — Firmware Architecture

Rev 1.0. Companion to [body-coupled-handshake-design.md](body-coupled-handshake-design.md).

The design document specifies the link.
This document specifies **the shape of the code** — every module, every seam and
every interface, including ones nothing uses yet.

---

## 1. The rule this document exists to enforce

> **Every header in §4 is created before M1 ends. Milestones fill in
> implementations. Milestones never invent new seams.**

If M13 (half-duplex turnaround) discovers it needs a module boundary that does
not already exist here, that is an architecture bug to be fixed *in this
document* first, not a file to be appended to whatever was nearest.

The practical test: a reader at M1 should be able to point at the file that will
eventually hold role election, open it, and find a header with the right
functions and an empty body. That file exists from day one.

Two consequences worth stating plainly:

- **Stubs are committed.** `lib/proto/elect.c` returns `PROTO_UNIMPLEMENTED` for
  eleven milestones. That is correct and intentional.
- **The dependency graph is fixed now.** Adding an `#include` that points *up*
  the layer stack is the failure mode this whole structure exists to prevent, and
  CI enforces it (§3.2).

---

## 2. What changed from the design document

Three requirements moved. All are recorded here and flagged in the design doc.

| Design doc | Now | Why |
|---|---|---|
| R1 / §9.4 — "40-byte contact record", single packet | **Full vCard**, fragmented across frames | 40 bytes cannot hold a real contact |
| §10.6 — Web Bluetooth | **Native Android app**, no web client | a backgrounded browser tab drops the BLE GATT link, and the phone is pocketed during a handshake; see §11 |
| §9.6 — listen-before-talk role election, 0–5 ms backoff, ties broken by redraw | **No election at all**; the contact trigger decides who sends | the trigger has to exist anyway (§7.6), and once it does, its timing geometry already names one sender — the draw would only be re-deriving it, and re-deriving it badly |

The first change is not cosmetic. A 40-byte payload fits one packet and needs no
sequencing, no reassembly and no transfer strategy. A vCard needs all three. §8
is the consequence, and it is the largest new part of this architecture.

The third is a deletion rather than an addition, and it is the larger
simplification: `elect.c`, its tests, its backoff constant, its tie-and-redraw
path and the wake-role hint that fed it are all gone, and §7.6's rule replaces
them with three states and three timeouts.

---

## 3. Layer map

### 3.1 The stack

```
        ┌──────────────────────────────────────────────┐
  app   │  apps/*  — one flashable image per milestone │
        ├──────────────────────────────────────────────┤
        │  proto/  link state machine, role election   │   core 0
        │  record/ vCard codec, fragmentation, store   │   slower than 2 kHz
        │  link/   framing, CRC, Manchester            │
        ├──────────────────────────────────────────────┤
        │  dsp/    Goertzel, symbol sync, carrier det. │   core 1
        │                                              │   2 kHz and faster
        ├──────────────────────────────────────────────┤
   HAL  │  hal/    the ONLY hardware-aware layer       │
        └──────────────────────────────────────────────┘
             │                              │
      hal_pico/ (RP2350)          hal_host/ (simulator)
```

### 3.2 The dependency rule

| Layer | May include | Must never include |
|---|---|---|
| `dsp`, `link`, `record`, `proto` | C stdlib, `hal/hal.h`, each other downward | any `pico/*`, `hardware/*`, `btstack*` |
| `hal_pico` | everything SDK | `apps/*` |
| `apps` | everything | — |

Enforced two ways, both in CI:

1. Those four directories are compiled with host `gcc -Wall -Werror` in
   `test/host`. An SDK include fails the build immediately.
2. A grep check rejects `#include "pico/`, `hardware/` or `btstack` under
   `lib/{dsp,link,record,proto}`.

This is the load-bearing constraint of the whole project. Everything in the
development plan that claims "hardware: none" depends on it holding.

### 3.3 The core-split rule

> **Core 1 owns everything at 2 kHz and above. Core 0 owns everything below.**

Core 1: ADC DMA servicing, Goertzel (10 kHz windows), symbol sync, carrier
detection. Output is a chip-energy stream at the chip rate.

Core 0: Manchester decision, framing, CRC, fragment reassembly, vCard codec,
link state machine, BLE, USB, telemetry.

Transmit does not involve core 1 at all: core 0 fills a chip buffer, DMA clocks
it into the PIO, PIO gates the carrier. TX is essentially free.

---

## 4. Directory tree

Complete, and amended once — M2 added `link/chunk.h/.c`, and §13.2 records
why. Files marked ○ were stubs at M1.

```
firmware/
  apps/
    blink/            M0   board + toolchain + USB console          [done]
    txgen/            M3   carrier generation, PIO self-measurement
    adcbench/         M4   500 ksps DMA ring, Goertzel timing budget
    loopback/         M5   whole link inside one chip
    linktest/         M6   one-way link, role fixed at boot
    afe_sweep/        M7   analogue front-end characterisation
    handoff/          M14  the real thing
  lib/
    dsp/
      goertzel.h/.c        samples  -> scores          (10 kHz)
      sync.h/.c            scores   -> chip energies   (chip rate)
      carrier.h/.c         running energy, carrier-present flag
    link/
      manchester.h/.c      chip energies <-> bits
      frame.h/.c           preamble, marker, header, CRC
      crc.h/.c             CRC-16
      chunk.h/.c           seq|total framing for the PHONE link  (§11.2)
    record/
      vcard.h/.c           vCard text <-> field list
      compact.h/.c         field list <-> compact TLV        (§8.2)
      frag.h/.c            TLV blob <-> fragments            (§8.3)
      store.h/.c           own record in flash, provisioning (§9)
    proto/
      link_sm.h/.c       ○ half-duplex link state machine    (§7)
      beacon.h/.c        ○ the contact trigger                (§7.6)
      carousel.h/.c      ○ which fragment to send next       (§8.4)
    hal/
      hal.h                the seam. interface only, no code (§5)
      config.h             every milestone-varying constant  (§10)
    hal_pico/
      hal_pico.c           binds hal.h to RP2350
      pio_carrier.c/.pio   carrier generation + gating
      adc_ring.c           free-running ADC -> DMA ring
      ipc.c                lock-free SPSC ring, core1 -> core0
      ble.c/.h             GATT service per §11.2
      ble_service.gatt     the attribute database, compiled by the SDK
      btstack_config.h     BTstack build configuration
      flash.c              record persistence
      tlm_usb.c            telemetry sink over USB CDC
      tlm_ble.c            telemetry sink over BLE
  test/
    host/
      hal_host.c           the channel simulator implements hal.h
      chan.c               attenuation, noise, offset, drift, dropout
      test_*.c             unit tests per module
      sim_twonode.c        two link_sm instances, one simulated channel
    vectors/
      generated/           from tools/gen_vectors.py
      captures/            from hardware, replayed forever
tools/
  gen_vectors.py           waveforms from the spec, independent of the C
  plot.py                  score / raw-ADC plotter
  replay.py                hardware capture -> host decoder
  vcf.py                   reference vCard codec, cross-checks the C
android/                 native Android app (own project, not built by the Pico SDK)
  app/src/main/…/ble/    Gatt.kt, Chunk.kt, BandClient.kt, BandService.kt, Pairing.kt
  app/src/main/…/data/   Room database — the handshake history
  app/src/main/…/vcard/  vCard 3.0, the phone's half of the codec
  app/src/test/          Chunk.kt against the same cases as test_chunk.c
```

Files marked ○ above are the stubs that remain. M1 filled in `dsp`, `link`,
`record` and `proto`; M2 filled in `chunk`, `store`, `ble`, `flash`, `tlm_ble`
and the whole of `android/`.

---

## 5. The HAL seam

This is the single most important interface in the project. It is what allows
the link state machine — including role election — to be tested at M1 against a
simulated channel, and then run unmodified on hardware at M14.

```c
/* lib/hal/hal.h — implemented twice: hal_pico/ and test/host/ */

typedef enum { HAL_TLM_SCORE, HAL_TLM_RAW, HAL_TLM_EVENT } hal_tlm_kind_t;

typedef struct hal_iface {
    /* --- transmit --- */
    void     (*tx_drive)(bool on);        /* false = GP2 high-Z, design §6.3   */
    void     (*tx_chips)(const uint8_t *chips, size_t n);
    bool     (*tx_busy)(void);

    /* --- receive --- */
    size_t   (*rx_chips)(uint16_t *dst, size_t max);  /* chip energies         */
    uint32_t (*rx_carrier_level)(void);   /* for listen-before-talk, §7.3      */

    /* --- time --- */
    uint64_t (*now_us)(void);             /* virtual under test                */

    /* --- out-of-band --- */
    void     (*telemetry)(hal_tlm_kind_t, const void *, size_t);
    void     (*random)(void *dst, size_t n);   /* election backoff draw        */
} hal_iface_t;
```

Notes that matter:

- **`now_us` is virtual under test.** The two-node simulator advances time by
  hand, so a 5 ms backoff and a 350 ms frame cost no wall-clock time and role
  election tests run thousands of handshakes per second.
- **`random` is injectable**, so election tie cases can be *forced* rather than
  waited for. §7.3's tie-and-redraw path is otherwise nearly untestable.
- **`rx_chips` returns chip energies, not samples.** That is the core-1/core-0
  boundary from §3.3, and it is also the layer the host simulator injects at for
  protocol tests. DSP tests inject one layer lower, at raw samples.

---

## 6. Data flow

### 6.1 Receive

```
  pad ─► AFE ─► ADC0 ─► DMA ring ─────────────────────────┐  core 1
                        2 × 2048 × u16                    │
                              │                           │
                        goertzel.c   N samples -> score   │  10 kHz
                              │                           │
                        sync.c       5 scores -> chip     │  2 kHz
                              │      + timing recovery    │
        ══════════════════ ipc.c SPSC ring ═══════════════╪══ core boundary
                              │                           │
                        manchester.c chip pair -> bit     │  1 kHz   core 0
                              │                           │
                        frame.c      preamble, marker,    │
                              │      header, CRC-16       │
                        frag.c       reassembly bitmap    │
                              │                           │
                        compact.c    TLV -> field list    │
                              │                           │
                        vcard.c      field list -> .vcf   │
                              │                           │
                        ble.c        notify to phone      ▼
```

### 6.2 Transmit

```
  store.c  own vCard from flash
      │
  compact.c   field list -> TLV blob
      │
  frag.c      TLV -> fragments
      │
  carousel.c  which fragment next (§8.4)
      │
  frame.c     preamble + marker + header + payload + CRC
      │
  manchester.c bits -> chips
      │
  DMA ─► PIO ─► GP2 ─► R1 ─► pad          core 1 not involved
```

The PIO stream carries two bits per half-period, the pad level and the pad
direction: a mark chip is a driven square, a space chip is a released pad
(design §9.8, M13). `hal_tx_drive()` is the frame-level switch; the
chip-level one is in the data, so the CPU never touches the running state
machine and the release is slot-exact. GP2's input buffer is off for good in
`hal_pico` (RP2350-E9 cannot latch a released pad without it); the M3
instruments turn it on for their own measurements.

---

## 7. Link state machine

`lib/proto/link_sm.c`. A pure function of `(state, event, now_us)` producing
actions. No blocking, no sleeping, no hardware access except through `hal_iface_t`.

### 7.1 States

| State | Meaning | Exits on |
|---|---|---|
| `IDLE` | no contact: running the trigger, §7.6 | trigger says send → `TX_FRAME`; trigger says receive → `RX_FRAME` |
| `TX_FRAME` | clocking chips out | DMA complete |
| `TURNAROUND` | amplifier recovering, §9.7 | settling timer (1 ms budget) |
| `RX_FRAME` | receiving | frame CRC pass/fail, or timeout |
| `EXCHANGE` | carousel running, both directions | record complete both ways |
| `COMPLETE` | done, notify phone | — |
| `ABORT` | contact lost / unrecoverable | — |

`SENDER` and `RECEIVER` are a role flag, not states — they select which
transitions the exchange takes, so the same state graph serves both ends. They
are **not an election result**: the trigger hands them out, and §7.6 is the
argument that exactly one band can get `SENDER`.

`BACKOFF` and `LISTEN` are gone. They were the two halves of the role election
that `IDLE` used to exit into, and there is no election any more. Two receive
turns with nothing heard put the exchange back into `IDLE` — keeping the
half-built record and the contact budget — with a bounded retry count.

### 7.6 What takes a band out of `IDLE`

`lib/proto/beacon.c` — the filename is historical; the module is the trigger.
This section is the specification, the summary and the divergence record.

An earlier draft of §7.1 said `IDLE` exits on "carrier detected, or host says
go", which is not a mechanism: if every band waits to hear a carrier, no band
emits one and no handshake ever starts. The board has no button, no
accelerometer and no touch sensor — design §6 gives it a drive electrode, a
sense electrode and an ADC — so the trigger has to be built out of the link
itself.

It can be. **Before two wearers touch there is no channel at all**: a band's
transmission is simply inaudible to the other. The instant skin meets skin a
channel exists. So a transmission *being heard* is itself the contact signal,
and no separate sensor is needed. The channel is the sensor.

Every band free-runs this loop, unsynchronised with any other:

| phase | length | ears |
|---|---|---|
| `SHOUT` | 10 ms of flat carrier | deaf — own amplifier driving |
| `SETTLE` | 1 ms | deaf — own amplifier recovering |
| `LISTEN` | 50–100 ms, drawn per cycle | open, continuously |

While listening, anything heard is one of exactly two things. A flat carrier
the framer never locks to is somebody's shout: wait for silence, then send our
card. An alternating preamble the framer does lock to is a card already
arriving: receive it. Nothing at all for the whole drawn window means nobody is
there: shout again. **The listen timer counts silent time only** — it is held
while a carrier is present, or a band would shout over a card in flight.

**There is nothing to elect, and this is the load-bearing claim.** To hear the
other band's shout you must open your ears before their shout ends, and your
ears open 11 ms after your own shout began. For shouts at `t_A < t_B`: B's ears
are already open at `t_A`, so B always hears A; A's ears open at `t_A + 11`, by
which time A's own shout is long over, and A hears B only if B is still
shouting — which needs `t_B > t_A + 2`, but a B that heard A has already
stopped shouting. The earlier shouter is always too late and the later one is
always in time, so **at most one band can hear the other's shout**. Both-send
and both-listen are unreachable, not merely unlikely.

That is timing algebra, not a machine-checked proof, so `test_beacon.c` sweeps
the relative phase across a whole worst-case cycle at 1 ms resolution and
asserts on every one of the 112 offsets both that the pair rendezvous and that
exactly one end comes out a sender. Measured: every offset rendezvous, worst
case 108 ms, none needing more than one cycle.

The one degenerate case is two shouts within about a detector latency of each
other, where neither hears. Both draw a fresh listen window and whichever
shouts first next round is heard, because the other is listening continuously.
Forced over 400 seeds, **398 resolve within two rounds and the worst case is
three** — which replaces an arithmetic estimate of ~4 % per round with a
measured 0.5 % needing a third.

Three timeouts are the entire error handling. A carrier that never stops is
capped at 30 ms, after which the band resets its carrier detector (the ambient
floor may genuinely have moved), returns to listening, and **does not send** —
transmitting into a channel that is provably busy is worse than losing a cycle.
Contact lost mid-card keeps the partial record, per §8.4. Two barren receive
turns drop back into the trigger, which is the one thing in the system that can
tell the two ends apart.

**Two decisions re-examined and kept.** The shout is a **flat carrier, not a
preamble**: an alternating burst *is* a preamble under §8.3, and would drag
every listening band's framer into a half-locked state ten times a second, with
a decaying tail able to supply a false marker. A flat carrier has no
transitions at all, so it is unambiguous by construction rather than by
timeout. And the shout stays **short rather than sending the card blind**: a
156 ms transmission every cycle makes a band deaf most of the time instead of a
small fraction of it, and the proof above depends on a short deaf window.

**Power is explicitly not a concern for v1.** The duty-cycled sniffing this
replaces was estimated from datasheets at ~3.9 mA against ~10.8 mA — roughly
85 hours of idle against 39 on a 500 mAh cell — and that estimate has never
been measured. Always-on listening is a deliberate bench decision rather than
an oversight: the no-tie proof depends on there being no deaf gap outside a
band's own shout. Duty-cycled receiving can be reintroduced later without
changing the rule.

### 7.2 Why this is a table, not `if` statements

The transition table is data. `sim_twonode.c` runs two instances against a
simulated channel with injected time and injected randomness, which makes the
following testable at M1, with no hardware:

- contact breaks mid-exchange and both ends return to `IDLE` cleanly
- a frame is lost during turnaround and the carousel recovers
- one end is reset mid-exchange while the other keeps talking
- an exchange falls back into the trigger and keeps the record it had

The first case this list used to carry — both ends drawing the same backoff,
redrawing, and converging — no longer exists to test. `test_beacon.c` tests the
thing that replaced it, and the two are not the same shape: an election tie is
a probability to be bounded, whereas §7.6's claim is that a tie is unreachable,
which is a sweep rather than a sample.

### 7.3 Role election — removed

There is no role election. It was `lib/proto/elect.c`, per design §9.6: draw
0–5 ms, listen, carrier heard means target and silence means initiator, ties
broken by redraw. §7.6 replaces the whole of it — the trigger's timing geometry
decides who sends, so there is nothing left to arbitrate and no tie to break.
The module, its tests and `HANDOFF_BACKOFF_MAX_US` are deleted.

Design §9.6's rejection of burned-in priority IDs still stands, and for the same
reason: body coupling has no dominant/recessive state, so there is no collision
detection to arbitrate with.

---

## 8. Payload architecture

The largest change from the design document. §9.4's single 40-byte packet is
replaced by a four-stage pipeline.

### 8.1 Why a vCard cannot go on the wire literally

A realistic vCard — name, mobile, email, org, title — is about 169 bytes:

| Encoding | Size | Frames | One full pass @ 1000 bps | @ 2000 bps |
|---|---|---|---|---|
| Raw vCard text | 169 B | 6 | **1872 ms** | 936 ms |
| Compact TLV | 79 B | 3 | 936 ms | **468 ms** |

Contact lasts about one second (R1). Raw text does not fit — not even one pass,
let alone the repetition §9.5 relies on. So the wire carries a compact encoding
and **the full RFC-compliant vCard is reconstructed at the receiving end**. The
user still gets a real `.vcf`; only the boilerplate stops crossing the body.

This also makes the case for `HANDOFF_GZ_N = 25` (development plan M1, open
item 2) much stronger: 3 dB of processing gain buys a 2× data rate, which is the
difference between one pass and two inside a handshake. Decide it with the
simulator, but expect to land on 25.

### 8.2 Compact TLV — `lib/record/compact.c`

`tag(1) | len(1) | value(len)`. Three tricks, all reversible:

| Trick | Saves | How |
|---|---|---|
| Drop boilerplate | ~37 B | `BEGIN`/`VERSION`/`END` are implied by the tag set |
| Property names → tags | ~35 B | `TEL;TYPE=CELL:` → `0x0B 0x01` |
| Domain dictionary | ~10 B | `gmail.com`, `outlook.com`, `icloud.com`… → 1 byte |
| Phone as packed BCD | ~6 B | 12 digits → 6 bytes + country code |

Tag registry:

| Tag | Property | Value encoding |
|---|---|---|
| `0x01` | `FN` | UTF-8 |
| `0x02` | `N` | UTF-8, `;` separated |
| `0x03` | `TEL;CELL` | u16 country + packed BCD — **retired, decoded forever** |
| `0x04` | `TEL;WORK` | u16 country + packed BCD — **retired, decoded forever** |
| `0x05` | `EMAIL` | UTF-8 local part + 1-byte domain id (`0xFF` = literal) |
| `0x06` | `ORG` | UTF-8 |
| `0x07` | `TITLE` | UTF-8 |
| `0x08` | `URL` | UTF-8 |
| `0x09` | `ADR` | UTF-8, `;` separated |
| `0x0A` | `NOTE` | UTF-8 |
| `0x0B` | `TEL` | label byte + u16 country + packed BCD |
| `0xFF` | **raw vCard line** | UTF-8, verbatim |

A phone is a number AND what it is for, so `0x0B` carries the label in the
value rather than in the tag:

```
label(1) | [ len(1) | UTF-8 label text ]   <- the bracket only when CUSTOM
         | u16 country code BE | packed BCD
```

| Label | Byte | vCard |
|---|---|---|
| Mobile | `0x01` | `TEL;TYPE=CELL` |
| Work | `0x02` | `TEL;TYPE=WORK` |
| Home | `0x03` | `TEL;TYPE=HOME` |
| Main | `0x04` | `TEL;TYPE=MAIN` |
| Custom | `0xFF` | `TEL;TYPE=X-<label>` |
| None | `0x00` | bare `TEL` |

One tag per phone rather than one tag per label is what lets a card carry the
same label twice — two mobiles is a real thing a person has — and a label the
wearer typed themselves. The whole phone is ONE TLV, so `frag.c` either places
all of it in a fragment or none of it, and a number can never arrive wearing
the wrong label. `TEL_LABEL_NONE` is a real state, not a default: a `TEL` that
named no type is not a mobile, and saying it is prints a word under somebody's
number that nobody ever said.

Every phone would otherwise claim priority 1 and three numbers would push
`EMAIL` out of fragment 0, so only the FIRST takes that slot (`tel_rank` in
`compact.c`); the rest take the place `TEL;WORK` used to hold, in the order the
card lists them. `0x03` and `0x04` are never encoded again, and are decoded
forever — as Mobile and Work, which is what they always meant — because bands
flashed before this change still send them.

`0xFF` is the escape hatch and it is what makes the claim "we send a full vCard"
literally true: any property not in the registry crosses verbatim, at full cost.
Nothing is silently dropped. `PHOTO` is the one exception — it is rejected at
encode time with an explicit error rather than being allowed to occupy nine
seconds of airtime.

**All lengths are bytes, not characters.** Names here are not ASCII; the codec is
UTF-8 throughout and `len` never splits a multi-byte sequence.

### 8.3 Fragmentation — `lib/record/frag.c`

Frame layout, replacing §9.4:

| Field | Length | Content |
|---|---|---|
| Preamble | 32 chips | alternating, §9.4 |
| Start marker | 8 bits | `11110000` |
| **Header** | **2 bytes** | frag index (4b), frag count (4b), record id (6b), flags (2b) |
| Payload | ≤ 32 bytes | slice of the TLV blob |
| **Checksum** | **2 bytes** | **CRC-16** |

Header per fragment costs 16 ms and buys: reassembly without ordering
assumptions, a `record id` so a re-provisioned card does not merge with a stale
one, and a `frag count` so the receiver knows when it is done without a length
prefix that could itself be corrupted.

CRC-16 rather than §9.4's CRC-8, per development plan M1 open item 3 — now with
more force, because a fragmented transfer runs the CRC 3–6× as often per contact.

### 8.4 Priority-weighted carousel — `lib/proto/carousel.c`

The transfer strategy, replacing §9.5's "send the packet repeatedly".

Fragments are ordered by **importance, not by offset**. Fragment 0 holds `FN` and
`TEL;CELL` — 24 bytes, **248 ms**, a usable contact on its own. Later fragments
enrich it.

Transmission order is `0, 1, 0, 2, 0, 3, 0, 1, …` — fragment 0 gets roughly half
the airtime, later fragments share the rest. The receiver keeps a bitmap and
assembles whatever passed CRC.

The consequences are worth stating, because this is what makes the vCard
requirement survive contact with physics:

| Contact duration | Outcome |
|---|---|
| ~250 ms | name + mobile — a usable contact |
| ~500 ms | + email |
| ~1 s | typically the whole card |
| shorter | nothing, CRC rejects — never a corrupted contact |

**Graceful degradation is the point.** A brief handshake yields a real if sparse
contact rather than a failed transfer. It also keeps §9.5's best property: no
acknowledgement, no retry negotiation, no timers in the one-way case.

### 8.5 Reconstruction

`vcard.c` on the receiving side rebuilds a well-formed vCard 3.0 from whatever
fragments arrived, synthesising `BEGIN`/`VERSION`/`END` and deriving `N` from
`FN` when `N` did not make it. `tools/vcf.py` is an independent reference
implementation of the same codec, and M1 cross-checks C against Python in both
directions — the same independent-generator discipline the development plan
applies to the modulation.

---

## 9. Provisioning and storage

The design document never says how a wristband learns its own contact details.
It needs a path, and it is the reverse of the delivery path:

```
  phone ──BLE write──► ble.c ──► vcard.c ──► compact.c ──► store.c ──► flash
```

`lib/record/store.c` holds one compact TLV blob plus a record id in the last
flash sector, survives power cycles, and is re-writable from the phone. The
wristband never stores vCard *text* — only the compact form it will transmit, so
encoding cost is paid once at provisioning rather than at every handshake.

---

## 10. Configuration

`lib/hal/config.h`. Every constant the development plan varies, in one file, so
that M3→M10's carrier change and M1's N decision are edits to a constant rather
than to code:

```c
#define HANDOFF_CARRIER_HZ        200000  /* 40000 during M3/M10 bring-up   */
#define HANDOFF_ADC_FS_HZ         500000
#define HANDOFF_GZ_N              25      /* M1 decides: 50 or 25, see §8.1 */
#define HANDOFF_WINDOWS_PER_CHIP  5
#define HANDOFF_FRAG_PAYLOAD      32
#define HANDOFF_TURNAROUND_US     1000    /* §9.7, measured at M8           */
```

Derived rates are `static_assert`ed, not commented — if `HANDOFF_CARRIER_HZ` is
set to something that is not an exact PIO divider or not on a Goertzel bin
centre, the build fails rather than the link quietly degrading.

`HANDOFF_BACKOFF_MAX_US` was here and is deleted with the election (§7.3). The
trigger's own constants — the shout, the listen draw range, the quiet-wait cap
— live in `lib/proto/beacon.h` next to the assertions that bound them, because
each one is a property of the link rather than a milestone knob.

---

## 11. The phone client

**Settled: a native Android app. No web client, no iOS.**

The firmware is insulated from this regardless — it exposes the §11.2 BLE GATT
service and nothing above it — but the client is decided, and the milestones
build against it.

### 11.1 Why native, not Web Bluetooth

The phone is in the wearer's pocket during a handshake. Catching the `rx_vcard`
notify with the screen off needs a background BLE connection, and a backgrounded
browser tab drops the GATT link — Web Bluetooth structurally cannot do the one
thing the product requires. Native also brings a real local database for
handshake history, OS-level device bonding, and the system contact picker.

Rejected with it:

- **Web Bluetooth** — Chrome-on-Android only, never iOS, no background
  connection, a device-chooser gesture every session, persistence limited to
  fragile per-origin IndexedDB.
- **A web client alongside the app** — two clients to maintain for no gain once
  native does the real work.
- **iOS** — out of scope (design §2); Web Bluetooth never existed there either,
  so nothing was lost by not targeting it.

The Android app is a normal project at `android/`, outside the firmware tree; the
Pico SDK toolchain does not build it.

### 11.2 The BLE contract

Implemented at M2. The authoritative copy is
`firmware/lib/hal_pico/ble_service.gatt`, which the SDK compiles into the
attribute database; `android/…/ble/Gatt.kt` is the other side of the same
contract, and a UUID changed in one place and not the other does not fail
loudly — the app connects, finds nothing it recognises, and reports nothing.

| Characteristic | UUID `48414e44-xxxx-4f46-9b2c-1e0a7d3f5c81` | Access | Purpose |
|---|---|---|---|
| `my_vcard`  | `0002` | write, chunked, **encrypted** | phone provisions the wristband (§9) |
| `rx_vcard`  | `0003` | notify, chunked, **encrypted** | received contact, reconstructed vCard text |
| `status`    | `0004` | read + notify | link state, last score, fragment bitmap, errors |
| `telemetry` | `0005` | notify | decimated score stream for §14.1 body tests |
| `control`   | `0006` | write | carrier select, trigger raw capture, force role |

The service UUID is `0001` of the same base, and it is in the **advertisement**
rather than the scan response: `CompanionDeviceManager` filters on what is
advertised, and a band that only answers an active scan never appears in the
pairing chooser. That costs 18 of the 31 advertising bytes, so the device name
goes in the scan response instead.

Chunking: ATT MTU is not guaranteed above 23 bytes, so every chunked
characteristic carries a 2-byte `seq | total` header and reassembles at the far
end. The framing is `lib/link/chunk.c` — see §13.2 — and every chunk but the
last carries a full payload, which is what lets the receiver derive the
capacity from chunk 0 and never be told the MTU at all.

`my_vcard` and `rx_vcard` require a bonded, encrypted link, so a bystander cannot
read the wearer's card or inject one. The bond is established once through
`CompanionDeviceManager` (§11.3) and the app reconnects on it automatically
afterwards. Pairing is LE Secure Connections, Just Works — the wristband has no
display and no keypad, so there is no method available that authenticates the
peer, and the bond is protected against passive eavesdropping but not against a
man in the middle *at the moment of pairing*. That is one dialog in a device's
life, in the wearer's own hand.

`status`, `telemetry` and `control` are deliberately **not** encrypted. They
carry no identity — a link state, a score, a carrier selection — and leaving
them open means a bench session or a §14.1 body test needs no pairing dance
before it can see anything. The two `control` opcodes that touch identity
(`FAKE_RX`, `FORGET`) are refused on an unencrypted link in `ble.c` rather than
by the attribute permissions.

`telemetry` exists because design §13 forbids a mains-tethered USB laptop while
anyone is touching an electrode — during body tests BLE is the *only* legal way
data leaves the wristband. Development plan M4 sizes it: the decimated score
stream, not raw ADC.

### 11.3 App responsibilities

The firmware owns none of this; it is recorded here so the milestones and the
`android/` project agree on scope.

**Pairing.** First run: the app scans for the Handoff service UUID and pairs with
one wristband through `CompanionDeviceManager` — a one-time OS dialog, a stored
BLE bond, automatic reconnection thereafter. A foreground service holds the
connection while the phone is pocketed.

**Provisioning the wearer's own card (§9).** The app builds vCard 3.0 text and
writes it to `my_vcard`. The source is one of:

- **A contact chosen from the phone's address book** — `ACTION_PICK` on
  `ContactsContract`, then a full-field read (`READ_CONTACTS`). The app shows a
  preview with per-field toggles before sending; `PHOTO` is stripped (§8.2
  rejects it at encode time). The band holds only a snapshot, so the app records
  "based on contact X, last synced <when>" and offers a re-push when that
  contact changes.
- **Manual entry** — a form, for a purpose-built card not derived from a contact.

**Receiving a card.** Every card arriving on `rx_vcard` is written to the app's
own local database — always, no permission needed. This is the handshake history:
one row per handshake with timestamp, parsed fields, the raw reconstructed vCard,
and a completeness badge (name + mobile only for a ~250 ms contact, the full card
for ~1 s — design §9.5, architecture §8.4).

**Promoting to the system address book is an explicit user action**, never
automatic:

- Default: a `ContactsContract.Intents.Insert` intent opens the system
  new-contact editor prefilled — the user confirms, no permission required.
- Optional setting, off by default: "auto-save new handshakes to Contacts",
  which switches to a silent `ContentProvider` insert and requests
  `WRITE_CONTACTS` at that point.

Auto-save is off by default deliberately: a conference handshake is not always
someone the wearer wants permanently in an account that syncs to every device,
and a partial card auto-committed as a real contact is the "visibly wrong name in
someone's address book" failure that CRC-16 (§8.3) exists to prevent.

**Browsing.** A list screen backed by the local database — name, time,
completeness badge, search, sort by date. Tap through to a detail screen: the
full card, "add to contacts", "message", "delete". Any "met at" label is
something the phone adds from its own clock and location; nothing about it
crosses the body.

---

## 12. Milestone → module map

What each milestone fills in. Nothing here creates a file that §4 does not
already list.

| Milestone | Implements | Stubs it leaves alone |
|---|---|---|
| M1 | `dsp/*`, `link/*`, `record/vcard`, `record/compact`, `record/frag`, `proto/*`, `test/host/*`, `tools/gen_vectors`, `tools/vcf` | everything under `hal_pico/` |
| M2 | `hal_pico/ble.c`, `record/store.c`, `hal_pico/flash.c`, `hal_pico/tlm_ble.c`, **`link/chunk.c` (§13.2)**, `android/` app | link, dsp |
| M3 | `hal_pico/pio_carrier.c` | adc, ipc |
| M4 | `hal_pico/adc_ring.c`, `ipc.c`, `tlm_usb.c` | ble |
| M5 | `hal_pico/hal_pico.c` complete — first time the real HAL binds | — |
| M6 | nothing new; `apps/linktest` composes existing modules | — |
| M7 | `apps/afe_sweep` only | — |
| M8–M11 | nothing new; constants in `config.h`, plots in `tools/plot.py` | — |
| M12 | wires `rx_vcard` notify to the real receive path | — |
| M13 | `link_sm.c` turnaround transitions — header already exists from M1 | — |
| M14 | `beacon.c` binding only; logic was written and tested at M1. **Done 15 Sep 2026 on the passive divider**: `apps/handoff` polls `link_sm`; `hal_pico.c` cuts a board's own sends out of the chip stream on the sample clock and registers core 1 as a flash-lockout victim; the two latencies the link must allow for are named in `hal_pico.h` | — |

M6 through M11 add **no new firmware modules at all**. That is the intended
result: six milestones of measurement against code that already exists. If a
milestone in that range needs a new file, this architecture was wrong and gets
revised here first.

---

## 13. Open items

| Item | Owner | Closes at |
|---|---|---|
| Phone client | §11 — **settled: native Android app** | — |
| `HANDOFF_GZ_N` 50 vs 25 | **settled: 25.** N=50's waterfall is ~2 dB lower; N=25 halves the frame to 156 ms. Six frames per second of contact against three, and the link budget has decibels to spare and no milliseconds | M1 ✅ |
| Tag registry final list | **settled**, plus `0xFE` — see §8.2 | M1 ✅ |
| Domain dictionary contents | **settled: 16 domains**, append-only, ids below `0x20` | M1 ✅ |
| Fragment payload size 32 B | **kept at 32 B.** 40 B shortens a full pass slightly but lengthens the frame that has to survive; re-measure at M5 against real losses | M1 ✅ |
| Carousel weighting `0,1,0,2,…` | **settled: plain round robin, weight 0** — the sweep contradicted §8.4, see below | M1 ✅ |
| `PHOTO` handling | §8.2 — rejected at encode, with an explicit error | when someone asks |
| Turnaround real settling time | §7.1 — 1 ms is a budget, not a measurement | M8 |
| Contact trigger power | §7.6 — **v1 listens continuously, deliberately.** The ~2.2× saving from duty-cycled sniffing is a datasheet estimate that has never been measured, and always-on is what makes the no-tie proof hold | M8 / when a battery matters |
| Two-way exchange sequencing | **implemented and tested at M1** against two simulated nodes; only the hardware binding is left | M1 / M14 |
| Phone-link chunk framing | **settled at M2**: `lib/link/chunk.c`, 2-byte `seq \| total`, every chunk but the last payload-full. Host-tested at every capacity from the ATT floor to 244 | M2 ✅ |
| Record sector placement | **settled at M2: NOT the last sector** — the SDK reserves it on RP2350 for the E10 workaround and BTstack's bond bank takes the two below it. `flash.c` static-asserts against `PICO_FLASH_BANK_STORAGE_OFFSET` | M2 ✅ |
| Pairing method | **settled at M2: LE Secure Connections, Just Works, bonded.** No display, no keypad, so nothing stronger exists. See §11.2 | M2 ✅ |
| Where the record store's backend lives | **settled at M2: registered from below.** `hal_pico/flash.c` calls `store_set_backend()`; `store.c` names no transport and stays host-testable | M2 ✅ |
| Receive-path latency | **found at M14.** A chip surfaces up to one DMA block (4 ms) after it was sampled. The HAL now hides a board's own sends on the sample clock, and `apps/handoff` widens `rx_idle_us` by two blocks — but the trigger's effective deaf window is ~9 ms rather than ~1 ms, and one rendezvous in three needs a second shout round against the simulator's one in two hundred. A smaller `ADC_RING_BLOCK` shrinks it; M4's overrun soak would have to be re-run | when rendezvous time matters |
| Frame packing time | **found at M14.** `pio_carrier_send` writes every stream bit before the DMA starts: 5.4–6.2 ms per frame, measured. Budgeted as `HAL_PICO_TX_SETUP_US` and added to `rx_idle_us`; a packer that copied a precomputed chip pattern would make it near zero and take 7 ms off every turn | when a turn is worth 7 ms |

### 13.1 What M1 changed in this document

Recorded here rather than quietly, because §1's rule cuts both ways: a
milestone may not invent a seam, but a measurement may overrule a guess.

**The carousel weighting was wrong.** §8.4 specified `0, 1, 0, 2, …`, giving
fragment 0 half the airtime. Measured, that is the worst option available:

| weight | order | fields @250 ms | @1 s | complete @1 s |
|---|---|---|---|---|
| **0** | **round robin** | **1.5** | **5.0** | **50 %** |
| 1 | `0,1,0,2` (§8.4) | 1.5 | 4.5 | 0 % |
| 3 | `0,0,0,1` | 1.5 | 3.5 | 0 % |

Re-measured after §13.3 removed the election from the two-node simulator's
start-up; the columns moved by a tenth or so and the conclusion did not.

### 13.2 What M2 changed in this document

**A new module, `lib/link/chunk.h/.c`.** §1 says a milestone may not invent a
seam and that discovering one is an architecture bug to be fixed here first.
This is that fix, recorded rather than done quietly.

The 2-byte `seq | total` framing was specified in §11.2 and left as a detail of
`ble.c`. That was wrong, for the reason this whole architecture exists:
`hal_pico/ble.c` cannot be compiled by the host build, so framing that lives
inside it cannot be tested without a board and a phone — and development plan
M2 makes "chunked reassembly works at the 23-byte ATT MTU floor, not just at
whatever MTU your phone happens to negotiate" an exit criterion precisely
because that is the case which passes on the developer's own handset and fails
on someone else's. A seam that puts the framing in `link/` and leaves `ble.c`
binding it to ATT makes the floor testable in CI, at every capacity from 20 to
244 bytes, in a couple of milliseconds.

It sits in `link/` and not in a new directory because it *is* link framing,
just not of the body link. `frame.c` frames what crosses skin; `chunk.c` frames
what crosses BLE. They share a directory and nothing else, and both header
comments say so.

**`store.c`'s persistence backend is registered from below.** §9's diagram has
the arrow pointing `store.c → flash`, which read as a call. It cannot be one:
`record/` is inside §3.2's sandbox and may not name `hal_pico`. So
`hal_pico/flash.c` calls `store_set_backend()` at start-up and `store.c` sees
three function pointers. The dependency still runs the direction §9 draws — it
is only inverted at the language level — and the side effect is that the
persistence logic is host-tested against a fake backend, which is where the
record-id-across-a-power-cycle test lives.

**M2 adds no app, and §4's app list stands.** The phone link is not a bring-up
instrument like `txgen` or `afe_sweep`; it is half of the finished device, and
development plan M12 says so outright — "M2's fake record is replaced by the
real received one". So `apps/handoff` is M2's image with a hardcoded card where
the body link will later be, and M12 replaces the constant.

**The build gained a second library, and it is not a seam in the source.**
`handoff_ble` holds `ble.c`, `flash.c` and `tlm_ble.c` — all three still under
`hal_pico/` in §4. The split is a CMake fact, not an architectural one:
`CYW43_ENABLE_BLUETOOTH` has to be PUBLIC for the cyw43 driver compiled into
each executable to agree with it, and PUBLIC on `handoff_lib` measurably put
92 KB of Bluetooth firmware into `apps/blink`, which is M0's image. It would
also have put a Bluetooth stack inside `apps/adcbench`, whose entire job at M4
is to measure how much of core 1 is left.

§8.4 assumed frames were cheap enough to spend half of them on repetition. At
156 ms a frame, a one-second contact carries about six frames in total, and
three spent re-sending what the far end already has are three fragments it
never receives. What actually delivers §8.4's promise — a usable contact from a
brief touch — is the **priority ordering**, not the repetition: `FN` and the
mobile are in fragment 0, and round robin still sends fragment 0 first. The
weighting stays a parameter, because M5 and M6 re-measure it against a channel
that loses frames for real.

**`hal_iface_t` gained a context pointer.** Every function takes `void *ctx`
first. §5's literal signature cannot support two instances in one process,
which §7.2 requires — the two-node simulator is the whole reason the seam
exists.

**A flag bit was respent.** `FRAME_FLAG_LAST_PASS` ("I have sent everything")
became `FRAME_FLAG_HAVE_YOURS` ("I have *your* whole record"). Without it the
first end to be satisfied simply stops transmitting and strands the other one a
fragment short — which cost about a third of all handshakes in the simulator
before it was found. §8.3's header is unchanged in size and layout.

**The frame carries no length field**, and short fragments are padded with TLV
tag `0x00`, which the decoder skips. This follows §8.3's own argument against a
length prefix that could itself be corrupted.

**Tag `0xFE` was added: a continuation.** A value longer than a fragment is
split into a head TLV under its own tag followed by `0xFE` chunks. This is what
lets `frag.c` refuse to cut a TLV in half, so every fragment holds whole TLVs
and decodes on its own — without which §8.4's degradation table is not true.

**The preamble detector counts transitions rather than requiring a run.** A
perfect-run rule of length *N* only tolerates a corrupted chip in the first
`40 − N` of the 40 alternating chips; at *N* = 24 that is the first 16, and the
other 24 positions lose the whole frame. Counting 22 of the last 24 transitions
tolerates one flipped chip anywhere. See `lib/link/frame.h`.

**`ELECT_MAX_REDRAWS` went from 8 to 16.** Design §9.6's 0–5 ms backoff against
a ~1 ms carrier-detect latency gives roughly a one-in-three collision on the
first attempt. Eight redraws leave a 1-in-10 000 handshake that never elects a
role — visible over a demo afternoon. Sixteen costs at most 112 ms.

### 13.3 What the simplified contact trigger changed in this document

Recorded here for the same reason as §13.1 and §13.2: the change deletes a
design-document requirement, and deleting one quietly is worse than having
implemented it wrong.

**The role election is gone**, and with it `lib/proto/elect.h/.c`,
`test_elect.c`, `HANDOFF_BACKOFF_MAX_US`, the `LINK_BACKOFF` and `LINK_LISTEN`
states, and the beacon's wake-role hint. Design §9.6 specified it and §7.3 used
to describe it. §7.6's trigger replaces it: the sender is decided by which band
could physically hear the other's shout, which is a property of the timing
rather than a draw, so there is no tie to break and nothing to redraw.

**Three claims in the trigger's original spec were arithmetic and are now
measured.** The spec was
explicit that they were estimates and should not be inherited as facts:

| claim | was | measured |
|---|---|---|
| repeat rate after a simultaneous shout | ~4 % per round, ~0.2 % over two | 0.5 % of 400 forced collisions needed a third round; worst case three |
| rendezvous latency | one cycle, by construction | worst 108 ms over all 112 phase offsets; none needed a second cycle |
| exactly one sender | timing algebra, unproven | holds at all 112 offsets and all 400 forced collisions |

The power comparison behind "listen continuously" — ~10.8 mA against ~3.9 mA,
~39 h against ~85 h on a 500 mAh cell — is **still** a datasheet estimate and
is still unmeasured. It is in §13's open items rather than presented as a
result.

**One thing the spec flagged as its main risk turned out to have a sharp edge
underneath it, and the edge has since been taken off.** §5.1 asked whether the
receive path should reset the carrier detector, and asked for a test rather than
an argument. End to end the two choices were bit-identical — same frames sent,
same turnarounds, same rendezvous — because handover during a receive turn
counts decoded frames and the framer is untouched either way. One layer down
they were not: `carrier.c` re-primed its level *and its floor* from the next
chip after a reset, and during a frame that chip is high half the time. Primed
on a high one the floor sat at the carrier's own level and could not fall back
inside the frame — measured, presence never returned across the remaining 624
chips. So the reset was a coin flip on blinding the thing that drives handover.

**§5.1 is now answered by making the question stop mattering.** The floor is a
property of the room, not of the state machine: `carrier_reset()` clears
presence and re-primes `level` alone, and only `carrier_reprime()` — which
§4.3's quiet-wait cap is the sole caller of — throws the floor away. Adding the
reset back to the receive path was then measured against the whole suite and
changed nothing at all: 0 failures either way, worst rendezvous 130900 us
either way, the same 399/400 on simultaneous starts. It is left out because it
buys nothing, not because it is dangerous. `test_beacon.c` now pins the four
properties the floor has to have instead of the asymmetry it used to have.
