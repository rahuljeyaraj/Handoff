#!/usr/bin/env python3
"""
Configure and build the Handoff firmware from a plain terminal.

Runs on Windows and Linux. The pinned toolchain lives under ~/.pico-sdk (that is
where the Raspberry Pi Pico VS Code extension puts it) and is deliberately not on
PATH, so this script points CMake at it explicitly. Where a pinned tool is
missing it falls back to whatever is on PATH, which is the usual situation on a
Linux box with distro cmake/ninja/arm-none-eabi-gcc installed.

    python scripts/build.py
    python scripts/build.py --clean
    python scripts/build.py --flash
    python scripts/build.py --config Release
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Keep these in step with the DO-NOT-EDIT block in CMakeLists.txt and the paths
# in .vscode/settings.json.
SDK_VERSION = "2.3.1"
TOOLCHAIN_VERSION = "15_2_Rel1"
PICOTOOL_VERSION = "2.3.1"
CMAKE_VERSION = "v4.3.4"
NINJA_VERSION = "v1.13.2"

PICO_ROOT = Path.home() / ".pico-sdk"
EXE = ".exe" if os.name == "nt" else ""

REPO_ROOT = Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------

def _supports_colour() -> bool:
    if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
        return False
    if os.name == "nt":
        try:  # enable VT processing on the Windows console
            import ctypes

            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        except Exception:
            return False
    return True


_COLOUR = _supports_colour()


def _paint(text: str, code: str) -> str:
    return "\033[" + code + "m" + text + "\033[0m" if _COLOUR else text


def step(msg: str) -> None:
    print(_paint("==> " + msg, "36"), flush=True)


def ok(msg: str) -> None:
    print(_paint("    " + msg, "32"), flush=True)


def die(msg: str) -> None:
    print(_paint("error: " + msg, "31"), file=sys.stderr, flush=True)
    raise SystemExit(1)


# ---------------------------------------------------------------------------
# toolchain discovery
# ---------------------------------------------------------------------------

def find_tool(name: str, pinned: Path) -> Path:
    """Prefer the pinned copy under ~/.pico-sdk, else fall back to PATH."""
    if pinned.is_file():
        return pinned
    found = shutil.which(name)
    if found:
        return Path(found)
    die(
        "could not find " + name + ".\n"
        "  looked for the pinned copy at: " + str(pinned) + "\n"
        "  and for " + name + " on PATH.\n"
        "Open this folder in VS Code and let the Raspberry Pi Pico extension\n"
        "install SDK " + SDK_VERSION + ", or install " + name + " yourself. "
        "See README.md."
    )


def find_sdk() -> Path:
    pinned = PICO_ROOT / "sdk" / SDK_VERSION
    if (pinned / "pico_sdk_init.cmake").is_file():
        return pinned
    env = os.environ.get("PICO_SDK_PATH")
    if env and (Path(env) / "pico_sdk_init.cmake").is_file():
        return Path(env)
    die(
        "could not find pico-sdk " + SDK_VERSION + ".\n"
        "  looked at: " + str(pinned) + "\n"
        "  and at PICO_SDK_PATH (" + (env or "unset") + ").\n"
        "Open this folder in VS Code and let the Raspberry Pi Pico extension\n"
        "install it, or clone it yourself:\n"
        "  git clone --branch " + SDK_VERSION
        + " https://github.com/raspberrypi/pico-sdk " + str(pinned) + "\n"
        "  git -C " + str(pinned) + " submodule update --init"
    )


def run(argv: list, env: dict = None) -> None:
    printable = " ".join(str(a) for a in argv)
    try:
        subprocess.run([str(a) for a in argv], env=env, check=True)
    except FileNotFoundError:
        die("cannot execute: " + printable)
    except subprocess.CalledProcessError as exc:
        die("command failed with exit code " + str(exc.returncode) + ":\n  " + printable)


# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build the Handoff firmware for the Raspberry Pi Pico 2 W.",
    )
    parser.add_argument("--clean", action="store_true",
                        help="delete the build directory first")
    parser.add_argument("--flash", action="store_true",
                        help="flash the built image over USB with picotool")
    parser.add_argument("--config", default="Debug",
                        choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                        help="CMake build type (default: Debug)")
    parser.add_argument("--target", default=None,
                        help="build only this target (default: everything)")
    parser.add_argument("--flash-target", default="blink", metavar="NAME",
                        help="which .uf2 to flash with --flash (default: blink)")
    parser.add_argument("--tx-pin", default=2, type=int, metavar="GP",
                        help="GPIO that drives the electrode: 2 on the breadboard "
                             "benches (default), 11 on the PCB")
    parser.add_argument("--build-dir", default="build", metavar="DIR",
                        help="build directory, relative to the repo root "
                             "(default: build)")
    args = parser.parse_args()

    build_dir = (REPO_ROOT / args.build_dir).resolve()

    sdk = find_sdk()
    cmake = find_tool("cmake", PICO_ROOT / "cmake" / CMAKE_VERSION / "bin" / ("cmake" + EXE))
    ninja = find_tool("ninja", PICO_ROOT / "ninja" / NINJA_VERSION / ("ninja" + EXE))

    env = os.environ.copy()
    env["PICO_SDK_PATH"] = str(sdk)

    toolchain = PICO_ROOT / "toolchain" / TOOLCHAIN_VERSION
    if (toolchain / "bin" / ("arm-none-eabi-gcc" + EXE)).is_file():
        env["PICO_TOOLCHAIN_PATH"] = str(toolchain)
    elif not shutil.which("arm-none-eabi-gcc"):
        die(
            "no ARM toolchain.\n"
            "  looked for the pinned copy at: " + str(toolchain) + "\n"
            "  and for arm-none-eabi-gcc on PATH.\n"
            "See README.md for the expected " + TOOLCHAIN_VERSION + " install."
        )

    if args.clean and build_dir.exists():
        step("Removing " + str(build_dir))
        shutil.rmtree(build_dir)

    configure = [
        cmake, "-S", REPO_ROOT, "-B", build_dir, "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=" + str(ninja),
        "-DCMAKE_BUILD_TYPE=" + args.config,
        # Always passed, so a build directory never keeps a stale pin in its
        # CMake cache from an earlier run with a different --tx-pin.
        "-DHANDOFF_TX_PIN=" + str(args.tx_pin),
    ]

    # Point the SDK at the prebuilt picotool and pioasm, so it does not try to
    # fetch and build them from source (which needs a host compiler that a
    # firmware-only machine may not have).
    picotool_dir = PICO_ROOT / "picotool" / PICOTOOL_VERSION / "picotool"
    if (picotool_dir / "picotoolConfig.cmake").is_file():
        configure.append("-Dpicotool_DIR=" + str(picotool_dir))
    pioasm_dir = PICO_ROOT / "tools" / SDK_VERSION / "pioasm"
    if (pioasm_dir / "pioasmConfig.cmake").is_file():
        configure.append("-Dpioasm_DIR=" + str(pioasm_dir))

    step("Configuring (" + args.config + ", TX on GP" + str(args.tx_pin) + ")")
    run(configure, env=env)

    step("Building")
    build_cmd = [cmake, "--build", build_dir]
    if args.target:
        build_cmd += ["--target", args.target]
    run(build_cmd, env=env)

    artifacts = sorted(build_dir.glob("*.uf2"))
    if artifacts:
        for uf2 in artifacts:
            ok("{}  {:.1f} KB  ({})".format(
                uf2.name, uf2.stat().st_size / 1024, uf2))
    else:
        print("    no .uf2 produced", file=sys.stderr, flush=True)

    if args.flash:
        picotool = find_tool(
            "picotool",
            PICO_ROOT / "picotool" / PICOTOOL_VERSION / "picotool" / ("picotool" + EXE),
        )
        image = build_dir / (args.flash_target + ".uf2")
        if not image.is_file():
            die("nothing to flash: " + str(image) + " does not exist")
        step("Flashing " + image.name)
        # -f reboots a running board into BOOTSEL and -x runs it afterwards, so
        # holding BOOTSEL by hand is only needed for the very first flash.
        run([picotool, "load", image, "-fx"])
        if os.name != "nt":
            print("    (on a permissions error, install the udev rules from the "
                  "picotool repo rather than reaching for sudo)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
