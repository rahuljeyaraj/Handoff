# Handoff

**Shake hands. Share contacts.**

Two people wear a wristband. They shake hands, and their contact cards cross
between the bands **through the two bodies** and land in each other's phones. No
cards, no QR codes, no spelling a name out.

The body is the wire. A coated metal plate on the back of each band couples a
pair of tones into the skin at 180 and 200 kHz, the handshake joins the two
arms, and the plate on the other band picks up what survives. Capacitive
body-coupled communication, at 0 cm.

**[Read the full write-up, with figures](docs/element14-blog.md)** is the place
to start. It is the project end to end: what it feels like to use, how the link
works, the two physical layers, and two appendices that get you from an empty
desk to a working pair.

Built for element14's **Make a Connection** contest. The PCBs were sponsored by
[PCBWay](https://www.pcbway.com/).

---

## What is here

A finished pair of bands, and everything that made them.

| | |
|---|---|
| **Firmware** | `firmware/` one C image per milestone, Pico SDK, host-testable DSP and protocol |
| **Board** | `hardware/` KiCad 10, generated from a script, fabrication zip ready to upload |
| **Box** | `hardware/enclosure/` two printed halves, 22 mm strap |
| **App** | `android/` native Android: pairing, provisioning, history, notes |
| **Docs** | `docs/` the write-up, the design, the radio, bring-up |

## Where to go next

| I want to | Read |
|---|---|
| understand the whole thing | [docs/element14-blog.md](docs/element14-blog.md) |
| build a pair myself | appendices A and B of the same file, then [docs/hardware-bringup.md](docs/hardware-bringup.md) |
| know why the electrical design is what it is | [docs/body-coupled-handshake-design.md](docs/body-coupled-handshake-design.md) |
| know how the radio works today | [docs/link-v2-design.md](docs/link-v2-design.md) |
| know what v1 got wrong, and why it matters | appendix C of the write-up |
| find my way round the firmware | [docs/firmware-architecture.md](docs/firmware-architecture.md) |
| work on the board | [hardware/README.md](hardware/README.md) |
| work on the app | [android/README.md](android/README.md) |

---

## Status

Both bands are built, boxed, worn and working. The link on `main` is **v2**, the
second physical layer. v1 is kept only as appendix C of the write-up, because
its mistakes are the reason v2 looks the way it does.

**Measured**

* Two bands complete handshakes continuously through a real skin path, on their
  own cells, with nothing tethered.
* Worn link test, v2: **1015 good frames, frame error rate 0.163**, against
  v1's 447 and 0.296 on the same bench. About twice as good.
* Bands apart at the same spacing, v1: **nothing gets through**. That is the
  measurement that says the path is the body and not the air.
* Host suite: **25 868 checks**, all green, in a couple of seconds, no hardware.
* Every bring-up step on both boards, with a multimeter. No oscilloscope was
  used anywhere in this project.

**Not measured, and worth saying so**

* Current draw, and so battery life. Both cores run flat out all day and nothing
  sleeps.
* Two bands within about 10 cm complete a handshake with nobody holding either
  of them. That is a near-field leak, not the intent.
* The band has to be on the hand that shakes. The other wrist does not work, and
  nothing in the design says why.

Chapter 7 of the write-up is the full list of what the next pair should do
better.

---

## How the link works, in one screen

| | |
|---|---|
| Tones | 180 kHz and 200 kHz, one on the plate at all times, straight out of a PIO pin |
| Bit | two chips, Manchester: low then high is a 0, high then low is a 1 |
| Rate | 4000 chips a second, so **2000 bits a second** |
| Receiver | ADC free-running at 500 ksps into a DMA ring; core 1 runs five Goertzel filters over every 25 samples |
| Window | 50 us, 20 000 a second, five windows to a chip |
| Decision | which of the two tones was louder. Never how loud |
| Guard bins | 140, 160 and 220 kHz, pitches nobody ever sends, measured through the same body and the same amplifier |
| Presence | the tone against the **median of those three**, by a margin computed from "one false busy a minute". Cell-averaging CFAR, not a tracked floor |
| Sync | no chip count: find the one `00` in the preamble, then check 28 of the 30 transitions around it |
| Frame | fixed size, 156 ms, CRC-16/CCITT. No length field |
| Card | a vCard packed from 169 bytes to 79, cut into frames most important first |
| Rendezvous | every band beacons a nonce, then listens for a random time. Being heard is the touch |
| Turn taking | the band that **heard** sends first, then one frame each, plain round robin |
| Clock | 144 MHz, not the SDK's 150, so both tone periods are a whole even number of cycles |

Not one of those numbers was tuned on the bench. Each comes from a requirement
sentence and arithmetic, and the host suite asserts the arithmetic.

---

## Hardware

| | |
|---|---|
| MCU board | Raspberry Pi Pico 2 W (RP2350 + CYW43439), socketed, not soldered |
| Why RP2350 | RP2040 has a known ADC differential-non-linearity defect |
| Why the W | BLE is the link from the band to its owner's phone |
| Why a Pico at all | PIO makes both tones with no oscillator and no driver chip, and leaves both cores for the work |
| Analogue | MCP6292 dual rail-to-rail op-amp, two stages of x11 |
| Electrodes | two 25 x 25 mm copper-clad squares: the taped skin plate, and an outer one tied to board ground that couples to the room |
| Power | 1S LiPo, about 500 mAh, charged on an off-board TP4056. The Pico's own buck-boost takes the cell from 4.2 V down past 3.0 V |
| Wearer UI | one RGB LED, one coin vibration motor, one button, one slide switch |
| Board | 40 x 62 mm, 2 layers, 1.6 mm. `hardware/build/handoff-pcbway.zip` is ready to upload |
| Box | about 45 x 65 x 25 mm, on a 22 mm strap |

> ### Safety is not optional
>
> **Battery only, both ends, whenever anyone is wearing one.** Never on a
> mains-powered laptop. A tethered reading is also a *wrong* reading: the USB
> lead joins the two bands' grounds through the PC, and that return path is the
> thing under test.
>
> **Every electrode is insulated**, edge to edge, and the skin plate is never
> bare. **Hand to hand only**, and **nobody with a pacemaker or an implanted
> defibrillator.**
>
> The series resistors fitted are **100 kOhm**, which holds the worst-case
> current through a person under **33 uA**, with the insulation on top of that.
> The design document's rule is 1 MOhm, ten times less again, and the silkscreen
> and `hardware/bom.csv` still say so. Appendix B.1 of the write-up is the whole
> justification for the deviation.
>
> Read section 13 of
> [docs/body-coupled-handshake-design.md](docs/body-coupled-handshake-design.md)
> before powering anything on a wrist.

---

## Layout

```
CMakeLists.txt            top level; PICO_BOARD=pico2_w, 144 MHz, handoff_add_app()
pico_sdk_import.cmake     stock SDK bootstrap, from pico-sdk 2.3.1

firmware/
  apps/                   one flashable image per milestone
    blink/                board, toolchain and USB console alive
    txgen/                tone generation, self-measured
    adcbench/             the 500 ksps DMA ring and the Goertzel budget
    loopback/             the whole link inside one chip
    linktest/             one-way link between two boards
    turnaround/           half duplex: how fast a band can swap direction
    afe_sweep/            the analogue front end, characterised alone
    bringup/              the PCB bring-up image, driven by scripts/bringup.py
    handoff/              the real thing, plus the LED, motor and wear logic
  lib/
    dsp/                  Goertzel, the FSK decision, CFAR presence, chip sync
    link/                 Manchester, framing, CRC-16, phone-link chunking
    record/               vCard codec, compact TLV, fragmentation, flash store
    proto/                beacon and rendezvous, the exchange, the carousel
    ui/                   the LED vocabulary and the button
    hal/                  the seam: interfaces and config.h, no code
    hal_pico/             the RP2350 binding: PIO, ADC ring, BLE, flash, IPC
  test/
    host/                 unit tests, channel simulator, two-node protocol sim
    vectors/generated/    from tools/gen_vectors.py, derived, git-ignored
    vectors/captures/     from hardware, committed, replayed forever

scripts/build.py          build and flash the firmware        (Windows + Linux)
scripts/test.py           build and run the host tests        (Windows + Linux)
scripts/bringup.py        the PCB bring-up driver: flash, then LED, motor, TX
scripts/link2.py          one process, two boards' consoles, one timeline
scripts/blelog.py         band readings off two phones by radio, never over USB
tools/                    vector generator, reference vCard codec, plotter, labels
hardware/                 the KiCad project, its generators, the fab zip, the box
android/                  the app. Its own Gradle project
docs/                     design, redesign, bring-up, the write-up and its figures
```

Each app is a directory under `firmware/apps/` whose `CMakeLists.txt` ends in
`handoff_add_app(<target>)`. That wrapper exists because the SDK reads the
`.uf2` path from `ARCHIVE_OUTPUT_DIRECTORY` and the `.elf` path from
`RUNTIME_OUTPUT_DIRECTORY`; setting both keeps every artifact together.

---

## Toolchain

Official Raspberry Pi Pico SDK with CMake and Ninja, not PlatformIO. The
low-level work this project needs is first class in the SDK and second class
everywhere else: PIO tone generation, a free-running ADC at 500 ksps into a DMA
ring, core 1 on five Goertzel filters, BLE over BTstack.

Everything is pinned and lives under `~/.pico-sdk`, deliberately **not** on
PATH:

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

**First time on a new machine.** Install the **Raspberry Pi Pico** VS Code
extension (`raspberry-pi.raspberry-pi-pico`), open this folder, and let it
download SDK 2.3.1 when prompted. It populates `~/.pico-sdk` with exactly the
layout above.

## Build

In VS Code, `Ctrl+Shift+B`, or the Pico extension's *Compile Project*. From a
terminal, the same command on Windows and Linux:

```
python scripts/build.py                    # configure + build
python scripts/build.py --clean            # wipe build/ first
python scripts/build.py --flash            # build, then flash over USB
python scripts/build.py --config Release
python scripts/build.py --target handoff   # just one app
python scripts/build.py --help
```

Artifacts land in `build/`: `.uf2`, `.elf`, `.bin`, `.hex`, `.map`, `.dis`.

The script prefers the pinned toolchain under `~/.pico-sdk` and falls back to
whatever is on `PATH`, so a Linux box with distro `cmake`, `ninja` and
`arm-none-eabi-gcc` builds this without the VS Code extension's bundle. Python
3.8 or newer, standard library only.

**For a real band, use the bring-up script instead**, because the PCB moves the
transmitter pin and wants its own build directory:

```
python scripts/bringup.py flash handoff
```

That builds into `build-pcb/` with the board's pin map, flashes it, and can then
drive the LED, the motor and the transmitter from the PC. It is the script
[docs/hardware-bringup.md](docs/hardware-bringup.md) is written against.

## Flash

Either drag a `.uf2` onto the `RP2350` drive that appears when you plug the board
in holding **BOOTSEL**, or use `--flash`. `picotool ... -fx` reboots a running
board into BOOTSEL by itself, so BOOTSEL is only needed for the first flash. On
Linux, install picotool's udev rules rather than running the flash step under
`sudo`.

With two boards on one PC, name the board by its serial rather than its COM
port. The serial is matched as a suffix, so `93D1` finds `4904EF1FFA2393D1`, and
that survives the ports moving.

## Serial console

The firmware presents a USB CDC port. Any terminal at any baud rate works, and
the rate is ignored because it is USB:

```
# Windows: 'mode' lists COM ports; attach with PuTTY or Windows Terminal
# Linux:   /dev/ttyACM0; attach with 'screen /dev/ttyACM0' or 'picocom'
```

For a two-board bench, use `scripts/link2.py` instead of two terminals. It opens
both consoles in one process, stamps every line with the time since the run
started, and can send commands on a schedule. Two separate terminals cannot
answer "which board decided to send first", because their clocks are the
operator's hands.

**Never read a worn band over USB.** Use `scripts/blelog.py`, which takes the
same counters off both phones over BLE while the bands stay floating. A tethered
reading of a body link is wrong, not just unsafe.

---

## Host tests

**Most of this project is testable with no hardware at all**, and that is
structural rather than lucky: nothing under `lib/dsp`, `lib/link`, `lib/record`,
`lib/proto` or `lib/ui` may include a Pico SDK header, so they all compile with a
host compiler and run against a channel simulator.

```
python scripts/test.py                    build and run everything
python scripts/test.py --check            the layering rule only, no compiler
python scripts/test.py --suite frame      one suite
python scripts/test.py --ber              the BER sweep
python scripts/test.py --sweep            the parameter sweeps
python scripts/test.py --define HANDOFF_GZ_N=50
```

A run regenerates the golden vectors from `tools/gen_vectors.py`, cross-checks
the C vCard codec against `tools/vcf.py` in both directions, and executes every
suite. Currently **25 868 checks in a couple of seconds**.

Needs any host C compiler: `gcc` or `clang` if one is on `PATH`, otherwise the
Visual Studio build tools on Windows. This is **separate** from the
`arm-none-eabi` toolchain the firmware uses, and neither substitutes for the
other.

Three rules are worth knowing before changing anything here:

* **Golden vectors are generated from the spec, not from the C encoder**
  (`tools/gen_vectors.py`). If the encoder and decoder share a misreading of the
  frame format, a loopback test passes and the link still fails on the bench. An
  independent generator is the only thing that catches that.
* **Every hardware failure becomes a host test.** A capture that broke the
  decoder on the bench goes into `firmware/test/vectors/captures/` through
  `tools/replay.py --adopt` and is replayed forever. The bug is not fixed until
  it is a regression test.
* **The derived constants are asserted, not trusted.** The tone periods, the
  CFAR margin, the sync threshold and the clock are static-asserted in
  `firmware/lib/hal/config.h` and re-derived in the suite. Changing one number by
  hand fails the build, which is the point.

---

## Two things the design doc gets wrong about the Pico 2 W

Both cost an evening, and both fail silently rather than at build time.

**The SMPS mode pin.** Section 10.4 says tie **GP23** high to force
fixed-frequency PWM. That is right for a plain Pico 2
(`PICO_SMPS_MODE_PIN 23`) and **wrong for the 2 W**: there GP23 drives the CYW43
power enable, and SMPS mode control moves to WL_GPIO1 on the wireless chip,
`cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true)`.

**The LED.** For the same reason there is no `PICO_DEFAULT_LED_PIN` on a 2 W, so
even blinking needs `cyw43_arch_init()` first.

---

## Thanks

[PCBWay](https://www.pcbway.com/) sponsored and manufactured the boards, and
their engineers caught two real problems in the files before anything was made.
Chapter 9 of the write-up is that story. Thank you to Serene, Tori and Sophia.
