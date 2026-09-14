#!/usr/bin/env python3
"""
Build and run the Handoff host test suite. Development plan M1.

Compiles lib/dsp, lib/link, lib/record and lib/proto with the HOST compiler and
runs the unit tests, the channel simulator and the two-node protocol
simulation. No hardware, no Pico SDK, no external test framework.

That is not only convenience. Architecture section 3.2 makes it the enforcement
mechanism: those four directories may not include a Pico SDK header, and this
build fails immediately if one does. --check is the other half of that rule.

    python scripts/test.py                 build and run everything
    python scripts/test.py --check         layering rules only, no compiler
    python scripts/test.py --suite frame   one suite
    python scripts/test.py --ber           the BER sweep
    python scripts/test.py --sweep         the M1 parameter sweeps
    python scripts/test.py --define HANDOFF_GZ_N=50 --sweep gz_n

There is deliberately no CMake here. The host build is twenty files into three
executables; a generator would add a dependency and a configure step to a thing
whose entire selling point is that it needs neither.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FW = REPO_ROOT / "firmware"
LIB = FW / "lib"
HOST = FW / "test" / "host"
VECTORS = FW / "test" / "vectors" / "generated"
OUT = REPO_ROOT / "build-host"

INCLUDE_DIRS = [LIB / "dsp", LIB / "link", LIB / "record", LIB / "proto", LIB / "hal",
                HOST, OUT]

# Architecture 3.2: these four may see the C standard library and each other,
# and nothing else.
SANDBOXED = ["dsp", "link", "record", "proto"]
FORBIDDEN = ('#include "pico/', "#include <pico/", '#include "hardware/',
             "#include <hardware/", "btstack", "pico_stdlib", "cyw43")

LIB_SOURCES = [
    LIB / "dsp" / "goertzel.c",
    LIB / "dsp" / "sync.c",
    LIB / "dsp" / "carrier.c",
    LIB / "link" / "crc.c",
    LIB / "link" / "manchester.c",
    LIB / "link" / "frame.c",
    LIB / "link" / "chunk.c",
    LIB / "record" / "compact.c",
    LIB / "record" / "vcard.c",
    LIB / "record" / "frag.c",
    LIB / "record" / "store.c",
    LIB / "proto" / "carousel.c",
    LIB / "proto" / "beacon.c",
    LIB / "proto" / "link_sm.c",
]

SIM_SOURCES = [HOST / "chan.c", HOST / "hal_host.c", HOST / "sim_twonode.c"]

TEST_SOURCES = [HOST / n for n in (
    "main.c", "test_crc.c", "test_manchester.c", "test_goertzel.c", "test_sync.c",
    "test_frame.c", "test_chunk.c", "test_compact.c", "test_vcard.c",
    "test_frag.c", "test_store.c",
    "test_carousel.c", "test_beacon.c", "test_link.c",
    "test_vectors.c",
    "test_channel.c", "test_budget.c",
)]

PROGRAMS = {
    "handoff_tests": LIB_SOURCES + SIM_SOURCES + TEST_SOURCES,
    "handoff_ber":   LIB_SOURCES + SIM_SOURCES + [HOST / "ber.c"],
    "handoff_sweep": LIB_SOURCES + SIM_SOURCES + [HOST / "sweep.c"],
    "handoff_vcf":   LIB_SOURCES + [HOST / "vcfc.c"],
}

# Cards the two codec implementations are compared on. Deliberately awkward:
# an unknown domain, a property outside the registry, a non-ASCII name, a value
# too long for one fragment, and a one-word name that N cannot be split from.
# Cards the two codec implementations are compared on. Deliberately awkward:
# an unknown domain, a property outside the registry, a non-ASCII name, a value
# too long for one fragment, and a one-word name that N cannot be split from.

CRLF = chr(13) + chr(10)


def _card(*lines):
    """A vCard from its property lines. CRLF, as vCard 3.0 requires."""
    return CRLF.join(("BEGIN:VCARD", "VERSION:3.0") + lines + ("END:VCARD",)) + CRLF


CODEC_CARDS = [
    ("plain", _card(
        "N:Lovelace;Ada;;;",
        "FN:Ada Lovelace",
        "ORG:Analytical Engines Ltd",
        "TITLE:Programmer",
        "TEL;TYPE=CELL:+44 7700 900123",
        "EMAIL;TYPE=INTERNET:ada@gmail.com")),

    ("odd-domain", _card(
        "FN:Bo Tester",
        "EMAIL:bo@example.org",
        "TEL;TYPE=WORK:+1 555 0100",
        "URL:https://example.org/bo")),

    ("unknown-property", _card(
        "FN:Bo Tester",
        "X-SKYPE:bo.tester")),

    ("utf8", _card(
        "FN:Björn Smári",
        "ORG:Æther Ltd",
        "TEL;TYPE=CELL:+354 555 1234")),

    ("long-note", _card(
        "FN:Ada Lovelace",
        "NOTE:" + "met at the conference, " * 5)),

    ("one-word-name", _card(
        "FN:Prince",
        "TEL;TYPE=CELL:+1 555 0199")),

    # Every phone label, the same label twice, and one the wearer typed. The
    # label byte and its text are part of the wire format, so C and Python have
    # to agree on them as exactly as they agree on the packed digits.
    ("phone-labels", _card(
        "FN:Björn Smári",
        "TEL;TYPE=CELL:+354 555 1234",
        "TEL;TYPE=WORK:+354 555 8000",
        "TEL;TYPE=HOME:+354 555 1543",
        "TEL;TYPE=MAIN:+354 555 2020",
        "TEL;TYPE=X-Reception:+354 555 9000",
        "TEL;TYPE=CELL:+354 555 7777",
        "TEL:+354 555 6000",
        "EMAIL;TYPE=INTERNET:bjorn@gmail.com")),
]

EXE = ".exe" if os.name == "nt" else ""


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------

def _colour() -> bool:
    if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
        return False
    if os.name == "nt":
        try:
            import ctypes
            k = ctypes.windll.kernel32
            k.SetConsoleMode(k.GetStdHandle(-11), 7)
        except Exception:
            return False
    return True


_C = _colour()


def paint(text, code):
    return "\033[" + code + "m" + text + "\033[0m" if _C else text


def step(msg):
    print(paint("==> " + msg, "36"), flush=True)


def ok(msg):
    print(paint("    " + msg, "32"), flush=True)


def warn(msg):
    print(paint("    " + msg, "33"), flush=True)


def die(msg):
    print(paint("error: " + msg, "31"), file=sys.stderr, flush=True)
    raise SystemExit(1)


# ---------------------------------------------------------------------------
# the layering rule
# ---------------------------------------------------------------------------

def check_layering() -> int:
    """
    Architecture 3.2, the load-bearing constraint of the whole project: every
    milestone that claims 'hardware: none' depends on this holding.
    """
    step("checking the layering rule")
    bad = []

    for layer in SANDBOXED:
        for src in sorted((LIB / layer).glob("*.[ch]")):
            text = src.read_text(encoding="utf-8", errors="replace")
            for n, line in enumerate(text.splitlines(), 1):
                if line.lstrip().startswith("//") or line.lstrip().startswith("*"):
                    continue
                for token in FORBIDDEN:
                    if token in line:
                        bad.append("%s:%d: %s" % (src.relative_to(REPO_ROOT), n, line.strip()))

    if bad:
        for b in bad:
            print(paint("    " + b, "31"), file=sys.stderr)
        die("%d layering violation(s): lib/{%s} may not touch the SDK"
            % (len(bad), ",".join(SANDBOXED)))

    ok("lib/{%s} are free of SDK headers" % ",".join(SANDBOXED))
    return 0


# ---------------------------------------------------------------------------
# compiler discovery
# ---------------------------------------------------------------------------

class Compiler:
    def __init__(self, kind, argv, env=None):
        self.kind = kind        # "gcc" or "msvc"
        self.argv = argv
        self.env = env

    def compile_link(self, sources, out_path, defines, quiet):
        inc = [str(p) for p in INCLUDE_DIRS]

        if self.kind == "msvc":
            # /W4 rather than /W3: it is the closest MSVC gets to gcc -Wextra,
            # which is what CI uses. The three suppressions are warnings gcc
            # does not have an equivalent for — unused parameter (already
            # waived below for gcc too), constant conditional (every do/while
            # macro trips it), and unreferenced inline in a header.
            cmd = list(self.argv) + ["/nologo", "/W4", "/WX", "/O2", "/std:c11",
                                     "/wd4100", "/wd4127", "/wd4514",
                                     "/D_CRT_SECURE_NO_WARNINGS"]
            cmd += ["/I" + i for i in inc]
            cmd += ["/D" + d for d in defines]
            cmd += [str(s) for s in sources]
            cmd += ["/Fe:" + str(out_path),
                    "/Fo:" + str(out_path.parent) + os.sep,
                    "/link", "/INCREMENTAL:NO"]
        else:
            cmd = list(self.argv) + ["-std=c11", "-O2", "-g",
                                     "-Wall", "-Wextra", "-Werror",
                                     "-Wno-unused-parameter"]
            cmd += ["-I" + i for i in inc]
            cmd += ["-D" + d for d in defines]
            cmd += [str(s) for s in sources]
            cmd += ["-o", str(out_path), "-lm"]

        res = subprocess.run(cmd, cwd=str(OUT), env=self.env,
                             capture_output=quiet, text=True)
        if res.returncode != 0:
            if quiet:
                sys.stdout.write(res.stdout or "")
                sys.stderr.write(res.stderr or "")
            die("compilation failed (%s)" % self.kind)


def _msvc_env():
    """Run vcvars64.bat and capture the environment it sets."""
    roots = [Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
             Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))]
    for root in roots:
        vs = root / "Microsoft Visual Studio"
        if not vs.is_dir():
            continue
        for bat in sorted(vs.glob("*/*/VC/Auxiliary/Build/vcvars64.bat"), reverse=True):
            out = subprocess.run(["cmd", "/c", str(bat), "&&", "set"],
                                 capture_output=True, text=True)
            if out.returncode != 0:
                continue
            env = dict(os.environ)
            for line in out.stdout.splitlines():
                if "=" in line:
                    k, v = line.split("=", 1)
                    env[k] = v
            cl = shutil.which("cl", path=env.get("PATH", ""))
            if cl:
                return cl, env
    return None, None


def find_compiler(prefer=None) -> Compiler:
    names = [prefer] if prefer else ["cc", "gcc", "clang"]
    for name in names:
        if name and shutil.which(name):
            return Compiler("gcc", [name])

    if platform.system() == "Windows":
        cl, env = _msvc_env()
        if cl:
            return Compiler("msvc", [cl], env)

    die("no host C compiler found. Install gcc or clang, or (on Windows) the "
        "Visual Studio C++ build tools. This is separate from the arm-none-eabi "
        "toolchain the firmware uses.")


# ---------------------------------------------------------------------------
# vectors
# ---------------------------------------------------------------------------

def generate_vectors(defines) -> None:
    """
    Regenerate before every run. Vectors are derived, so they are not committed
    (see .gitignore) and cannot go stale against a changed HANDOFF_GZ_N.
    """
    gz_n, carrier = 25, 200000
    for d in defines:
        if d.startswith("HANDOFF_GZ_N="):
            gz_n = int(d.split("=", 1)[1])
        elif d.startswith("HANDOFF_CARRIER_HZ="):
            carrier = int(d.split("=", 1)[1])

    step("generating golden vectors (GZ_N %d, carrier %d Hz)" % (gz_n, carrier))
    res = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "gen_vectors.py"),
                          "--out", str(VECTORS), "--gz-n", str(gz_n),
                          "--carrier-hz", str(carrier), "--quiet"],
                         capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        die("gen_vectors.py failed")
    OUT.mkdir(parents=True, exist_ok=True)
    header = ('/* generated by scripts/test.py */\n'
              '#define HANDOFF_VECTOR_DIR "%s"\n' % VECTORS.as_posix())
    (OUT / "vectors_path.h").write_text(header, encoding="ascii")
    ok("wrote %s" % VECTORS.relative_to(REPO_ROOT))


# ---------------------------------------------------------------------------

def check_codec(exe: Path) -> int:
    """
    Development plan M1: cross-check the C codec against tools/vcf.py in BOTH
    directions. One direction is not enough — a misreading of architecture 8.2
    that both implementations share would round-trip perfectly and still put a
    mangled contact in someone's address book.
    """
    import tempfile
    sys.path.insert(0, str(REPO_ROOT / "tools"))
    import vcf   # noqa: E402

    step("cross-checking the C codec against tools/vcf.py")
    bad = 0
    max_value = 30   # HANDOFF_FRAG_PAYLOAD - 2

    for name, card in CODEC_CARDS:
        with tempfile.NamedTemporaryFile("w", suffix=".vcf", encoding="utf-8",
                                         delete=False, newline="") as fh:
            fh.write(card)
            path = fh.name

        try:
            c_hex = subprocess.run(
                [str(exe), "--max-value", str(max_value), "encode", path],
                capture_output=True, text=True, check=True).stdout.strip()
            py_hex = vcf.encode(vcf.parse(card), max_value).hex()

            if c_hex != py_hex:
                bad += 1
                print(paint("    %-18s ENCODE differs" % name, "31"))
                print("      C      %s" % c_hex)
                print("      python %s" % py_hex)
                continue

            # ...and back the other way: each decodes what the other encoded.
            raw = subprocess.run([str(exe), "decode", py_hex],
                                 capture_output=True, check=True).stdout
            c_text = raw.decode("utf-8").replace(CRLF, chr(10))
            py_text = vcf.render(vcf.decode(bytes.fromhex(c_hex))).replace(CRLF, chr(10))

            if c_text != py_text:
                bad += 1
                print(paint("    %-18s DECODE differs" % name, "31"))
                ca, pa = c_text.splitlines(), py_text.splitlines()
                for n in range(max(len(ca), len(pa))):
                    a = ca[n] if n < len(ca) else "<missing>"
                    b = pa[n] if n < len(pa) else "<missing>"
                    if a != b:
                        print("      line %d  C      %s" % (n + 1, a))
                        print("              python %s" % b)
                continue

            ok("%-18s %3d bytes, identical both ways" % (name, len(c_hex) // 2))
        finally:
            os.unlink(path)

    if bad:
        die("%d card(s) disagree between the C and the Python codec" % bad)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="layering rules only")
    ap.add_argument("--suite", help="run one test suite by name")
    ap.add_argument("--ber", action="store_true", help="run the BER sweep")
    ap.add_argument("--sweep", nargs="?", const="all",
                    help="run the M1 parameter sweeps")
    ap.add_argument("--no-codec-check", action="store_true",
                    help="skip the C-versus-Python vCard cross-check")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VALUE",
                    help="override a config.h constant")
    ap.add_argument("--cc", help="host compiler to use")
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    check_layering()
    if args.check:
        return 0

    if args.clean and OUT.exists():
        step("removing " + str(OUT.relative_to(REPO_ROOT)))
        shutil.rmtree(OUT)

    OUT.mkdir(parents=True, exist_ok=True)
    generate_vectors(args.define)

    defines = list(args.define)
    cc = find_compiler(args.cc)
    step("building with %s" % cc.kind)

    wanted = ["handoff_tests"]
    if not args.no_codec_check:
        wanted.append("handoff_vcf")
    if args.ber:
        wanted.append("handoff_ber")
    if args.sweep:
        wanted.append("handoff_sweep")

    for name in wanted:
        exe = OUT / (name + EXE)
        cc.compile_link(PROGRAMS[name], exe, defines, not args.verbose)
        ok("%-14s %s" % (name, exe.relative_to(REPO_ROOT)))

    rc = 0

    if not args.no_codec_check:
        rc_codec = check_codec(OUT / ("handoff_vcf" + EXE))
        if rc_codec:
            return rc_codec

    step("running unit tests")
    cmd = [str(OUT / ("handoff_tests" + EXE))]
    if args.suite:
        cmd.append(args.suite)
    rc |= subprocess.run(cmd).returncode

    if args.ber:
        step("BER sweep")
        rc |= subprocess.run([str(OUT / ("handoff_ber" + EXE))]).returncode

    if args.sweep:
        step("parameter sweeps")
        rc |= subprocess.run([str(OUT / ("handoff_sweep" + EXE)), args.sweep]).returncode

    if rc == 0:
        ok("all green")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
