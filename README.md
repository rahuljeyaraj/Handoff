# Handoff — Shake hands. Share contacts.

Two people wear a wristband. They shake hands. Contact details transfer between
the wristbands **through their bodies**, by capacitive coupling at 200 kHz.

The body is the transmission medium. The handshake closes the circuit; it does
not generate the signal.

Full electrical design, modulation, protocol, BOM and validation plan:
**[docs/body-coupled-handshake-design.md](docs/body-coupled-handshake-design.md)**

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
firmware/blink/           bring-up step 0 — LED + USB heartbeat
scripts/build.py          command-line build / flash (Windows + Linux)
docs/                     design documentation
build/                    generated, git-ignored
```

Each firmware app is a directory under `firmware/` with its own `CMakeLists.txt`
that ends in `handoff_add_app(<target>)`. That wrapper exists because the SDK
reads the `.uf2` output path from `ARCHIVE_OUTPUT_DIRECTORY` but the `.elf` path
from `RUNTIME_OUTPUT_DIRECTORY`; setting both keeps every artifact in `build/`.

---

## Bring-up status

Following the sequence in design doc §14.2 — one direction at a time, because if
both halves are built at once there is no way to tell which one is wrong.

- [x] **Step 0** — board, toolchain and USB console alive (`firmware/blink`)
- [ ] **Step 1** — one-way link at 40 kHz. A transmits, B receives, nothing else
- [ ] **Step 2** — move to 200 kHz (only the PIO divider changes)
- [ ] **Step 3** — Manchester coding, added *after* threshold drift is observed
- [ ] **Step 4** — BLE and the phone page
- [ ] **Step 5** — role election for two-way operation

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
