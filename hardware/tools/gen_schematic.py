#!/usr/bin/env python3
"""
Generate hardware/handoff.kicad_sch from the design-doc netlist (design §7),
then prove it with kicad-cli: ERC must be clean and the exported netlist must
match the intended one, pin for pin.

One-shot bootstrap. The moment the schematic is edited by hand in KiCad this
script stops being the source of truth; keep it only as the record of how the
sheet was first produced and as the netlist oracle in `EXPECTED_NETS`.

    python hardware/tools/gen_schematic.py            # generate + check
    python hardware/tools/gen_schematic.py --no-check # generate only

Coordinates are in grid units of 1.27 mm (KiCad's 50 mil schematic grid), so
every pin end lands on the grid by construction.
"""
from __future__ import annotations

import copy
import json
import os
import re
import subprocess
import sys
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
HW = HERE.parent
KICAD = Path(os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0"))
KICAD_CLI = KICAD / "bin" / "kicad-cli.exe"
SYMDIR = KICAD / "share" / "kicad" / "symbols"

PROJECT = "handoff"
# labels that appear once, on a wire, purely to give the net its name; any other single label is a mistake
NAME_ONLY_LABELS = {"BAT+", "SW_OUT", "PAD"}
G = 1.27  # one grid unit, mm


# --------------------------------------------------------------------------
# A tiny s-expression reader/writer. Quoted strings become Q, everything else
# stays a bare token string. Good enough for KiCad's files.
# --------------------------------------------------------------------------
class Q(str):
    """A quoted string token."""


def parse(text: str):
    i, n = 0, len(text)

    def skip_ws():
        nonlocal i
        while i < n and text[i] in " \t\r\n":
            i += 1

    def node():
        nonlocal i
        skip_ws()
        c = text[i]
        if c == "(":
            i += 1
            out = []
            while True:
                skip_ws()
                if text[i] == ")":
                    i += 1
                    return out
                out.append(node())
        if c == '"':
            i += 1
            buf = []
            while text[i] != '"':
                if text[i] == "\\":
                    i += 1
                buf.append(text[i])
                i += 1
            i += 1
            return Q("".join(buf))
        j = i
        while i < n and text[i] not in " \t\r\n()":
            i += 1
        return text[j:i]

    return node()


def dump(node, indent=0) -> str:
    pad = "\t" * indent
    if isinstance(node, Q):
        return '"' + node.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(node, str):
        return node
    if not node:
        return "()"
    head = node[0]
    # short leaf lists stay on one line
    if all(not isinstance(x, list) for x in node):
        return "(" + " ".join(dump(x) for x in node) + ")"
    parts = ["(" + dump(head)]
    inline = []
    rest = node[1:]
    # keep leading atoms on the head line: (symbol "name" ...), (at 1 2 3)
    k = 0
    while k < len(rest) and not isinstance(rest[k], list):
        inline.append(dump(rest[k]))
        k += 1
    line = parts[0] + ("" if not inline else " " + " ".join(inline))
    body = [dump(x, indent + 1) for x in rest[k:]]
    return line + "\n" + "\n".join("\t" * (indent + 1) + b for b in body) + "\n" + pad + ")"


def find(node, key):
    """First child list whose head is `key`."""
    for x in node:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def find_all(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


# --------------------------------------------------------------------------
# Library symbols
# --------------------------------------------------------------------------
_libcache: dict[str, dict] = {}


def load_lib(name: str) -> dict:
    if name not in _libcache:
        tree = parse((SYMDIR / f"{name}.kicad_sym").read_text(encoding="utf-8"))
        _libcache[name] = {s[1]: s for s in find_all(tree, "symbol")}
    return _libcache[name]


def flatten(lib: str, name: str):
    """Resolve `extends` so the schematic gets a self-contained symbol."""
    src = load_lib(lib)[name]
    ext = find(src, "extends")
    if ext is None:
        return copy.deepcopy(src)
    parent_name = ext[1]
    out = flatten(lib, parent_name)
    out[1] = Q(name)
    for sub in find_all(out, "symbol"):
        sub[1] = Q(name + sub[1][len(parent_name):])
    # child properties override the parent's
    for prop in find_all(src, "property"):
        for i, x in enumerate(out):
            if isinstance(x, list) and x and x[0] == "property" and x[1] == prop[1]:
                out[i] = copy.deepcopy(prop)
                break
        else:
            out.append(copy.deepcopy(prop))
    return out


class LibSym:
    def __init__(self, libname: str, name: str, node, sch_name: str | None = None):
        self.node = node
        self.lib_id = f"{libname}:{sch_name or name}"
        self.pins: dict[tuple[int, str], tuple[float, float, int, str, str]] = {}
        for sub in find_all(node, "symbol"):
            m = re.match(r".*_(\d+)_(\d+)$", sub[1])
            unit = int(m.group(1))
            for p in find_all(sub, "pin"):
                at = find(p, "at")
                num = find(p, "number")[1]
                nm = find(p, "name")[1]
                self.pins[(unit, num)] = (float(at[1]), float(at[2]), int(float(at[3])), nm, p[1])

    def pin(self, unit: int, number: str):
        if (unit, number) in self.pins:
            return self.pins[(unit, number)]
        if (0, number) in self.pins:
            return self.pins[(0, number)]
        raise KeyError(f"{self.lib_id}: no pin {number} in unit {unit}")

    def rename(self, new_name: str, libname: str):
        old = self.node[1]
        self.node[1] = Q(new_name)
        for sub in find_all(self.node, "symbol"):
            sub[1] = Q(new_name + sub[1][len(old):])
        self.lib_id = f"{libname}:{new_name}"

    def set_prop(self, key: str, value: str):
        for p in find_all(self.node, "property"):
            if p[1] == key:
                p[2] = Q(value)
                return
        self.node.append(["property", Q(key), Q(value), ["at", "0", "0", "0"],
                          ["effects", ["font", ["size", "1.27", "1.27"]], ["hide", "yes"]]])

    def units(self) -> int:
        return max(int(re.match(r".*_(\d+)_\d+$", s[1]).group(1)) for s in find_all(self.node, "symbol"))


_UID_N = 0


def uid() -> str:
    """Deterministic: the n-th UUID asked for is always the same one. The sheet
    is regenerated from scratch every run, and with random UUIDs every run
    rewrote every junction, wire and symbol path in the sheet and the board -
    a thousand-line diff that said nothing. Stable UUIDs make the diff the
    change."""
    global _UID_N
    _UID_N += 1
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"handoff.kicad_sch/{_UID_N}"))


# --------------------------------------------------------------------------
# Schematic builder
# --------------------------------------------------------------------------
def rot_pin(px: float, py: float, rot: int, mirror: str | None):
    """Library pin (y up) -> offset in sheet coordinates (y down)."""
    if mirror == "y":
        px = -px
    elif mirror == "x":
        py = -py
    if rot == 0:
        return px, -py
    if rot == 90:
        return -py, -px
    if rot == 180:
        return -px, py
    if rot == 270:
        return py, px
    raise ValueError(rot)


class Inst:
    def __init__(self, sym: LibSym, ref: str, value: str, unit: int, at, rot, mirror, props, hidden_props,
                 in_bom=True, on_board=True, dnp=False):
        self.sym, self.ref, self.value, self.unit = sym, ref, value, unit
        self.x, self.y = at[0] * G, at[1] * G
        self.rot, self.mirror = rot, mirror
        self.props = props
        self.hidden_props = hidden_props
        self.in_bom, self.on_board, self.dnp = in_bom, on_board, dnp
        self.uuid = uid()
        self.label_pos = {}

    def pin_xy(self, number: str):
        px, py, _, _, _ = self.sym.pin(self.unit, number)
        dx, dy = rot_pin(px, py, self.rot, self.mirror)
        return round(self.x + dx, 4), round(self.y + dy, 4)

    def pin_numbers(self):
        return sorted({n for (u, n) in self.sym.pins if u in (0, self.unit)}, key=lambda s: (len(s), s))


class Schematic:
    def __init__(self, paper="A3", title="", rev="", company="", date=""):
        self.paper, self.title, self.rev, self.company, self.date = paper, title, rev, company, date
        self.root_uuid = uid()
        self.libsyms: dict[str, LibSym] = {}
        self.insts: list[Inst] = []
        self.by_ref: dict[tuple[str, int], Inst] = {}
        self.wires: list[list[tuple[float, float]]] = []
        self.labels = []
        self.texts = []
        self.no_connects = []
        self.pwr_n = 0

    # -- symbols ----------------------------------------------------------
    def use(self, libname: str, name: str, sch_name: str | None = None, adapt=None) -> LibSym:
        key = f"{libname}:{sch_name or name}"
        if key not in self.libsyms:
            node = flatten(libname, name)
            ls = LibSym(libname, name, node)
            if sch_name:
                ls.rename(sch_name, libname)
            if adapt:
                adapt(ls)
            self.libsyms[key] = ls
        return self.libsyms[key]

    def place(self, sym: LibSym, ref: str, value: str, at, rot=0, mirror=None, unit=1, fp="", datasheet="",
              desc="", ref_at=None, val_at=None, hide_value=False, in_bom=True, on_board=True, dnp=False) -> Inst:
        inst = Inst(sym, ref, value, unit, at, rot, mirror,
                    props={"Reference": ref, "Value": value},
                    hidden_props={"Footprint": fp, "Datasheet": datasheet, "Description": desc},
                    in_bom=in_bom, on_board=on_board, dnp=dnp)
        inst.label_pos = {"Reference": ref_at, "Value": val_at}
        inst.hide_value = hide_value
        self.insts.append(inst)
        self.by_ref[(ref, unit)] = inst
        return inst

    def pin(self, ref: str, number: str, unit: int = 1):
        return self.by_ref[(ref, unit)].pin_xy(number)

    def gpin(self, ref: str, number: str, unit: int = 1):
        """Pin position in grid units."""
        x, y = self.pin(ref, number, unit)
        return round(x / G), round(y / G)

    # -- wiring (grid units) ----------------------------------------------
    def wire(self, *pts):
        """Polyline through grid points; consecutive points must share x or y."""
        pts = [self._g(p) for p in pts]
        for a, b in zip(pts, pts[1:]):
            if a[0] != b[0] and a[1] != b[1]:
                raise ValueError(f"diagonal wire {a}->{b}")
            if a != b:
                self.wires.append([a, b])

    def hv(self, a, b):
        """a -> b, horizontal first."""
        a, b = self._g(a), self._g(b)
        self.wire(a, (b[0], a[1]), b)

    def vh(self, a, b):
        a, b = self._g(a), self._g(b)
        self.wire(a, (a[0], b[1]), b)

    @staticmethod
    def _g(p):
        return (round(p[0]), round(p[1]))

    def label(self, name: str, at, rot=0, justify="left bottom"):
        self.labels.append((name, self._g(at), rot, justify))

    def power(self, kind: str, at, rot=0, value=None):
        """Power symbol: GND / +3V3 from the stock lib, VREF / VSYS from ours."""
        libname, name = {"GND": ("power", "GND"), "+3V3": ("power", "+3V3"),
                         "VREF": ("handoff", "VREF"), "VSYS": ("handoff", "VSYS")}[kind]
        sym = self.libsyms[f"{libname}:{name}"]
        self.pwr_n += 1
        ref = f"#PWR{self.pwr_n:02d}"
        if rot == 180:
            val_at = (0, -3, "center") if kind == "GND" else (0, 4, "center")
        else:
            val_at = (0, 3, "center") if kind == "GND" else (0, -3, "center")
        return self.place(sym, ref, value or name, at, rot=rot, val_at=val_at)

    def text(self, s: str, at, size=1.27, bold=False, justify="left bottom"):
        self.texts.append((s, self._g(at), size, bold, justify))

    def no_connect(self, at):
        self.no_connects.append(self._g(at))

    # -- junctions: where 3+ things meet ----------------------------------
    def _junctions(self):
        from collections import defaultdict
        touch = defaultdict(int)
        endpoints = set()
        for a, b in self.wires:
            endpoints.add(a)
            endpoints.add(b)
        pin_pts = set()
        for inst in self.insts:
            for n in inst.pin_numbers():
                x, y = inst.pin_xy(n)
                pin_pts.add((round(x / G), round(y / G)))
        for p in endpoints | pin_pts:
            cnt = 0
            for a, b in self.wires:
                if p == a or p == b:
                    cnt += 1
                elif a[0] == b[0] == p[0] and min(a[1], b[1]) < p[1] < max(a[1], b[1]):
                    cnt += 2  # passing through: counts as two arms
                elif a[1] == b[1] == p[1] and min(a[0], b[0]) < p[0] < max(a[0], b[0]):
                    cnt += 2
            if p in pin_pts:
                cnt += 1
            touch[p] = cnt
        return [p for p, c in touch.items() if c >= 3]

    # -- output -----------------------------------------------------------
    def _prop(self, key, val, x, y, hide=False, justify=None, rot=0):
        eff = ["effects", ["font", ["size", "1.27", "1.27"]]]
        if justify and justify != "center":
            eff.append(["justify"] + justify.split())
        if hide:
            eff.append(["hide", "yes"])
        return ["property", Q(key), Q(val), ["at", f"{x:.4f}", f"{y:.4f}", str(rot)], eff]

    def _inst_node(self, inst: Inst):
        n = ["symbol", ["lib_id", Q(inst.sym.lib_id)],
             ["at", f"{inst.x:.4f}", f"{inst.y:.4f}", str(inst.rot)]]
        if inst.mirror:
            n.append(["mirror", inst.mirror])
        n += [["unit", str(inst.unit)], ["exclude_from_sim", "no"],
              ["in_bom", "yes" if inst.in_bom else "no"], ["on_board", "yes" if inst.on_board else "no"],
              ["dnp", "yes" if inst.dnp else "no"], ["uuid", Q(inst.uuid)]]
        is_pwr = inst.ref.startswith("#")
        sideways = inst.rot in (90, 270)
        for key in ("Reference", "Value"):
            pos = inst.label_pos.get(key)
            if pos is None:
                if sideways:   # a horizontal two-pin part: name above, value below, centred
                    k = 2.5 if inst.sym.lib_id.startswith("Device:C") else 2
                    dx, dy = (0, -k * G) if key == "Reference" else (0, k * G)
                    justify = "center"
                else:
                    dx, dy = (2.0, -1.5) if key == "Reference" else (2.0, 1.5)
                    justify = "left"
                x, y = inst.x + dx, inst.y + dy
            else:
                x, y = inst.x + pos[0] * G, inst.y + pos[1] * G
                justify = pos[2] if len(pos) > 2 else "left"
            hide = (is_pwr and key == "Reference") or (key == "Value" and inst.hide_value)
            n.append(self._prop(key, inst.props[key], x, y, hide=hide, justify=justify, rot=90 if sideways else 0))
        for key, val in inst.hidden_props.items():
            n.append(self._prop(key, val, inst.x, inst.y, hide=True))
        for num in inst.pin_numbers():
            n.append(["pin", Q(num), ["uuid", Q(uid())]])
        n.append(["instances", ["project", Q(PROJECT),
                                ["path", Q("/" + self.root_uuid), ["reference", Q(inst.ref)],
                                 ["unit", str(inst.unit)]]]])
        return n

    def lint(self):
        """Fail loudly on the mistakes that silently short or open things in KiCad."""
        from collections import Counter
        for name, n in Counter(name for name, *_ in self.labels).items():
            if n < 2 and name not in NAME_ONLY_LABELS:
                raise ValueError(f"label {name!r} appears only once: it connects nothing")
        segs = self.wires
        for i, (a, b) in enumerate(segs):
            for c, d in segs[i + 1:]:
                if a[0] == b[0] == c[0] == d[0]:      # both vertical, same x
                    lo, hi = sorted((a[1], b[1])); lo2, hi2 = sorted((c[1], d[1]))
                    if min(hi, hi2) > max(lo, lo2):
                        raise ValueError(f"overlapping vertical wires {a}-{b} and {c}-{d}")
                if a[1] == b[1] == c[1] == d[1]:      # both horizontal, same y
                    lo, hi = sorted((a[0], b[0])); lo2, hi2 = sorted((c[0], d[0]))
                    if min(hi, hi2) > max(lo, lo2):
                        raise ValueError(f"overlapping horizontal wires {a}-{b} and {c}-{d}")
        for inst in self.insts:
            for n in inst.pin_numbers():
                x, y = inst.pin_xy(n)
                p = (round(x / G), round(y / G))
                for a, b in segs:
                    if p in (a, b):
                        continue
                    if a[0] == b[0] == p[0] and min(a[1], b[1]) < p[1] < max(a[1], b[1]):
                        raise ValueError(f"wire {a}-{b} passes over {inst.ref} pin {n} at {p}")
                    if a[1] == b[1] == p[1] and min(a[0], b[0]) < p[0] < max(a[0], b[0]):
                        raise ValueError(f"wire {a}-{b} passes over {inst.ref} pin {n} at {p}")

    def render(self) -> str:
        self.lint()
        sch = ["kicad_sch", ["version", "20250610"], ["generator", Q("gen_schematic.py")],
               ["generator_version", Q("10.0")], ["uuid", Q(self.root_uuid)], ["paper", Q(self.paper)],
               ["title_block", ["title", Q(self.title)], ["date", Q(self.date)], ["rev", Q(self.rev)],
                ["company", Q(self.company)]]]
        libs = ["lib_symbols"]
        for key in sorted(self.libsyms):
            node = copy.deepcopy(self.libsyms[key].node)
            node[1] = Q(key)
            libs.append(node)
        sch.append(libs)
        for p in self._junctions():
            sch.append(["junction", ["at", f"{p[0]*G:.4f}", f"{p[1]*G:.4f}"], ["diameter", "0"],
                        ["color", "0", "0", "0", "0"], ["uuid", Q(uid())]])
        for p in self.no_connects:
            sch.append(["no_connect", ["at", f"{p[0]*G:.4f}", f"{p[1]*G:.4f}"], ["uuid", Q(uid())]])
        for a, b in self.wires:
            sch.append(["wire", ["pts", ["xy", f"{a[0]*G:.4f}", f"{a[1]*G:.4f}"],
                                 ["xy", f"{b[0]*G:.4f}", f"{b[1]*G:.4f}"]],
                        ["stroke", ["width", "0"], ["type", "default"]], ["uuid", Q(uid())]])
        for name, p, rot, justify in self.labels:
            sch.append(["label", Q(name), ["at", f"{p[0]*G:.4f}", f"{p[1]*G:.4f}", str(rot)],
                        ["effects", ["font", ["size", "1.27", "1.27"]], ["justify"] + justify.split()],
                        ["uuid", Q(uid())]])
        for s, p, size, bold, justify in self.texts:
            eff = ["effects", ["font", ["size", f"{size}", f"{size}"]] + ([["bold", "yes"]] if bold else []),
                   ["justify"] + justify.split()]
            sch.append(["text", Q(s), ["exclude_from_sim", "no"],
                        ["at", f"{p[0]*G:.4f}", f"{p[1]*G:.4f}", "0"], eff, ["uuid", Q(uid())]])
        for inst in self.insts:
            sch.append(self._inst_node(inst))
        sch.append(["sheet_instances", ["path", Q("/"), ["page", Q("1")]]])
        sch.append(["embedded_fonts", "no"])
        return dump(sch) + "\n"


# --------------------------------------------------------------------------
# Footprints (all stock KiCad 10 except SW1, see hardware/README.md)
# --------------------------------------------------------------------------
# Two passive sizes, deliberately. 1206 stays wherever the part is only made
# in it here (1 M, 1k5) or where the value is a C0G/X7R the 0603 order does not
# cover (330 pF, 100 nF); everything re-ordered as 0603 uses FP_R06. The bulk
# 10 uF is no longer an electrolytic at all - see FP_C08.
FP_R = "Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder"
FP_R06 = "Resistor_SMD:R_0603_1608Metric_Pad0.98x0.95mm_HandSolder"
FP_C = "Capacitor_SMD:C_1206_3216Metric_Pad1.33x1.80mm_HandSolder"
# C4/C5 were CP_Radial_D5.0mm_P2.50mm, a 5 mm radial electrolytic. They are now
# MLCC in 0805: non-polarised, 4 mm shorter, and they move to the bottom face
# (the routes that fed them were already there, through the old part's leads).
FP_C08 = "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder"
FP_D41 = "Diode_THT:D_DO-41_SOD81_P7.62mm_Horizontal"
FP_SOD123 = "Diode_SMD:D_SOD-123"
FP_SOT23 = "Package_TO_SOT_SMD:SOT-23"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_PICO = "Module:RaspberryPi_Pico_Common_THT"
FP_MSOP8 = "Package_SO:MSOP-8_3x3mm_P0.65mm"
FP_XH2 = "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical"
FP_XH4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
# J4 is ten separate breakout pads, not a connector: see hardware/README.md
FP_BRK = "TestPoint:TestPoint_THTPad_D1.5mm_Drill0.7mm"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"
FP_JP = "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm"
FP_JP3 = "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm"
FP_BTN = "Button_Switch_THT:SW_PUSH_6mm"
# 3.4 mm (ISO 273 medium) + the 8 mm boss keep-out; written by gen_mount_footprint.py
FP_HOLE = "handoff:MountingHole_3.4mm_M3_Boss8mm"
# drawn from the vendor drawing, see hardware/handoff.pretty
FP_SW = "handoff:SW_Slide_SS-12F23G5"


# --------------------------------------------------------------------------
# The sheet
# --------------------------------------------------------------------------
def build() -> Schematic:
    s = Schematic(paper="A3", title="Handoff — body-coupled handshake wristband", rev="A",
                  company="Handoff", date="2026-09-11")

    # ---- symbols ---------------------------------------------------------
    R = s.use("Device", "R")
    C = s.use("Device", "C")
    DSCH = s.use("Device", "D_Schottky")
    LED = s.use("Device", "LED_RGBK")
    BAT = s.use("Device", "Battery_Cell")
    SW = s.use("Switch", "SW_SPDT")
    TP = s.use("Connector", "TestPoint")
    JP = s.use("Jumper", "SolderJumper_2_Open")
    JP3S = s.use("Jumper", "SolderJumper_3_Open")
    BTN = s.use("Switch", "SW_Push")
    NFET = s.use("Transistor_FET", "AO3400A")
    MOTOR = s.use("Motor", "Motor_DC")
    HOLE = s.use("Mechanical", "MountingHole")
    C2 = s.use("Connector_Generic", "Conn_01x02")
    C4 = s.use("Connector_Generic", "Conn_01x04")
    C10 = s.use("Connector_Generic", "Conn_01x10")
    s.use("power", "GND")
    s.use("power", "+3V3")
    FLAG = s.use("power", "PWR_FLAG")

    def mk_power(ls: LibSym, name: str):
        ls.rename(name, "handoff")
        ls.set_prop("Value", name)
        ls.set_prop("Description", f'Power symbol creates a global label with name "{name}"')

    # our own power flags, cloned from +3V3
    for nm in ("VREF", "VSYS"):
        ls = LibSym("power", "+3V3", flatten("power", "+3V3"))
        mk_power(ls, nm)
        s.libsyms[ls.lib_id] = ls

    def adapt_pico(ls: LibSym):
        ls.set_prop("Value", "RaspberryPi_Pico_2_W")
        ls.set_prop("Footprint", FP_PICO)
        ls.set_prop("Datasheet", "https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf")
        ls.set_prop("Description", "Raspberry Pi Pico 2 W (RP2350 + CYW43439). 40-pin header identical to the Pico")
        # AGND is the same copper as GND on the module; two power outputs on one net is an ERC error
        for sub in find_all(ls.node, "symbol"):
            for p in find_all(sub, "pin"):
                if find(p, "name")[1] == "AGND":
                    p[1] = "passive"
        ls.pins = {k: (v[0], v[1], v[2], v[3], "passive" if v[3] == "AGND" else v[4]) for k, v in ls.pins.items()}

    def adapt_opamp(ls: LibSym):
        ls.set_prop("Value", "MCP6292-E/MS")
        ls.set_prop("Footprint", FP_MSOP8)
        ls.set_prop("Datasheet", "https://ww1.microchip.com/downloads/en/DeviceDoc/21810e.pdf")
        ls.set_prop("Description", "10 MHz rail-to-rail dual op-amp, 2.4-6 V, MSOP-8")

    PICO = s.use("MCU_Module", "RaspberryPi_Pico_W", sch_name="RaspberryPi_Pico_2_W", adapt=adapt_pico)
    PICO.lib_id = "handoff:RaspberryPi_Pico_2_W"
    s.libsyms.pop("MCU_Module:RaspberryPi_Pico_2_W")
    s.libsyms[PICO.lib_id] = PICO
    OPA = s.use("Amplifier_Operational", "MCP6002-xMS", sch_name="MCP6292-xMS", adapt=adapt_opamp)
    OPA.lib_id = "handoff:MCP6292-xMS"
    s.libsyms.pop("Amplifier_Operational:MCP6292-xMS")
    s.libsyms[OPA.lib_id] = OPA

    # =====================================================================
    # 1. POWER   (top left)
    # =====================================================================
    s.text("POWER — cell, switch, OR-ing diode (design §6.1, README)", (12, 14), size=2.0, bold=True)
    # BT1 (off-board) on the left, J1 with its pins facing the cell (rot 0, flipped so
    # BAT+ is the upper row), SW1 -> D1 -> VSYS on a row above, GND dropped below.
    bt = s.place(BAT, "BT1", "KP384455 3.7V 1500mAh", (24, 34), rot=0, in_bom=True, on_board=False,
                 desc="Li-ion cell, off-board, on a JST-XH pigtail", ref_at=(-2, -6, "center"), val_at=(-2, 6, "center"))
    bp, bn = s.gpin("BT1", "1"), s.gpin("BT1", "2")           # + (top), - (bottom)
    j1 = s.place(C2, "J1", "BAT XH-2", (bn[0] + 16, bn[1]), rot=0, mirror="x", fp=FP_XH2,
                 desc="Battery input, JST-XH 2p. Pin 1 = BAT-, pin 2 = BAT+", ref_at=(2, -4), val_at=(-2, 4, "center"))
    j1p1, j1p2 = s.gpin("J1", "1"), s.gpin("J1", "2")          # pin 1 = BAT- (lower), pin 2 = BAT+ (upper)
    assert j1p1[1] == bn[1] and j1p2[1] == bn[1] - 2
    s.wire(bn, j1p1)
    s.wire(bp, (bp[0] + 4, bp[1]), (bp[0] + 4, j1p2[1]), j1p2)
    # board side: BAT+ up and over to the switch, BAT- down to ground
    tap_p = (bp[0] + 8, j1p2[1])
    tap_n = (bn[0] + 10, bn[1])
    row = j1p2[1] - 8
    sw = s.place(SW, "SW1", "SS-12F23G5", (tap_p[0] + 20, row), rot=0, fp=FP_SW, desc="Power switch, SPDT slide, right angle",
                 ref_at=(0, -5, "center"), val_at=(0, -7, "center"))
    swb, swa, swc = s.gpin("SW1", "2"), s.gpin("SW1", "1"), s.gpin("SW1", "3")   # common, throw to D1, spare
    s.wire(tap_p, (tap_p[0], swb[1]), (tap_p[0] + 8, swb[1]), swb)
    s.label("BAT+", (tap_p[0], swb[1]), 0, "left bottom")
    # TP10: the cell voltage, before D1
    # J5: the charger's plug, in parallel with J1 on the cell side of SW1, same pin order.
    # Not a power input: it exists so the TP4056 plugs in beside the cell instead of replacing it.
    j5 = s.place(C2, "J5", "CHG XH-2", (j1p2[0] + 16, bn[1]), rot=0, mirror="x", fp=FP_XH2,
                 desc="Charger port, JST-XH 2p, parallel to J1: TP4056 OUT- to pin 1, OUT+ to pin 2. Not a power input", ref_at=(2, -4), val_at=(0, 4, "center"))
    j5p1, j5p2 = s.gpin("J5", "1"), s.gpin("J5", "2")
    assert j5p1[1] == bn[1] and j5p2[1] == j1p2[1]
    s.wire((tap_p[0], j1p2[1] - 4), (j5p2[0] - 4, j1p2[1] - 4), (j5p2[0] - 4, j5p2[1]), j5p2)   # BAT+ over the top of J1
    s.wire(tap_n, (tap_n[0], tap_n[1] + 6), (j5p1[0] - 2, tap_n[1] + 6), (j5p1[0] - 2, j5p1[1]), j5p1)   # BAT- under J1
    s.power("GND", (tap_n[0], tap_n[1] + 6))
    s.no_connect(swc)
    d1 = s.place(DSCH, "D1", "1N5819", (swa[0] + 10, swa[1]), rot=180, fp=FP_D41, desc="VSYS OR-ing / reverse polarity",
                 ref_at=(0, -3, "center"), val_at=(0, 3, "center"))
    da, dk = s.gpin("D1", "2"), s.gpin("D1", "1")  # A, K
    s.wire(swa, da)
    s.label("SW_OUT", (swa[0] + 1, swa[1]), 0, "left bottom")
    # D1 cathode -> JP5 -> VSYS. JP5 open: the whole board's current through an ammeter TP7 -> TP6.
    n1 = (dk[0] + 2, dk[1])
    jp5 = s.place(JP, "JP5", "VSYS", (n1[0] + 6, n1[1]), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to feed VSYS from the cell; open, an ammeter from TP7 (D1_K) to TP6 (VSYS) reads the board current",
                  ref_at=(-3, 3), val_at=(1, 3))
    a5, b5 = s.gpin("JP5", "1"), s.gpin("JP5", "2")
    s.wire(dk, n1, a5)
    s.label("D1_K", (n1[0], n1[1]), 0, "left bottom")
    vsys_p = (b5[0] + 2, b5[1])
    s.wire(b5, vsys_p, (vsys_p[0], vsys_p[1] - 2))
    s.power("VSYS", (vsys_p[0], vsys_p[1] - 2))
    s.place(FLAG, "#FLG01", "PWR_FLAG", (vsys_p[0] + 2, vsys_p[1] + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(vsys_p, (vsys_p[0] + 2, vsys_p[1]), (vsys_p[0] + 2, vsys_p[1] + 4))

    s.text("VBUS (USB) and VSYS are OR'd inside the Pico; D1 stops VSYS back-feeding the cell and makes a reversed J1 harmless.", (12, 50), size=1.27)
    s.text("~0.35 V drop: VSYS 2.6-3.8 V, Pico needs 1.8-5.5 V. J5 is the charger's plug, wired to the cell, NOT a power input.", (12, 52), size=1.27)
    s.text("JP5 ships OPEN: bridge it to run from the cell (USB works regardless); open, TP7 -> TP6 is the ammeter position.", (12, 54), size=1.27)

    # =====================================================================
    # 2. VREF bias  (mid top)
    # =====================================================================
    s.text("VREF = 3V3/2 = 1.65 V (design §6.2)", (110, 14), size=2.0, bold=True)
    r10 = s.place(R, "R10", "100k", (120, 26), fp=FP_R06, desc="VREF divider, top")
    r11 = s.place(R, "R11", "100k", (120, 40), fp=FP_R06, desc="VREF divider, bottom")
    c4 = s.place(C, "C4", "10uF 100V", (132, 40), fp=FP_C08, desc="VREF hold-up, 0805 MLCC")
    top = s.gpin("R10", "1"); mid1 = s.gpin("R10", "2"); mid2 = s.gpin("R11", "1"); bot = s.gpin("R11", "2")
    c4p, c4n = s.gpin("C4", "1"), s.gpin("C4", "2")
    # the divider hangs off the AFE side of JP4, so a bench supply on JP4 pad 2 powers the whole analogue side
    s.wire(top, (top[0], top[1] - 3)); s.label("AFE_3V3", (top[0], top[1] - 3), 90, "left bottom")
    s.wire(mid1, mid2)
    s.wire(bot, (bot[0], bot[1] + 2)); s.power("GND", (bot[0], bot[1] + 2))
    vref_y = (mid1[1] + mid2[1]) // 2 if (mid1[1] + mid2[1]) % 2 == 0 else mid1[1]
    s.wire((mid1[0], vref_y), (c4p[0], vref_y), c4p)
    s.wire(c4n, (c4n[0], bot[1] + 2), (bot[0], bot[1] + 2))
    # JP6: divider -> VREF. Open, an external bias goes in on JP6 pad 2.
    jp6 = s.place(JP, "JP6", "VREF", (c4p[0] + 7, vref_y), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to connect the R10/R11/C4 divider to VREF; open, inject a bias on pad 2",
                  ref_at=(-3, 3), val_at=(1, 3))
    a6, b6 = s.gpin("JP6", "1"), s.gpin("JP6", "2")
    s.wire((c4p[0], vref_y), a6)
    vref_out = (b6[0] + 4, vref_y)
    s.wire(b6, vref_out, (vref_out[0], vref_out[1] - 2))
    s.power("VREF", (vref_out[0], vref_out[1] - 2))
    flag_x = vref_out[0] + 6
    s.place(FLAG, "#FLG02", "PWR_FLAG", (flag_x, vref_out[1] + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(vref_out, (flag_x, vref_out[1]), (flag_x, vref_out[1] + 4))

    # =====================================================================
    # 3. Op-amp supply + decoupling (right top)
    # =====================================================================
    s.text("U2 supply — AFE_3V3 behind JP4 (design §6.4 decoupling + C5 bulk)", (190, 14), size=2.0, bold=True)
    u2c = s.place(OPA, "U2", "MCP6292-E/MS", (204, 36), unit=3, fp=FP_MSOP8, ref_at=(-4, -1, "right"), val_at=(-4, 1, "right"))
    c3 = s.place(C, "C3", "100nF", (220, 36), fp=FP_C, desc="U2 decoupling, across pins 8 and 4")
    c5 = s.place(C, "C5", "10uF 100V", (230, 36), fp=FP_C08, desc="U2 bulk decoupling against the Pico SMPS, 0805 MLCC")
    vp, vn = s.gpin("U2", "8", 3), s.gpin("U2", "4", 3)
    c3a, c3b = s.gpin("C3", "1"), s.gpin("C3", "2")
    c5a, c5b = s.gpin("C5", "1"), s.gpin("C5", "2")
    yt, yb = vp[1] - 2, vn[1] + 2
    rail_end = (246, yt)
    s.wire(vp, (vp[0], yt), (vp[0] + 4, yt), (240, yt), rail_end)
    s.label("AFE_3V3", (vp[0] + 4, yt), 0, "left bottom")
    s.wire(c3a, (c3a[0], yt)); s.wire(c5a, (c5a[0], yt))
    s.wire(vn, (vn[0], yb), (max(c3b[0], c5b[0]), yb))   # the rail stopped at TP7 before
    s.wire(c3b, (c3b[0], yb)); s.wire(c5b, (c5b[0], yb))
    s.wire((vn[0], yb), (vn[0], yb + 2)); s.power("GND", (vn[0], yb + 2))
    # JP4: 3V3 -> U2 VDD. Open: U2 current through an ammeter across the jumper, or a bench
    # supply on pad 2 runs the AFE alone.
    s.wire((vp[0], yt), (vp[0], yt - 4))
    jp4 = s.place(JP, "JP4", "U2 VDD", (vp[0] - 8, yt - 4), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to power the AFE from the Pico's 3V3; open, meter U2's current across it or feed AFE_3V3 on pad 2",
                  ref_at=(0, -3, "center"), val_at=(0, 3, "center"))
    a4, b4 = s.gpin("JP4", "1"), s.gpin("JP4", "2")
    s.wire(b4, (vp[0], yt - 4))
    s.wire(a4, (a4[0], a4[1] - 2)); s.power("+3V3", (a4[0], a4[1] - 2))
    s.place(FLAG, "#FLG03", "PWR_FLAG", (rail_end[0], yt + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(rail_end, (rail_end[0], yt + 4))

    # =====================================================================
    # 4. PICO  (left middle)
    # =====================================================================
    s.text("U1 — Pico 2 W, socketed on two 1x20 female strips", (12, 60), size=2.0, bold=True)
    u1 = s.place(PICO, "U1", "Raspberry Pi Pico 2 W", (46, 100), fp=FP_PICO, ref_at=(3, 29), val_at=(3, 31))
    # power pins on top: VSYS (39), VBUS (40), 3V3 (36)
    pvsys, pvbus, p3v3 = s.gpin("U1", "39"), s.gpin("U1", "40"), s.gpin("U1", "36")
    s.wire(pvsys, (pvsys[0], pvsys[1] - 3)); s.power("VSYS", (pvsys[0], pvsys[1] - 3))
    s.no_connect(pvbus)
    s.wire(p3v3, (p3v3[0], p3v3[1] - 3)); s.power("+3V3", (p3v3[0], p3v3[1] - 3))
    pgnd = s.gpin("U1", "3")
    s.wire(pgnd, (pgnd[0], pgnd[1] + 3)); s.power("GND", (pgnd[0], pgnd[1] + 3))
    pagnd = s.gpin("U1", "33")
    s.wire(pagnd, (pagnd[0] + 3, pagnd[1]), (pagnd[0] + 3, pagnd[1] + 3)); s.power("GND", (pagnd[0] + 3, pagnd[1] + 3))
    # unused pins
    for num in ("35", "37"):
        s.no_connect(s.gpin("U1", num))
    used_left = {"5": "GP3", "6": "GP4", "7": "GP5", "15": "GP11_TX",
                 "19": "ROLE", "20": "BTN", "30": "RUN"}
    used_right = {"22": "LED_R", "24": "LED_G", "25": "LED_B", "26": "GP20", "27": "GP21",
                  "29": "GP22", "31": "ADC0", "32": "GP27_ADC1", "34": "MOT_DRV"}
    gp_left = ["1", "2", "4", "5", "6", "7", "9", "10", "11", "12", "14", "15", "16", "17", "19", "20"]
    gp_right = ["21", "22", "24", "25", "26", "27", "29", "31", "32", "34"]
    for num in gp_left + ["30"]:
        p = s.gpin("U1", num)
        if num in used_left:
            s.wire(p, (p[0] - 6, p[1]))
            s.label(used_left[num], (p[0] - 6, p[1]), 0, "right bottom")
        else:
            s.no_connect(p)
    for num in gp_right:
        p = s.gpin("U1", num)
        if num in used_right:
            s.wire(p, (p[0] + 6, p[1]))
            s.label(used_right[num], (p[0] + 6, p[1]), 0, "left bottom")
        else:
            s.no_connect(p)

    # =====================================================================
    # 5. ANALOGUE FRONT END  (right middle)  design §6.3 / §6.4
    # =====================================================================
    s.text("ANALOGUE — TX drive, shared PAD, x121 receive chain (design §6.3, §6.4)", (110, 60), size=2.0, bold=True)
    Y = 100  # signal row
    # -- TX: GP11 -> JP2 -> R1 -> PAD node
    jp2 = s.place(JP, "JP2", "TX", (118, Y - 12), rot=0, fp=FP_JP, desc="Open as shipped: bridge with solder to connect GP11 to the pad (DC-coupled); or fit C6 instead",
                  ref_at=(-3, 3), val_at=(1, 3))
    r1 = s.place(R, "R1", "1M", (134, Y - 12), rot=90, fp=FP_R, desc="TX safety resistor, design §13")
    a, b = s.gpin("JP2", "1"), s.gpin("JP2", "2")
    s.wire((a[0] - 8, a[1]), a); s.label("GP11_TX", (a[0] - 8, a[1]), 0, "right bottom")
    s.wire(b, s.gpin("R1", "1"))
    # C6, DNP, across JP2: AC-couples the TX drive so GP11's leakage (RP2350-E9) cannot reach the pad node.
    # Fit only if the M3/M7 leakage measurement says so; then JP2 stays open.
    c6 = s.place(C, "C6", "330pF C0G (DNP)", (118, Y - 20), rot=90, fp=FP_C, dnp=True,
                 desc="DNP. Optional TX DC block in parallel with JP2; fit instead of bridging JP2 if GP11 leakage biases the pad",
                 ref_at=(12, -1, "center"), val_at=(12, 1, "center"))
    c6a, c6b = s.gpin("C6", "1"), s.gpin("C6", "2")
    s.wire(a, (a[0], c6a[1]), c6a)
    s.wire(b, (b[0], c6b[1]), c6b)
    pad_node = (150, Y)
    r1b = s.gpin("R1", "2")
    s.wire(r1b, (pad_node[0], r1b[1]), pad_node)
    # -- PAD connector J2
    j2 = s.place(C2, "J2", "PAD JST-XH 2p", (140, Y), rot=180, fp=FP_XH2,
                 desc="Pin 1 = skin PAD, pin 2 = ground-plane electrode (optional)", ref_at=(-6, -4, "center"), val_at=(-6, 6, "center"))
    j2p1, j2p2 = s.gpin("J2", "1"), s.gpin("J2", "2")   # pin 1 (PAD) on row Y, pin 2 (GND plane) above it
    assert j2p1[1] == Y and j2p2[1] == Y - 2
    s.wire(j2p1, pad_node)
    s.wire(j2p2, (j2p2[0] + 2, j2p2[1]), (j2p2[0] + 2, j2p2[1] - 2))
    s.power("GND", (j2p2[0] + 2, j2p2[1] - 2), rot=180)
    s.label("PAD", (j2p1[0] + 1, j2p1[1]), 0, "left bottom")
    s.text("ground-plane electrode: optional, see README", (j2p1[0] - 22, Y + 8), size=1.0)
    # -- RX stage 1: PAD -> R2 -> U2A+ ; R3 to VREF ; R4/R5 feedback
    # JP7: PAD -> receiver. Open, the receiver is driven from JP7 pad 2 with the pad and TX out of the picture.
    jp7 = s.place(JP, "JP7", "RX", (156, Y), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to connect the pad to the receiver; open, drive the receiver from pad 2",
                  ref_at=(-3, 3), val_at=(1, 3))
    s.wire(pad_node, s.gpin("JP7", "1"))
    r2 = s.place(R, "R2", "1M", (170, Y), rot=90, fp=FP_R, desc="RX safety resistor, design §13")
    s.wire(s.gpin("JP7", "2"), (164, Y), s.gpin("R2", "1"))
    u2a = s.place(OPA, "U2", "MCP6292-E/MS", (196, Y + 2), unit=1, fp=FP_MSOP8, ref_at=(-1, 0, "center"), hide_value=True)
    ina_p, ina_n, outa = s.gpin("U2", "3", 1), s.gpin("U2", "2", 1), s.gpin("U2", "1", 1)
    hiz = (ina_p[0] - 8, ina_p[1])
    s.wire(s.gpin("R2", "2"), hiz, ina_p)
    r3 = s.place(R, "R3", "1M", (hiz[0], Y + 10), rot=0, fp=FP_R, desc="Biases the 10 MOhm node to VREF", ref_at=(-2, -1, "right"), val_at=(-2, 1, "right"))
    s.wire(hiz, s.gpin("R3", "1"))
    s.text("10 MOhm node: keep tiny, guard it (README)", (hiz[0] - 10, Y + 24), size=1.0)
    # feedback: R4 above the op-amp from OUT A back to -IN A; R5 from -IN A down to VREF
    fb1 = (ina_n[0] - 2, ina_n[1])
    r5 = s.place(R, "R5", "10k", (fb1[0], fb1[1] + 6), rot=0, fp=FP_R06, desc="Stage 1 gain set: 1 + R4/R5 = 11", ref_at=(2, -1), val_at=(2, 1))
    r4 = s.place(R, "R4", "100k", (ina_n[0] + 8, ina_n[1] - 12), rot=90, fp=FP_R06, desc="Stage 1 feedback")
    s.wire(ina_n, fb1, s.gpin("R5", "1"))
    r3b, r5b = s.gpin("R3", "2"), s.gpin("R5", "2")
    assert r3b[1] == r5b[1]
    mid = ((r3b[0] + r5b[0]) // 2, r3b[1])
    s.wire(r3b, r5b); s.wire(mid, (mid[0], mid[1] + 2)); s.power("VREF", (mid[0], mid[1] + 2), rot=180)
    r4a, r4b = s.gpin("R4", "1"), s.gpin("R4", "2")
    s.wire(fb1, (fb1[0], r4a[1]), r4a)
    s.wire(r4b, (outa[0] + 2, r4b[1]), (outa[0] + 2, outa[1]))
    s.wire(outa, (outa[0] + 2, outa[1]))
    # -- interstage C1 / R6
    c1 = s.place(C, "C1", "330pF C0G", (outa[0] + 14, outa[1]), rot=90, fp=FP_C, desc="Interstage DC block")
    s.wire((outa[0] + 2, outa[1]), (outa[0] + 7, outa[1]), s.gpin("C1", "1"))
    s.label("OUT1", (outa[0] + 7, outa[1]), 0, "left bottom")
    c1b = s.gpin("C1", "2")
    in2 = (c1b[0] + 4, c1b[1])
    s.wire(c1b, in2)
    r6 = s.place(R, "R6", "100k", (in2[0], in2[1] + 10), rot=0, fp=FP_R06, desc="Re-bias stage 2 +IN to VREF", ref_at=(-2, -1, "right"), val_at=(-2, 1, "right"))
    s.wire(in2, s.gpin("R6", "1"))
    # -- stage 2
    u2b = s.place(OPA, "U2", "MCP6292-E/MS", (in2[0] + 14, in2[1] + 2), unit=2, fp=FP_MSOP8, ref_at=(-1, 0, "center"), hide_value=True)
    inb_p, inb_n, outb = s.gpin("U2", "5", 2), s.gpin("U2", "6", 2), s.gpin("U2", "7", 2)
    s.wire(in2, inb_p)
    fb2 = (inb_n[0] - 2, inb_n[1])
    r8 = s.place(R, "R8", "10k", (fb2[0], fb2[1] + 6), rot=0, fp=FP_R06, desc="Stage 2 gain set: 1 + R7/R8 = 11", ref_at=(2, -1), val_at=(2, 1))
    r7 = s.place(R, "R7", "100k", (inb_n[0] + 8, inb_n[1] - 12), rot=90, fp=FP_R06, desc="Stage 2 feedback")
    s.wire(inb_n, fb2, s.gpin("R8", "1"))
    r6b, r8b = s.gpin("R6", "2"), s.gpin("R8", "2")
    assert r6b[1] == r8b[1]
    mid = ((r6b[0] + r8b[0]) // 2, r6b[1])
    s.wire(r6b, r8b); s.wire(mid, (mid[0], mid[1] + 2)); s.power("VREF", (mid[0], mid[1] + 2), rot=180)
    r7a, r7b = s.gpin("R7", "1"), s.gpin("R7", "2")
    s.wire(fb2, (fb2[0], r7a[1]), r7a)
    s.wire(r7b, (outb[0] + 2, r7b[1]), (outb[0] + 2, outb[1]))
    s.wire(outb, (outb[0] + 2, outb[1]))
    # -- JP3: which stage feeds the ADC. Centre (pin 2) -> R9; pin 3 <- OUT2 (x121); pin 1 <- OUT1 (x11).
    s.wire((outb[0] + 2, outb[1]), (outb[0] + 9, outb[1]))
    jp3 = s.place(JP3S, "JP3", "STAGE", (outb[0] + 13, outb[1] + 7), rot=180, fp=FP_JP3,
                  desc="Open as shipped. Bridge centre to pin 3 (OUT2) for x121, or centre to pin 1 (OUT1) for x11 if stage 2 clips",
                  ref_at=(-4, 3), val_at=(2, 3))
    j3a, j3c, j3b = s.gpin("JP3", "1"), s.gpin("JP3", "2"), s.gpin("JP3", "3")
    assert j3b[0] == outb[0] + 9 and j3c[0] == outb[0] + 13 and j3a[0] == outb[0] + 17, (j3a, j3c, j3b)
    s.wire((outb[0] + 9, outb[1]), j3b)                               # OUT2 -> pin 3
    s.wire(j3a, (j3a[0] + 3, j3a[1])); s.label("OUT1", (j3a[0] + 3, j3a[1]), 0, "left bottom")   # OUT1 -> pin 1
    # -- output filter R9, JP1, C2 -> ADC0
    r9 = s.place(R, "R9", "1k5", (outb[0] + 16, outb[1]), rot=90, fp=FP_R, desc="ADC series / anti-alias with C2")
    s.wire(j3c, (j3c[0], outb[1]), s.gpin("R9", "1"))                 # centre -> R9
    jp1 = s.place(JP, "JP1", "ADC", (outb[0] + 28, outb[1]), rot=0, fp=FP_JP, desc="Open as shipped: bridge with solder to connect the AFE to the ADC pin",
                  ref_at=(-3, 3), val_at=(1, 3))
    s.wire(s.gpin("R9", "2"), s.gpin("JP1", "1"))
    adc = s.gpin("JP1", "2")
    adc_node = (adc[0] + 4, adc[1])
    s.wire(adc, adc_node, (adc_node[0] + 6, adc_node[1]))
    s.label("ADC0", (adc_node[0] + 6, adc_node[1]), 0, "left bottom")
    c2 = s.place(C, "C2", "330pF C0G", (adc_node[0], adc_node[1] + 6), rot=0, fp=FP_C, desc="Anti-alias, 321 kHz with R9", ref_at=(2, -1), val_at=(2, 1))
    s.wire(adc_node, s.gpin("C2", "1"))
    c2b = s.gpin("C2", "2")
    s.wire(c2b, (c2b[0], c2b[1] + 2)); s.power("GND", (c2b[0], c2b[1] + 2))
    # a scope ground next to the analogue test pads (TP7 is up by U2's supply)
    s.text("Gain 11 x 11 = 121. Firmware holds GP11 high-Z with its input buffer OFF while receiving (RP2350-E9), so R1 does not load or bias the pad.", (110, 136), size=1.27)
    s.text("Every JP ships OPEN: bridge with solder to bring the board up one stage at a time (README order); wick off to isolate. No test pad on the 10 MOhm node on purpose.", (110, 138), size=1.27)
    s.text("JP3: centre -> R9. Blob to pin 3 = stage 2 (x121); move it to pin 1 = stage 1 only (x11) if stage 2 clips. C6 is DNP: fit it instead of bridging JP2 to AC-couple TX.", (110, 140), size=1.27)

    # =====================================================================
    # 6. STATUS LED  (bottom left)
    # =====================================================================
    s.text("STATUS LED — RGB common cathode on GP17/18/19, direct-solder or JST-XH 4p", (12, 150), size=2.0, bold=True)
    LY = 168
    # J3 rot 0: pins on the left, rows LY (R), LY+2 (K), LY+4 (G), LY+6 (B); each series
    # resistor sits on its own pin row, staggered in x so nothing crosses.
    j3 = s.place(C4, "J3", "LED JST-XH 4p / 5mm RGB", (64, LY), rot=0, fp=FP_XH4,
                 desc="Pin order R, K, G, B = 5 mm common-cathode RGB LED leads", ref_at=(2, -2), val_at=(0, 10))
    for ref, net, colour, pinnum, x, drop, col in (("R12", "LED_R", "R", "1", 34, 0, None),
                                                   ("R13", "LED_G", "G", "3", 42, 6, 56),
                                                   ("R14", "LED_B", "B", "4", 50, 10, 58)):
        jp = s.gpin("J3", pinnum)
        r = s.place(R, ref, "100", (x, jp[1] + drop), rot=90, fp=FP_R06, desc=f"LED {colour} series",
                    **({"ref_at": (-2, -2, "center"), "val_at": (3, -2, "center")} if drop == 0 else {}))
        pa, pb = s.gpin(ref, "1"), s.gpin(ref, "2")
        s.wire((pa[0] - 4, pa[1]), pa); s.label(net, (pa[0] - 4, pa[1]), 0, "right bottom")
        if col is None:
            s.wire(pb, jp)
        else:
            s.wire(pb, (col, pb[1]), (col, jp[1]), jp)
        s.label("J3_" + colour, (pb[0] + 1, pb[1]), 0, "left bottom")
    jk = s.gpin("J3", "2")
    s.wire(jk, (24, jk[1]), (24, jk[1] + 14)); s.power("GND", (24, jk[1] + 14))
    # the LED itself, off-board, shown plugged into J3 by net name
    d2 = s.place(LED, "D2", "RGB 5mm CC", (92, LY + 4), rot=0, mirror="y", in_bom=True, on_board=False,
                 desc="Off-board LED, plugs into / solders into J3", ref_at=(4, -3), val_at=(4, 8))
    for dpin, colour in (("1", "R"), ("2", "G"), ("3", "B")):
        dp = s.gpin("D2", dpin)
        s.wire((dp[0] - 4, dp[1]), dp); s.label("J3_" + colour, (dp[0] - 4, dp[1]), 0, "right bottom")
    dk = s.gpin("D2", "4")
    s.wire(dk, (dk[0] + 2, dk[1]), (dk[0] + 2, dk[1] + 8)); s.power("GND", (dk[0] + 2, dk[1] + 8))
    s.text("(D2 drawn for reference: it is the LED that plugs into J3, not a board component)", (12, 190), size=1.0)

    # =====================================================================
    # 7. EXPANSION header (bottom middle)
    # =====================================================================
    s.text("E1-E10 EXPANSION — one breakout pad inboard of its own Pico pin", (150, 150), size=2.0, bold=True)
    # Ten separate pads, not a 1x10 header. The floor plan puts each pad directly
    # inboard of the Pico pin it breaks out, which is ten scattered positions -
    # a single 1x10 footprint cannot describe that, and a connector symbol whose
    # pins are nowhere near each other lies to anyone reading the sheet.
    # Pins 1, 2 and 3 are OUT: at x 31.44 on the little-finger wall their pads
    # reach x 32.19, which is 3.3 mm from H4's centre - inside the 8 mm boss the
    # mounting screw stands in. The wall is only free between J5's housing and
    # that boss, which is pins 5-8, so GP0/GP1 give way to GP3 and a ground pad
    # beside the others, and GP1's slot moves to GP22 on the thumb row.
    exp = [("E1", "+3V3", "3V3", 36), ("E2", "RUN", "RUN", 30), ("E3", "GP3", "GP3", 5),
           ("E4", "GP22", "GP22", 29), ("E5", "GP4", "GP4", 6), ("E6", "GP5", "GP5", 7),
           ("E7", "GP20", "GP20", 26), ("E8", "GP21", "GP21", 27),
           ("E9", "GP27_ADC1", "GP27", 32), ("E10", "GND", "GND", 8)]
    for i, (ref, net, lab, picopin) in enumerate(exp):
        x = 176 + 34 * (i // 5)
        y = 162 + 8 * (i % 5)
        s.place(TP, ref, f"{lab} (pin {picopin})", (x, y), fp=FP_BRK,
                desc=f"Expansion breakout pad, Pico pin {picopin} ({lab}); placed inboard of that pin",
                ref_at=(-2, -2, "right"), val_at=(2, -2, "left"))
        pin = s.gpin(ref, "1")
        if net == "+3V3":
            s.wire(pin, (pin[0] - 6, pin[1])); s.power("+3V3", (pin[0] - 6, pin[1]), rot=270)
        elif net == "GND":
            s.wire(pin, (pin[0] - 6, pin[1])); s.power("GND", (pin[0] - 6, pin[1]), rot=90)
        else:
            s.wire(pin, (pin[0] - 6, pin[1])); s.label(net, (pin[0] - 6, pin[1]), 0, "right bottom")
    s.text("Through-hole, 1.5 mm pad / 0.7 mm drill: a wire solders in. RUN to GND is a reset.", (150, 205), size=1.0)

    # =====================================================================
    # 8. TEST PADS (bottom middle, right of the expansion block)
    # =====================================================================
    # Eight, in the README's bring-up priority order, on the nets a scope or
    # meter has to see that no jumper or connector pin already exposes well:
    # a ground for the scope's clip next to the AFE, the ADC input, stage 1's
    # output, the bias, the AFE supply, the pad (10x probe only), and the
    # ammeter pair either side of JP5. Same through-hole pad as E1-E10.
    s.text("TP1-TP8 TEST PADS — bring-up order (README)", (222, 150), size=2.0, bold=True)
    tps = [("TP1", "GND", "GND, scope clip, beside U2"), ("TP2", "ADC0", "ADC input"),
           ("TP3", "OUT1", "stage 1 output"), ("TP4", "VREF", "1.65 V bias"),
           ("TP5", "PAD", "receive node: 10x probe only"),
           ("TP6", "VSYS", "ammeter +, after JP5"), ("TP7", "D1_K", "ammeter -, before JP5"),
           ("TP8", "AFE_3V3", "AFE supply behind JP4 - C5 is SMD now, so its lead is no longer a probe point")]
    for i, (ref, net, what) in enumerate(tps):
        x = 248 + 34 * (i // 4)
        y = 162 + 8 * (i % 4)
        s.place(TP, ref, net, (x, y), fp=FP_BRK, desc=f"Test pad, {what}",
                ref_at=(-2, -2, "right"), val_at=(2, -2, "left"))
        pin = s.gpin(ref, "1")
        s.wire(pin, (pin[0] - 6, pin[1]))
        if net in ("GND", "VSYS", "VREF"):
            s.power(net, (pin[0] - 6, pin[1]), rot=90 if net == "GND" else 270)
        else:
            s.label(net, (pin[0] - 6, pin[1]), 0, "right bottom")
    s.text("TP5 loads a 10 MOhm node through R1: a 10x probe or better, never a meter. TP6/TP7 straddle JP5: the ammeter position.", (222, 205), size=1.0)
    s.text("TP8 is new: C4 and C5 are 0805 MLCC now, so the through-hole + lead that used to be the AFE_3V3 probe point is gone.", (222, 207), size=1.0)
    s.text("The spare ADC (GP27) is deliberate: a second analogue path is the likeliest hack this board will need.", (150, 207), size=1.0)

    # =====================================================================
    # 8. BUTTON, ROLE STRAP, MOUNTING  (bottom right)
    # =====================================================================
    s.text("BUTTON (GP15), ROLE strap (GP14), MOUNTING", (238, 150), size=2.0, bold=True)
    BX, BY = 250, 164
    r15 = s.place(R, "R15", "10k", (BX, BY), rot=0, fp=FP_R06, desc="BTN pull-up", ref_at=(2, -1), val_at=(2, 1))
    r15a, r15b = s.gpin("R15", "1"), s.gpin("R15", "2")
    s.wire(r15a, (r15a[0], r15a[1] - 2)); s.power("+3V3", (r15a[0], r15a[1] - 2))
    nb = (BX, BY + 5)
    s.wire(r15b, nb, (nb[0] - 8, nb[1])); s.label("BTN", (nb[0] - 8, nb[1]), 0, "right bottom")
    sw2 = s.place(BTN, "SW2", "TACT 6x6", (nb[0] + 6, nb[1]), rot=0, fp=FP_BTN,
                  desc="Bench control: force TX / force RX / provisioning / clear bond. Pads 1-1 and 2-2 are the switch's internally joined pairs",
                  ref_at=(2, -3), val_at=(-3, 3))
    b1, b2 = s.gpin("SW2", "1"), s.gpin("SW2", "2")
    s.wire(nb, b1)
    s.wire(b2, (b2[0] + 2, b2[1]), (b2[0] + 2, b2[1] + 4)); s.power("GND", (b2[0] + 2, b2[1] + 4))
    s.text("no debounce cap: debounce in firmware", (BX - 16, BY + 14), size=1.0)
    # JP8: strap GP14 to GND to pick the board's role for M6 (firmware enables the internal pull-up)
    jy = BY + 18
    jp8 = s.place(JP, "JP8", "ROLE", (BX + 8, jy), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to strap GP14 low = board role for M6 (internal pull-up in firmware)",
                  ref_at=(-3, 3), val_at=(1, 3))
    a8, b8 = s.gpin("JP8", "1"), s.gpin("JP8", "2")
    s.wire((BX - 8, jy), a8); s.label("ROLE", (BX - 8, jy), 0, "right bottom")
    s.wire(b8, (b8[0] + 2, jy), (b8[0] + 2, jy + 4)); s.power("GND", (b8[0] + 2, jy + 4))
    for i in range(4):
        s.place(HOLE, f"H{i + 1}", "M3", (282 + 10 * i, BY + 2), fp=FP_HOLE, desc="Mounting hole, M3 clearance, unplated",
                in_bom=False, ref_at=(0, -3, "center"), val_at=(0, 3, "center"))
    s.text("H1-H4: 3.4 mm unplated, 8 mm boss keep-out, one per corner, not tied to GND", (274, BY + 8), size=1.0)

    # =====================================================================
    # 9. HAPTIC  (bottom, below the LED block)  - new
    # =====================================================================
    # A 10 mm coin motor is an inductive load drawing ~100 mA from a 3 V rail:
    # more than a GPIO can source by an order of magnitude, so it is switched
    # low-side by an N-channel MOSFET, and its collapse is caught by D3.
    #
    # Why VSYS and not +3V3: the Pico's own regulator has to carry the RP2350,
    # the CYW43439 radio and the whole analogue side. Hanging a 100 mA pulsed
    # load on it would put motor current through the same rail that feeds
    # AFE_3V3 through JP4. VSYS is 2.6-3.8 V, which is what a 3 V coin motor
    # wants, and it is diode-protected by D1 against a reversed cell.
    s.text("HAPTIC — 10 mm coin motor on VSYS, low-side AO3400A, gate on GP28", (12, 194), size=2.0, bold=True)
    HY = 208
    q1 = s.place(NFET, "Q1", "AO3400A", (60, HY), rot=0, fp=FP_SOT23,
                 desc="Low-side motor switch, N-channel 30 V / 5.2 A, 27 mOhm at 4.5 V, SOT-23",
                 ref_at=(5, 2), val_at=(5, 4))
    qg, qs, qd = s.gpin("Q1", "1"), s.gpin("Q1", "2"), s.gpin("Q1", "3")
    # -- gate chain: GP28 -> R16 -> gate, with R17 holding the gate down ------
    # R17 is not optional. Between power-up and the first firmware write - and
    # through every BOOTSEL reset - GP28 is an input, so without a pull-down
    # the gate floats and the motor may run. 100 k against a 100 R series
    # resistor loses 3 mV of drive, which is nothing.
    r16 = s.place(R, "R16", "100", (qg[0] - 12, qg[1]), rot=90, fp=FP_R06,
                  desc="Q1 gate series", ref_at=(0, -2, "center"), val_at=(0, 2, "center"))
    r16a, r16b = s.gpin("R16", "1"), s.gpin("R16", "2")
    s.wire((r16a[0] - 6, r16a[1]), r16a)
    s.label("MOT_DRV", (r16a[0] - 6, r16a[1]), 0, "right bottom")
    gate_t = (qg[0] - 6, qg[1])
    s.wire(r16b, gate_t, qg)
    r17 = s.place(R, "R17", "100k", (gate_t[0], gate_t[1] + 3), rot=0, fp=FP_R06,
                  desc="Q1 gate pull-down: holds the motor off while GP28 is an input",
                  ref_at=(2, -1), val_at=(2, 1))
    r17a, r17b = s.gpin("R17", "1"), s.gpin("R17", "2")
    assert r17a == gate_t          # R17 hangs straight off the gate node, no stub
    s.wire(r17b, (r17b[0], r17b[1] + 1)); s.power("GND", (r17b[0], r17b[1] + 1))
    s.wire(qs, (qs[0], qs[1] + 2)); s.power("GND", (qs[0], qs[1] + 2))
    # -- drain, the motor, and the flyback diode -----------------------------
    sw = qd
    s.label("MOT_SW", (sw[0] + 2, sw[1]), 0, "left bottom")
    top = (sw[0], sw[1] - 6)
    # rot 270 puts the cathode uppermost, so the diode reads the way it sits:
    # anode on the switched node, cathode on VSYS, reverse-biased until Q1 opens.
    d3 = s.place(DSCH, "D3", "1N5819W", (sw[0] + 10, sw[1] - 3), rot=270, fp=FP_SOD123,
                 desc="Motor flyback, Schottky 40 V 1 A, SOD-123", ref_at=(-2, -2, "right"), val_at=(-2, 0, "right"))
    d3k, d3a = s.gpin("D3", "1"), s.gpin("D3", "2")
    s.wire(sw, (d3a[0], sw[1]), d3a)
    s.wire(d3k, (d3k[0], top[1]), top)
    s.power("VSYS", top, rot=0)
    # J6: two 2.54 mm through-holes at the elbow. The motor's flying leads
    # solder straight in, or a 2-pin header takes a plug - the same choice the
    # LED has at J3. Pin 1 is the switched (low) side on every 2-pin connector
    # on this board, J1 and J5 included.
    j6 = s.place(C2, "J6", "MOT 1x02 2.54", (sw[0] + 26, sw[1]), rot=0, mirror="x", fp=FP_HDR2,
                 desc="Motor, 2 x 2.54 mm THT. Pin 1 = MOT_SW (switched), pin 2 = VSYS",
                 ref_at=(2, -4), val_at=(0, 4, "center"))
    j6p1, j6p2 = s.gpin("J6", "1"), s.gpin("J6", "2")
    s.wire((j6p1[0] - 6, j6p1[1]), j6p1); s.label("MOT_SW", (j6p1[0] - 6, j6p1[1]), 0, "right bottom")
    s.wire((j6p2[0] - 6, j6p2[1]), j6p2); s.power("VSYS", (j6p2[0] - 6, j6p2[1]), rot=270)
    # the motor itself, off-board on its leads, shown by net name like D2
    m1 = s.place(MOTOR, "M1", "1034 coin 10mm 3V", (j6p1[0] + 16, j6p1[1] + 2), rot=0,
                 in_bom=True, on_board=False, desc="Coin vibration motor, off-board, leads into J6",
                 ref_at=(5, -2), val_at=(5, 2))
    m1p, m1n = s.gpin("M1", "1"), s.gpin("M1", "2")
    s.wire(m1p, (m1p[0], m1p[1] - 2)); s.power("VSYS", (m1p[0], m1p[1] - 2), rot=0)
    s.wire(m1n, (m1n[0] - 6, m1n[1])); s.label("MOT_SW", (m1n[0] - 6, m1n[1]), 0, "right bottom")
    s.text("(M1 drawn for reference: it is the motor whose leads go into J6, not a board component.)  D3 is the flyback path: without it the", (150, 210), size=1.0)
    s.text("motor's collapse drives Q1's drain well above VSYS at every turn-off.  Firmware: GP28 high runs the motor - never during an RX window.", (150, 213), size=1.0)

    # Copper-only items are not purchasable parts, so they are not in the BOM.
    # Their footprints already carry exclude_from_bom; saying the same thing on
    # the symbol is what makes the PCB's schematic-parity check a real test
    # instead of 31 standing complaints.
    for inst in s.insts:
        if re.match(r"(TP|JP|H|E)\d+$", inst.ref):
            inst.in_bom = False

    return s


# --------------------------------------------------------------------------
# The oracle: what the netlist must say. (ref, pin) sets per net. Unit-3 pins
# of U2 are its supply.
# --------------------------------------------------------------------------
EXPECTED_NETS = {
    # every JPx is OPEN as shipped, so each one splits what the design doc calls one net into two
    "BAT+": {("J1", "2"), ("J5", "2"), ("SW1", "2")},
    "GND": {("J1", "1"), ("J5", "1"), ("U1", "3"), ("U1", "8"), ("U1", "13"), ("U1", "18"), ("U1", "23"),
            ("U1", "28"), ("U1", "33"), ("U1", "38"), ("U2", "4"), ("C3", "2"), ("C5", "2"), ("R11", "2"), ("C4", "2"),
            ("J2", "2"), ("C2", "2"), ("J3", "2"), ("E10", "1"), ("SW2", "2"), ("JP8", "2"), ("TP1", "1"),
            ("Q1", "2"), ("R17", "2")},
    "SW_OUT": {("SW1", "1"), ("D1", "2")},
    "D1_K": {("D1", "1"), ("JP5", "1"), ("TP7", "1")},
    # M1, like BT1 and D2, is off-board (on_board=False) and so is not in the netlist
    "VSYS": {("JP5", "2"), ("U1", "39"), ("TP6", "1"), ("D3", "1"), ("J6", "2")},
    "+3V3": {("U1", "36"), ("JP4", "1"), ("E1", "1"), ("R15", "1")},
    "AFE_3V3": {("JP4", "2"), ("U2", "8"), ("C3", "1"), ("C5", "1"), ("R10", "1"), ("TP8", "1")},
    "VREF_DIV": {("R10", "2"), ("R11", "1"), ("C4", "1"), ("JP6", "1")},
    "VREF": {("JP6", "2"), ("R3", "2"), ("R5", "2"), ("R6", "2"), ("R8", "2"), ("TP4", "1")},
    # haptic: gate chain, then the switched low side of the motor
    "MOT_DRV": {("U1", "34"), ("R16", "1")},
    "MOT_G": {("R16", "2"), ("R17", "1"), ("Q1", "1")},
    "MOT_SW": {("Q1", "3"), ("D3", "2"), ("J6", "1")},
    "GP11_TX": {("U1", "15"), ("JP2", "1"), ("C6", "1")},
    "JP2_R1": {("JP2", "2"), ("C6", "2"), ("R1", "1")},
    "PAD": {("R1", "2"), ("JP7", "1"), ("J2", "1"), ("TP5", "1")},
    "RX_IN": {("JP7", "2"), ("R2", "1")},
    "HIZ": {("R2", "2"), ("R3", "1"), ("U2", "3")},
    "FB1": {("U2", "2"), ("R5", "1"), ("R4", "1")},
    "OUT1": {("U2", "1"), ("R4", "2"), ("C1", "1"), ("JP3", "1"), ("TP3", "1")},
    "IN2": {("C1", "2"), ("R6", "1"), ("U2", "5")},
    "FB2": {("U2", "6"), ("R8", "1"), ("R7", "1")},
    "OUT2": {("U2", "7"), ("R7", "2"), ("JP3", "3")},
    "JP3_R9": {("JP3", "2"), ("R9", "1")},
    "R9_JP1": {("R9", "2"), ("JP1", "1")},
    "BTN": {("U1", "20"), ("R15", "2"), ("SW2", "1")},
    "ROLE": {("U1", "19"), ("JP8", "1")},
    "ADC0": {("JP1", "2"), ("C2", "1"), ("U1", "31"), ("TP2", "1")},
    "LED_R": {("U1", "22"), ("R12", "1")},
    "LED_G": {("U1", "24"), ("R13", "1")},
    "LED_B": {("U1", "25"), ("R14", "1")},
    "J3_R": {("R12", "2"), ("J3", "1")},
    "J3_G": {("R13", "2"), ("J3", "3")},
    "J3_B": {("R14", "2"), ("J3", "4")},
    "RUN": {("U1", "30"), ("E2", "1")},
    "GP3": {("U1", "5"), ("E3", "1")},
    "GP22": {("U1", "29"), ("E4", "1")},
    "GP4": {("U1", "6"), ("E5", "1")},
    "GP5": {("U1", "7"), ("E6", "1")},
    "GP20": {("U1", "26"), ("E7", "1")},
    "GP21": {("U1", "27"), ("E8", "1")},
    "GP27_ADC1": {("U1", "32"), ("E9", "1")},
}


# Net classes, and the track width each one is routed at. gen_pcb.py reads this
# table for its widths, so the project file and the board cannot disagree.
# The names are the board's net names: a pattern has to match the whole name,
# and local labels arrive on the board with a leading "/".
#   Power  0.5 mm  the cell's current: up to ~0.5 A with the radio on
#   Rail   0.4 mm  +3V3 past the Pico and the AFE's supply - under 5 mA, so
#                  the width is convention, not current; 0.4 rather than 0.5
#                  because the one lane past the antenna keep-out is 0.99 mm
#   Default 0.25   everything else
NET_CLASSES = {
    "Power": (0.5, ("GND", "VSYS", "/BAT+", "/SW_OUT", "/D1_K", "/MOT_SW")),
    "Rail": (0.4, ("+3V3", "/AFE_3V3")),
}


def net_settings():
    """net_settings in the shape KiCad 10 itself writes (meta version 5), so a
    save from pcbnew keeps it. An earlier version-4 write was lost the first
    time KiCad saved over it from a session that had loaded the older file."""
    def cls(name, width, prio):
        return {"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
                "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2, "line_style": 0,
                "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": name,
                "pcb_color": "rgba(0, 0, 0, 0.000)", "priority": prio,
                "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": width,
                "tuning_profile": "", "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6}
    classes = [cls("Default", 0.25, 2147483647)]
    patterns = []
    for i, (name, (width, nets)) in enumerate(NET_CLASSES.items()):
        classes.append(cls(name, width, i))
        patterns += [{"netclass": name, "pattern": n} for n in nets]
    return {"classes": classes, "meta": {"version": 5}, "net_colors": None,
            "netclass_assignments": None, "netclass_patterns": patterns}


def board_settings():
    """The part of the project file that is the board's rule set: DRC minimums,
    severities and net classes. gen_pcb.py re-applies exactly this after every
    save, because pcbnew's BOARD.Save() also rewrites the project file from
    the blank project it was built under - which is how the Power class went
    missing once. Written once here, applied from both generators."""
    return {
        # 0.15/0.15 and a 0.3 mm drill are inside every PCBWay 2-layer process.
        "board": {"design_settings": {"defaults": {}, "rules": {
            "min_clearance": 0.15,
            "min_track_width": 0.15,
            "min_connection": 0.0,
            "min_through_hole_diameter": 0.3,
            "min_hole_to_hole": 0.25,
            "min_hole_clearance": 0.25,
            "min_copper_edge_clearance": 0.3,
            "min_via_annular_width": 0.1,
            "min_via_diameter": 0.45,
            "min_silk_clearance": 0.0,
            "min_text_height": 0.8,
            "min_text_thickness": 0.08,
            "solder_mask_clearance": 0.0,
            # 0.0 switched the minimum-web test off. 0.1 mm is the dam PCBWay's
            # green mask holds; it is what JP1-JP8's 0.3 mm pad gaps and the
            # MSOP-8's 0.25 mm ones are checked against.
            "solder_mask_min_width": 0.1,
            "min_resolved_spokes": 2,
            "max_error": 0.005,
            "allow_blind_buried_vias": False,
            "allow_microvias": False,
            "use_height_for_length_calcs": True,
        },
            # gen_pcb.py deliberately moves some footprints' silkscreen outlines
            # to the fab layer (U1's box is drawn over everything that sits under
            # the socketed module). Those ten mismatches stay visible as warnings
            # - documented in the README - rather than being hidden, so a real
            # one could not hide among them. Everything a fab would reject, or
            # that the layout convention forbids, is an error.
            "rule_severities": {"lib_footprint_mismatch": "warning",
                                "silk_over_copper": "error",
                                "silk_overlap": "error",
                                "silk_edge_clearance": "error",
                                "text_height": "error",
                                "text_thickness": "error",
                                "connection_width": "error",
                                "isolated_copper": "error",
                                "copper_sliver": "error",
                                "track_dangling": "error",
                                "via_dangling": "error",
                                "hole_to_hole": "error",
                                "holes_co_located": "error",
                                "track_angle": "error",
                                "track_segment_length": "error",
                                "mirrored_text_on_front_layer": "error",
                                "nonmirrored_text_on_back_layer": "error"}},
            "layer_presets": [], "viewports": []},
        "net_settings": net_settings(),
    }


def apply_board_settings(path):
    """Merge board_settings() into an existing project file, keeping whatever
    else KiCad has stored there."""
    pro = json.loads(path.read_text(encoding="utf-8"))
    for key, val in board_settings().items():
        if key == "board":
            ds = pro.setdefault("board", {}).setdefault("design_settings", {})
            for k2, v2 in val["design_settings"].items():
                if isinstance(v2, dict):
                    ds.setdefault(k2, {}).update(v2)
                else:
                    ds[k2] = v2
        else:
            pro[key] = val
    path.write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")


def write_project():
    pro = {
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 3},
        "pcbnew": {"page_layout_descr_file": ""},
        "schematic": {"drawing": {"default_line_thickness": 6.0, "default_text_size": 50.0,
                                  "label_size_ratio": 0.375, "pin_symbol_size": 25.0, "text_offset_ratio": 0.15},
                      "legacy_lib_dir": "", "legacy_lib_list": [], "meta": {"version": 1}},
        "sheets": [],
        "text_variables": {},
    }
    pro.update(board_settings())
    (HW / f"{PROJECT}.kicad_pro").write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")
    (HW / "sym-lib-table").write_text(
        '(sym_lib_table\n  (version 7)\n'
        f'  (lib (name "handoff")(type "KiCad")(uri "${{KIPRJMOD}}/{PROJECT}.kicad_sym")(options "")(descr "Handoff project symbols"))\n)\n',
        encoding="utf-8")
    (HW / "fp-lib-table").write_text(
        '(fp_lib_table\n  (version 7)\n'
        '  (lib (name "handoff")(type "KiCad")(uri "${KIPRJMOD}/handoff.pretty")(options "")(descr "Handoff project footprints"))\n)\n',
        encoding="utf-8")


def write_symbol_lib(s: Schematic):
    lib = ["kicad_symbol_lib", ["version", "20250610"], ["generator", Q("gen_schematic.py")],
           ["generator_version", Q("10.0")]]
    for key, ls in s.libsyms.items():
        if key.startswith("handoff:"):
            node = copy.deepcopy(ls.node)
            node[1] = Q(key.split(":", 1)[1])
            lib.append(node)
    (HW / f"{PROJECT}.kicad_sym").write_text(dump(lib) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------
def run_cli(*args) -> subprocess.CompletedProcess:
    return subprocess.run([str(KICAD_CLI), *args], capture_output=True, text=True)


def check(sch_path: Path) -> int:
    fails = 0
    out = HW / "build"
    out.mkdir(exist_ok=True)
    # ERC
    erc = out / "erc.json"
    r = run_cli("sch", "erc", "--format", "json", "--severity-all", "-o", str(erc), str(sch_path))
    rep = json.loads(erc.read_text(encoding="utf-8"))
    viol = [v for sh in rep.get("sheets", []) for v in sh.get("violations", [])]
    for v in viol:
        fails += v["severity"] == "error"
        where = ", ".join(f'{i.get("description","")}' for i in v.get("items", []))
        print(f'ERC {v["severity"]:7s} {v["type"]}: {v["description"]}  [{where}]')
    print(f"ERC: {len(viol)} violation(s), {sum(v['severity']=='error' for v in viol)} error(s)")
    # netlist
    net = out / "handoff.net"
    r = run_cli("sch", "export", "netlist", "--format", "kicadsexpr", "-o", str(net), str(sch_path))
    if r.returncode:
        print(r.stdout, r.stderr)
        return fails + 1
    tree = parse(net.read_text(encoding="utf-8"))
    nets = {}
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1]
        nets[name] = {(find(x, "ref")[1], find(x, "pin")[1]) for x in find_all(n, "node")}
    got = {frozenset(v) for v in nets.values() if len(v) > 1}
    want = {frozenset(v) for v in EXPECTED_NETS.values()}
    for w in want - got:
        # find best-matching real net to explain
        best = max(nets.items(), key=lambda kv: len(kv[1] & w))
        print(f"NET MISMATCH want {sorted(w)}\n             got  {best[0]} = {sorted(best[1])}")
        fails += 1
    for g in got - want:
        print(f"UNEXPECTED NET {sorted(g)}")
        fails += 1
    single = [(k, v) for k, v in nets.items() if len(v) == 1 and not k.startswith("unconnected-")]
    for k, v in single:
        print(f"SINGLE-NODE NET {k}: {v}")
    print(f"netlist: {len(nets)} nets, {len(want - got)} missing, {len(got - want)} unexpected")
    # BOM, diffed against the README parts table (refs only: the table is the parts authority)
    bom = out / "bom.csv"
    run_cli("sch", "export", "bom", "--fields", "Reference,Value,Footprint,Description,${DNP}",
            "--group-by", "Value,Footprint", "--ref-range-delimiter", "", "-o", str(bom), str(sch_path))
    bom_refs = set()
    for line in bom.read_text(encoding="utf-8").splitlines()[1:]:
        refs = line.split('","')[0].strip('"')
        bom_refs |= {r.strip() for r in refs.split(",")}
    # Only the parts table counts: the jumper and test-pad tables further down
    # also start their rows with a reference, and those are not purchasable parts.
    readme_refs, in_parts = set(), False
    for line in (HW / "README.md").read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            in_parts = line.startswith("## Parts")
            continue
        if not in_parts:
            continue
        m = re.match(r"\|\s*((?:[A-Z]+\d+(?:\s*,\s*)?)+)\s*\|", line)
        if m:
            readme_refs |= {r.strip() for r in m.group(1).split(",")}
    copper_only = {r for r in bom_refs if re.match(r"(TP|JP|H|E)\d+$", r)}
    for r in sorted(bom_refs - copper_only - readme_refs):
        print(f"BOM ref {r} is not in the README parts table"); fails += 1
    for r in sorted(readme_refs - bom_refs):
        print(f"README parts table lists {r}, which is not in the schematic BOM"); fails += 1
    print(f"BOM: {len(bom_refs)} refs, {len(copper_only)} copper-only, README table diff: "
          f"{len(bom_refs - copper_only - readme_refs) + len(readme_refs - bom_refs)} difference(s)")
    # PDF for eyeballing
    run_cli("sch", "export", "pdf", "-o", str(out / "handoff.pdf"), str(sch_path))
    return fails


def main():
    s = build()
    write_project()
    write_symbol_lib(s)
    sch_path = HW / f"{PROJECT}.kicad_sch"
    sch_path.write_text(s.render(), encoding="utf-8")
    print(f"wrote {sch_path}")
    if "--no-check" in sys.argv:
        return 0
    return check(sch_path)


if __name__ == "__main__":
    sys.exit(main())
