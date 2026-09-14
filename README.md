# Handoff — Shake hands. Share contacts.

Two people wear a wristband. They shake hands. Contact details transfer between
the wristbands **through their bodies**, by capacitive coupling at 200 kHz.

The body is the transmission medium. The handshake closes the circuit; it does
not generate the signal.

Full electrical design, modulation, protocol, BOM and validation plan:
**[docs/body-coupled-handshake-design.md](docs/body-coupled-handshake-design.md)**

Build order, testable units and what each milestone proves:
**[docs/development-plan.md](docs/development-plan.md)**

Module tree, interfaces and the vCard payload design — settled up front so each
milestone fills a known slot:
**[docs/firmware-architecture.md](docs/firmware-architecture.md)**

---

## Hardware target

| | |
|---|---|
| MCU board | Raspberry Pi Pico 2 W (RP2350 + CYW43439) |
| Why RP2350 | RP2040 has a known ADC differential-non-linearity defect |
| Why the W | BLE is the link from wristband to the wearer's own phone |
| Analogue | MCP6292 dual rail-to-rail op-amp, ×121 in two stages |
| Power | 1S LiPo + TP4056. **Battery only, fully floating, both ends.** |

> **Safety is not optional.** 1 MΩ minimum series resistance on every electrode,
> insulated electrodes only, no mains-referenced ground anywhere near a person.
> Read §13 of the design doc before powering anything on a wrist.

---

## Toolchain

Official Raspberry Pi Pico SDK with CMake and Ninja — not PlatformIO. The
low-level work this project needs (PIO carrier generation, free-running ADC at
500 ksps into a DMA ring, core 1 running a Goertzel filter, BLE over BTstack) is
first-class in the SDK and second-class everywhere else.

Everything is pinned and lives under `~/.pico-sdk`, deliberately **not** on PATH:

| Component | Version |
|---|---|
| pico-sdk | 2.3.1 |
| ARM GCC | 15.2.Rel1 (`arm-none-eabi`, 15.2.1) |
| CMake | 4.3.4 |
| Ninja | 1.13.2 |
| picotool | 2.3.1 |

These versions appear in three places that must stay in step: the DO-NOT-EDIT
block in [CMakeLists.txt](CMakeLists.txt), the variables at the top of
[scripts/build.py](scripts/build.py), and the paths in
[.vscode/settings.json](.vscode/settings.json).

### First-time setup on a new machine

Install the **Raspberry Pi Pico** VS Code extension
(`raspberry-pi.raspberry-pi-pico`), open this folder, and let it download SDK
2.3.1 when prompted. It populates `~/.pico-sdk` with exactly the layout above.

---

## Build

**In VS Code** — `Ctrl+Shift+B`, or the Pico extension's *Compile Project*.

**From a terminal** — same command on Windows and Linux:

```
python scripts/build.py                    # configure + build
python scripts/build.py --clean            # wipe build/ first
python scripts/build.py --flash            # build, then flash over USB
python scripts/build.py --config Release
python scripts/build.py --target blink     # just one app
python scripts/build.py --help
```

Artifacts land in `build/` — `blink.uf2`, `.elf`, `.bin`, `.hex`, `.map`, `.dis`.

The script prefers the pinned toolchain under `~/.pico-sdk` and falls back to
whatever is on `PATH`, so a Linux box with distro `cmake`, `ninja` and
`arm-none-eabi-gcc` builds this without installing the VS Code extension's
bundle. Python 3.8+, standard library only.

## Flash

Either drag `build/blink.uf2` onto the `RP2350` drive that appears when you plug
the board in holding **BOOTSEL**, or:

```
python scripts/build.py --flash
```

`picotool ... -fx` reboots a running board into BOOTSEL by itself, so BOOTSEL is
only needed for the very first flash. On Linux, install picotool's udev rules
rather than running the flash step under `sudo`.

## Serial console

The firmware presents a USB CDC port. Any terminal at any baud rate works — it
is USB, so the rate is ignored:

```
# Windows: 'mode' lists COM ports; attach with PuTTY or Windows Terminal
# Linux:   /dev/ttyACM0; attach with 'screen /dev/ttyACM0' or 'picocom'
```

Later this same link carries raw ADC buffers to the host plotter (design §10.5).
The receiver is the test instrument; there is no oscilloscope in this project.

---

## Layout

```
CMakeLists.txt            top level; sets PICO_BOARD=pico2_w, defines handoff_add_app()
pico_sdk_import.cmake     stock SDK bootstrap, copied from pico-sdk 2.3.1

firmware/
  CMakeLists.txt          builds handoff_lib, then one image per app
  apps/                   one flashable image per milestone
    blink/                M0   board + toolchain + USB console          [done]
    txgen/                M3   carrier generation, PIO self-measurement
    adcbench/             M4   500 ksps DMA ring, Goertzel timing budget
    loopback/             M5   the whole link inside one chip
    linktest/             M6   one-way link between two boards
    afe_sweep/            M7   analogue front-end characterisation
    handoff/              M14  the real thing
  lib/
    dsp/                  Goertzel, symbol sync, carrier detection
    link/                 Manchester, framing, CRC-16, phone-link chunking
    record/               vCard codec, compact TLV, fragmentation, store
    proto/                link state machine, role election, carousel
    hal/                  the seam — interface and config only, no code
    hal_pico/             the RP2350 binding (BLE and flash at M2, PIO at
                          M3, ADC ring, IPC and USB telemetry at M4; the
                          hal.h binding itself is still a stub until M5)
  test/
    host/                 unit tests, channel simulator, two-node protocol sim
    vectors/generated/    from tools/gen_vectors.py — derived, git-ignored
    vectors/captures/     from hardware — committed, replayed forever

scripts/build.py          build / flash the firmware   (Windows + Linux)
scripts/test.py           build / run the host tests   (Windows + Linux)
tools/                    vector generator, reference codec, plotter, replay
android/                  native Android app — pairing, foreground service,
                          history, provisioning. Its own Gradle project
docs/                     design, development plan, firmware architecture
build/, build-host/       generated, git-ignored
```

Each firmware app is a directory under `firmware/apps/` with its own
`CMakeLists.txt` that ends in `handoff_add_app(<target>)`. That wrapper exists
because the SDK reads the `.uf2` output path from `ARCHIVE_OUTPUT_DIRECTORY` but
the `.elf` path from `RUNTIME_OUTPUT_DIRECTORY`; setting both keeps every
artifact in `build/`.

---

## Host tests

**Most of this project is testable with no hardware at all**, and that is
structural rather than lucky: nothing under `lib/dsp`, `lib/link`, `lib/record`
or `lib/proto` may include a Pico SDK header, so all four compile with a host
compiler and run against a channel simulator.

```
python scripts/test.py                    build and run everything
python scripts/test.py --check            the layering rule only, no compiler
python scripts/test.py --suite frame      one suite
python scripts/test.py --ber              the BER sweep
python scripts/test.py --sweep            the M1 parameter sweeps
python scripts/test.py --define HANDOFF_GZ_N=50
```

A run regenerates the golden vectors from `tools/gen_vectors.py`, cross-checks
the C vCard codec against `tools/vcf.py` in both directions, and executes every
suite — currently ~23 700 assertions in a couple of seconds.

Needs any host C compiler: `gcc` or `clang` if one is on `PATH`, otherwise the
Visual Studio build tools on Windows. This is **separate** from the
`arm-none-eabi` toolchain the firmware uses, and neither can substitute for the
other.

Two rules are worth knowing before changing anything here:

- **Golden vectors are generated from the spec, not from the C encoder**
  (`tools/gen_vectors.py`). If the encoder and decoder share a misreading of
  design §9.4, a loopback test passes and the link still fails on the bench. An
  independent generator is the only thing that catches that.
- **Every hardware failure becomes a host test.** A capture that broke the
  decoder on the bench goes into `firmware/test/vectors/captures/` via
  `tools/replay.py --adopt` and is replayed in CI forever. The bug is not fixed
  until it is a regression test.

---

## Bring-up status

Following [docs/development-plan.md](docs/development-plan.md), which expands
design §14.2 into milestones ordered so that each one is testable with the
hardware already on the desk. One new variable at a time.

- [x] **M0** — board, toolchain and USB console alive (`firmware/apps/blink`)
- [x] **M1** — DSP + protocol library, host tested — *no hardware*
- [x] **M2** — BLE → phone → contact in the address book — *Pico + phone*
- [x] **M3** — carrier generation, self-measured — *no hardware*
- [x] **M4** — ADC at 500 ksps + Goertzel real-time budget — *no hardware*
- [ ] **M5** — full link inside one board — *one jumper wire*
- [ ] **M6** — two boards over a wire (first independent-clock test)
- [ ] **M7** — analogue front end characterised on the bench
- [ ] **M8** — link through the AFE, capacitor as a fake body
- [ ] **M9** — two boards, plate coupling, no body
- [ ] **M10** — body coupling, one way, 40 kHz → 200 kHz
- [ ] **M11** — validation campaign (§14.1)
- [ ] **M12** — body → BLE → phone, end to end
- [ ] **M13** — half-duplex turnaround
- [ ] **M14** — role election, two-way handshake


### What M1 settled

Four decisions the documents deliberately left open, each closed with a
measurement rather than an argument. Reproduce them with
`python scripts/test.py --sweep`.

| Question | Answer | Because |
|---|---|---|
| Goertzel `N`, 50 or 25 | **25** | N=50 buys ~2 dB of sensitivity; N=25 halves the frame to 156 ms. Six frames per second of contact instead of three, and the link budget has decibels to spare and no milliseconds |
| CRC-8 or CRC-16 | **CRC-16/CCITT** | fragmentation runs the check 3–6× per contact; 1-in-256 would put a visibly wrong name in an address book |
| The sync rule | lock to the alternating run, find the only `00`, verify the seven chips after it | resolves chip phase and frame position in one step, and needs no count of how many preamble chips survived |
| Carousel weighting | **plain round robin** | the sweep contradicted the architecture doc — see [§13.1](docs/firmware-architecture.md). At 156 ms a frame there is no airtime to spend on repetition; the *priority ordering* is what makes a brief touch useful |

M1 also found and fixed a protocol bug that no amount of reading would have
caught: the first end to be satisfied stopped transmitting and stranded the
other one a fragment short, costing about a third of all handshakes. The two
spare header flag bits now carry the acknowledgement that ends an exchange
deliberately instead of by timeout.

**Cannot prove** — and this matters as much as what it does prove: nothing
about the ADC, the analogue chain, two independent crystals, or a body. Those
are M4, M7, M6 and M10 respectively.


### What M2 settled

The phone half, and it needed no hardware beyond the board and a handset.
Firmware in [`lib/hal_pico/ble.c`](firmware/lib/hal_pico/ble.c) and
[`flash.c`](firmware/lib/hal_pico/flash.c), app in [`android/`](android/), and
the exit criteria are walked step by step in
[android/README.md](android/README.md).

| Question | Answer | Because |
|---|---|---|
| Where the chunk framing lives | **`lib/link/chunk.c`**, not inside `ble.c` | nothing under `hal_pico/` compiles on the host, so framing that lived there could only be tested with a board and a phone — against an exit criterion specifically about the case a developer's own handset does not exercise |
| Which flash sector holds the record | **fourth from the end**, not the last | RP2350 reserves the final sector for the E10 erratum workaround and BTstack's bond bank takes the two below it. "The last sector" would have erased the phone bond on every re-provisioning, and would have failed neither at build time nor at first boot |
| How `store.c` reaches flash | **the backend registers itself from below** | `record/` is inside the layering sandbox and may not name `hal_pico`. Inverting it also made the persistence logic host-testable, which is where the record-id-across-a-power-cycle test lives |
| Pairing method | **LE Secure Connections, Just Works, bonded** | no display and no keypad, so nothing that authenticates the peer is available. Protected against passive eavesdropping, not against a man in the middle at the one moment of pairing |

M2 also found that putting BTstack on `handoff_lib` cost `apps/blink` 92 KB of
Bluetooth firmware it never calls — and would have put a Bluetooth stack inside
`apps/adcbench`, whose whole job at M4 is to measure how much of core 1 is
left. The BLE binding is its own CMake library for that reason, and for that
reason only: the source tree still has all of it under `lib/hal_pico/`.

**Cannot prove**: anything about the body link. The card `apps/handoff` notifies
is a constant in flash. M12 replaces it with a received one, and that is the
first genuinely demonstrable result.


---

## Known deviation from the design doc

§10.4 says to tie **GP23** high to force the SMPS into fixed-frequency PWM mode.
That is correct for the plain Pico 2 (`PICO_SMPS_MODE_PIN 23`) but **wrong for
the Pico 2 W**: on the wireless boards GP23 drives the CYW43 power enable, and
SMPS mode control moves to WL_GPIO1 on the wireless chip —
`cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true)`. See the comment in
[firmware/blink/blink.c](firmware/blink/blink.c).

The onboard LED moves for the same reason: there is no `PICO_DEFAULT_LED_PIN` on
a 2 W, so even blinking requires `cyw43_arch_init()` first.
