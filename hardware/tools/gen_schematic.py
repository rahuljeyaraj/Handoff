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


def uid() -> str:
    return str(uuid.uuid4())


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
FP_R = "Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder"
FP_C = "Capacitor_SMD:C_1206_3216Metric_Pad1.33x1.80mm_HandSolder"
FP_CP = "Capacitor_THT:CP_Radial_D5.0mm_P2.50mm"
FP_D41 = "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
FP_PICO = "Module:RaspberryPi_Pico_Common_THT"
FP_MSOP8 = "Package_SO:MSOP-8_3x3mm_P0.65mm"
FP_XH2 = "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical"
FP_XH4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
FP_SOCK10 = "Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"
FP_JP = "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm"
FP_JP3 = "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm"
FP_BTN = "Button_Switch_THT:SW_PUSH_6mm"
FP_HOLE = "MountingHole:MountingHole_3.2mm_M3"
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
    CP = s.use("Device", "C_Polarized")
    DSCH = s.use("Device", "D_Schottky")
    LED = s.use("Device", "LED_RGBK")
    BAT = s.use("Device", "Battery_Cell")
    SW = s.use("Switch", "SW_SPDT")
    TP = s.use("Connector", "TestPoint")
    JP = s.use("Jumper", "SolderJumper_2_Open")
    JP3S = s.use("Jumper", "SolderJumper_3_Open")
    BTN = s.use("Switch", "SW_Push")
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
    tp10 = s.place(TP, "TP10", "BAT+", (tap_p[0] + 8, swb[1] - 4), fp=FP_TP, desc="Test pad, cell voltage (before D1)", ref_at=(1, -2), val_at=(1, 1))
    s.wire((tap_p[0] + 8, swb[1]), s.gpin("TP10", "1"))
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
    # D1 cathode -> TP11 -> JP5 -> VSYS. JP5 open: the whole board's current through an ammeter TP11 -> TP8.
    n1 = (dk[0] + 2, dk[1])
    tp11 = s.place(TP, "TP11", "D1_K", (n1[0], n1[1] - 4), fp=FP_TP, desc="Test pad, cell side of JP5 (ammeter + here)", ref_at=(1, -2), val_at=(1, 1))
    jp5 = s.place(JP, "JP5", "VSYS", (n1[0] + 6, n1[1]), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to feed VSYS from the cell; open, an ammeter from TP11 to TP8 reads the board current",
                  ref_at=(-3, 3), val_at=(1, 3))
    a5, b5 = s.gpin("JP5", "1"), s.gpin("JP5", "2")
    s.wire(dk, n1, a5)
    s.wire(n1, s.gpin("TP11", "1"))
    vsys_p = (b5[0] + 2, b5[1])
    s.wire(b5, vsys_p, (vsys_p[0], vsys_p[1] - 2))
    s.power("VSYS", (vsys_p[0], vsys_p[1] - 2))
    s.place(FLAG, "#FLG01", "PWR_FLAG", (vsys_p[0] + 2, vsys_p[1] + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(vsys_p, (vsys_p[0] + 2, vsys_p[1]), (vsys_p[0] + 2, vsys_p[1] + 4))
    tp8 = s.place(TP, "TP8", "VSYS", (vsys_p[0] + 8, vsys_p[1] - 4), fp=FP_TP, desc="Test pad, VSYS (after D1 and JP5)", ref_at=(1, -2), val_at=(1, 1))
    s.wire((vsys_p[0] + 2, vsys_p[1]), (vsys_p[0] + 8, vsys_p[1]), s.gpin("TP8", "1"))

    s.text("VBUS (USB) and VSYS are OR'd inside the Pico; D1 stops VSYS back-feeding the cell and makes a reversed J1 harmless.", (12, 50), size=1.27)
    s.text("~0.35 V drop: VSYS 2.6-3.8 V, Pico needs 1.8-5.5 V. J5 is the charger's plug, wired to the cell, NOT a power input.", (12, 52), size=1.27)
    s.text("JP5 ships OPEN: bridge it to run from the cell (USB works regardless); open, TP11 -> TP8 is the ammeter position.", (12, 54), size=1.27)

    # =====================================================================
    # 2. VREF bias  (mid top)
    # =====================================================================
    s.text("VREF = 3V3/2 = 1.65 V (design §6.2)", (110, 14), size=2.0, bold=True)
    r10 = s.place(R, "R10", "100k", (120, 26), fp=FP_R, desc="VREF divider, top")
    r11 = s.place(R, "R11", "100k", (120, 40), fp=FP_R, desc="VREF divider, bottom")
    c4 = s.place(CP, "C4", "10uF 63V", (132, 40), fp=FP_CP, desc="VREF hold-up, electrolytic, + to VREF")
    top = s.gpin("R10", "1"); mid1 = s.gpin("R10", "2"); mid2 = s.gpin("R11", "1"); bot = s.gpin("R11", "2")
    c4p, c4n = s.gpin("C4", "1"), s.gpin("C4", "2")
    # the divider hangs off the AFE side of JP4, so a bench supply at TP6 powers the whole analogue side
    s.wire(top, (top[0], top[1] - 3)); s.label("AFE_3V3", (top[0], top[1] - 3), 90, "left bottom")
    s.wire(mid1, mid2)
    s.wire(bot, (bot[0], bot[1] + 2)); s.power("GND", (bot[0], bot[1] + 2))
    vref_y = (mid1[1] + mid2[1]) // 2 if (mid1[1] + mid2[1]) % 2 == 0 else mid1[1]
    s.wire((mid1[0], vref_y), (c4p[0], vref_y), c4p)
    s.wire(c4n, (c4n[0], bot[1] + 2), (bot[0], bot[1] + 2))
    # JP6: divider -> VREF. Open, an external bias goes in at TP5.
    jp6 = s.place(JP, "JP6", "VREF", (c4p[0] + 7, vref_y), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to connect the R10/R11/C4 divider to VREF; open, inject a bias at TP5",
                  ref_at=(-3, 3), val_at=(1, 3))
    a6, b6 = s.gpin("JP6", "1"), s.gpin("JP6", "2")
    s.wire((c4p[0], vref_y), a6)
    vref_out = (b6[0] + 4, vref_y)
    s.wire(b6, vref_out, (vref_out[0], vref_out[1] - 2))
    s.power("VREF", (vref_out[0], vref_out[1] - 2))
    flag_x = vref_out[0] + 6
    s.place(FLAG, "#FLG02", "PWR_FLAG", (flag_x, vref_out[1] + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(vref_out, (flag_x, vref_out[1]), (flag_x, vref_out[1] + 4))
    tp5 = s.place(TP, "TP5", "VREF", (flag_x + 6, vref_out[1] - 2), fp=FP_TP, desc="Test pad", ref_at=(1, -2), val_at=(1, 1))
    s.wire((flag_x, vref_out[1]), (flag_x + 6, vref_out[1]), s.gpin("TP5", "1"))

    # =====================================================================
    # 3. Op-amp supply + decoupling (right top)
    # =====================================================================
    s.text("U2 supply — AFE_3V3 behind JP4 (design §6.4 decoupling + C5 bulk)", (190, 14), size=2.0, bold=True)
    u2c = s.place(OPA, "U2", "MCP6292-E/MS", (204, 36), unit=3, fp=FP_MSOP8, ref_at=(-4, -1, "right"), val_at=(-4, 1, "right"))
    c3 = s.place(C, "C3", "100nF", (220, 36), fp=FP_C, desc="U2 decoupling, across pins 8 and 4")
    c5 = s.place(CP, "C5", "10uF 63V", (230, 36), fp=FP_CP, desc="U2 bulk decoupling against the Pico SMPS, + to AFE_3V3")
    vp, vn = s.gpin("U2", "8", 3), s.gpin("U2", "4", 3)
    c3a, c3b = s.gpin("C3", "1"), s.gpin("C3", "2")
    c5a, c5b = s.gpin("C5", "1"), s.gpin("C5", "2")
    yt, yb = vp[1] - 2, vn[1] + 2
    rail_end = (246, yt)
    s.wire(vp, (vp[0], yt), (vp[0] + 4, yt), (240, yt), rail_end)
    s.label("AFE_3V3", (vp[0] + 4, yt), 0, "left bottom")
    s.wire(c3a, (c3a[0], yt)); s.wire(c5a, (c5a[0], yt))
    s.wire(vn, (vn[0], yb), (240, yb))
    s.wire(c3b, (c3b[0], yb)); s.wire(c5b, (c5b[0], yb))
    s.wire((vn[0], yb), (vn[0], yb + 2)); s.power("GND", (vn[0], yb + 2))
    # JP4: 3V3 -> U2 VDD. Open: U2 current through an ammeter, or a bench supply at TP6 runs the AFE alone.
    s.wire((vp[0], yt), (vp[0], yt - 4))
    jp4 = s.place(JP, "JP4", "U2 VDD", (vp[0] - 8, yt - 4), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to power the AFE from the Pico's 3V3; open, meter U2's current or feed AFE_3V3 at TP6",
                  ref_at=(0, -3, "center"), val_at=(0, 3, "center"))
    a4, b4 = s.gpin("JP4", "1"), s.gpin("JP4", "2")
    s.wire(b4, (vp[0], yt - 4))
    s.wire(a4, (a4[0], a4[1] - 2)); s.power("+3V3", (a4[0], a4[1] - 2))
    s.place(FLAG, "#FLG03", "PWR_FLAG", (rail_end[0], yt + 4), rot=180, ref_at=(0, 4, "center"), val_at=(0, 3, "center"))
    s.wire(rail_end, (rail_end[0], yt + 4))
    tp6 = s.place(TP, "TP6", "AFE_3V3", (240, yt - 4), fp=FP_TP, desc="Test pad, U2 supply (AFE side of JP4)", ref_at=(1, -2), val_at=(1, 1))
    s.wire((240, yt), s.gpin("TP6", "1"))
    tp7 = s.place(TP, "TP7", "GND", (240, yb + 4), rot=180, fp=FP_TP, desc="Test pad", ref_at=(0, 5, "center"), val_at=(0, 7, "center"))
    s.wire((240, yb), s.gpin("TP7", "1"))

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
    used_left = {"1": "GP0", "2": "GP1", "4": "GP2_TX", "6": "GP4", "7": "GP5", "19": "ROLE", "20": "BTN", "30": "RUN"}
    used_right = {"21": "LED_R", "22": "LED_G", "24": "LED_B", "26": "GP20", "27": "GP21",
                  "31": "ADC0", "32": "GP27_ADC1"}
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
    # -- TX: GP2 -> JP2 -> R1 -> PAD node
    jp2 = s.place(JP, "JP2", "TX", (118, Y - 12), rot=0, fp=FP_JP, desc="Open as shipped: bridge with solder to connect GP2 to the pad (DC-coupled); or fit C6 instead",
                  ref_at=(-3, 3), val_at=(1, 3))
    r1 = s.place(R, "R1", "1M", (134, Y - 12), rot=90, fp=FP_R, desc="TX safety resistor, design §13")
    a, b = s.gpin("JP2", "1"), s.gpin("JP2", "2")
    s.wire((a[0] - 8, a[1]), a); s.label("GP2_TX", (a[0] - 8, a[1]), 0, "right bottom")
    tp9 = s.place(TP, "TP9", "GP2 TX", (a[0] - 6, a[1] - 6), fp=FP_TP, desc="Test pad", ref_at=(1, -2), val_at=(1, 1))
    s.wire(s.gpin("TP9", "1"), (a[0] - 6, a[1]))
    s.wire(b, s.gpin("R1", "1"))
    # C6, DNP, across JP2: AC-couples the TX drive so GP2's leakage (RP2350-E9) cannot reach the pad node.
    # Fit only if the M3/M7 leakage measurement says so; then JP2 stays open.
    c6 = s.place(C, "C6", "330pF C0G (DNP)", (118, Y - 20), rot=90, fp=FP_C, dnp=True,
                 desc="DNP. Optional TX DC block in parallel with JP2; fit instead of bridging JP2 if GP2 leakage biases the pad",
                 ref_at=(12, -1, "center"), val_at=(12, 1, "center"))
    c6a, c6b = s.gpin("C6", "1"), s.gpin("C6", "2")
    s.wire(a, (a[0], c6a[1]), c6a)
    s.wire(b, (b[0], c6b[1]), c6b)
    pad_node = (150, Y)
    r1b = s.gpin("R1", "2")
    s.wire(r1b, (pad_node[0], r1b[1]), (pad_node[0], Y - 6))   # TP1 sits on this drop
    # -- PAD connector J2 and test pad
    j2 = s.place(C2, "J2", "PAD JST-XH 2p", (140, Y), rot=180, fp=FP_XH2,
                 desc="Pin 1 = skin PAD, pin 2 = ground-plane electrode (optional)", ref_at=(-6, -4, "center"), val_at=(-6, 6, "center"))
    j2p1, j2p2 = s.gpin("J2", "1"), s.gpin("J2", "2")   # pin 1 (PAD) on row Y, pin 2 (GND plane) above it
    assert j2p1[1] == Y and j2p2[1] == Y - 2
    s.wire(j2p1, pad_node)
    s.wire(j2p2, (j2p2[0] + 2, j2p2[1]), (j2p2[0] + 2, j2p2[1] - 2))
    s.power("GND", (j2p2[0] + 2, j2p2[1] - 2), rot=180)
    s.label("PAD", (j2p1[0] + 1, j2p1[1]), 0, "left bottom")
    s.text("ground-plane electrode: optional, see README", (j2p1[0] - 22, Y + 8), size=1.0)
    tp1 = s.place(TP, "TP1", "PAD", (150, Y - 6), fp=FP_TP, desc="Test pad", ref_at=(1, -3), val_at=(1, -1))
    s.wire(s.gpin("TP1", "1"), pad_node)
    # -- RX stage 1: PAD -> R2 -> U2A+ ; R3 to VREF ; R4/R5 feedback
    # JP7: PAD -> receiver. Open, the receiver is tested from a signal injected at TP12 with pad and TX out of the picture.
    jp7 = s.place(JP, "JP7", "RX", (156, Y), rot=0, fp=FP_JP,
                  desc="Open as shipped: bridge to connect the pad to the receiver; open, drive the receiver from TP12",
                  ref_at=(-3, 3), val_at=(1, 3))
    s.wire(pad_node, s.gpin("JP7", "1"))
    r2 = s.place(R, "R2", "1M", (170, Y), rot=90, fp=FP_R, desc="RX safety resistor, design §13")
    s.wire(s.gpin("JP7", "2"), (164, Y), s.gpin("R2", "1"))
    tp12 = s.place(TP, "TP12", "RX_IN", (164, Y - 6), fp=FP_TP, desc="Test pad, receiver input (R2 side of JP7)", ref_at=(1, -3), val_at=(1, -1))
    s.wire((164, Y), s.gpin("TP12", "1"))
    u2a = s.place(OPA, "U2", "MCP6292-E/MS", (196, Y + 2), unit=1, fp=FP_MSOP8, ref_at=(-1, 0, "center"), hide_value=True)
    ina_p, ina_n, outa = s.gpin("U2", "3", 1), s.gpin("U2", "2", 1), s.gpin("U2", "1", 1)
    hiz = (ina_p[0] - 8, ina_p[1])
    s.wire(s.gpin("R2", "2"), hiz, ina_p)
    r3 = s.place(R, "R3", "10M", (hiz[0], Y + 10), rot=0, fp=FP_R, desc="Biases the 10 MOhm node to VREF", ref_at=(-2, -1, "right"), val_at=(-2, 1, "right"))
    s.wire(hiz, s.gpin("R3", "1"))
    s.text("10 MOhm node: keep tiny, guard it (README)", (hiz[0] - 10, Y + 24), size=1.0)
    # feedback: R4 above the op-amp from OUT A back to -IN A; R5 from -IN A down to VREF
    fb1 = (ina_n[0] - 2, ina_n[1])
    r5 = s.place(R, "R5", "10k", (fb1[0], fb1[1] + 6), rot=0, fp=FP_R, desc="Stage 1 gain set: 1 + R4/R5 = 11", ref_at=(2, -1), val_at=(2, 1))
    r4 = s.place(R, "R4", "100k", (ina_n[0] + 8, ina_n[1] - 12), rot=90, fp=FP_R, desc="Stage 1 feedback")
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
    c1 = s.place(C, "C1", "100nF", (outa[0] + 14, outa[1]), rot=90, fp=FP_C, desc="Interstage DC block")
    s.wire((outa[0] + 2, outa[1]), (outa[0] + 7, outa[1]), s.gpin("C1", "1"))
    s.label("OUT1", (outa[0] + 7, outa[1]), 0, "left bottom")
    tp2 = s.place(TP, "TP2", "OUT1", (outa[0] + 5, outa[1] - 5), fp=FP_TP, desc="Test pad, stage 1 output", ref_at=(1, -3), val_at=(1, -1))
    s.wire((outa[0] + 5, outa[1]), s.gpin("TP2", "1"))
    c1b = s.gpin("C1", "2")
    in2 = (c1b[0] + 4, c1b[1])
    s.wire(c1b, in2)
    r6 = s.place(R, "R6", "100k", (in2[0], in2[1] + 10), rot=0, fp=FP_R, desc="Re-bias stage 2 +IN to VREF", ref_at=(-2, -1, "right"), val_at=(-2, 1, "right"))
    s.wire(in2, s.gpin("R6", "1"))
    # -- stage 2
    u2b = s.place(OPA, "U2", "MCP6292-E/MS", (in2[0] + 14, in2[1] + 2), unit=2, fp=FP_MSOP8, ref_at=(-1, 0, "center"), hide_value=True)
    inb_p, inb_n, outb = s.gpin("U2", "5", 2), s.gpin("U2", "6", 2), s.gpin("U2", "7", 2)
    s.wire(in2, inb_p)
    fb2 = (inb_n[0] - 2, inb_n[1])
    r8 = s.place(R, "R8", "10k", (fb2[0], fb2[1] + 6), rot=0, fp=FP_R, desc="Stage 2 gain set: 1 + R7/R8 = 11", ref_at=(2, -1), val_at=(2, 1))
    r7 = s.place(R, "R7", "100k", (inb_n[0] + 8, inb_n[1] - 12), rot=90, fp=FP_R, desc="Stage 2 feedback")
    s.wire(inb_n, fb2, s.gpin("R8", "1"))
    r6b, r8b = s.gpin("R6", "2"), s.gpin("R8", "2")
    assert r6b[1] == r8b[1]
    mid = ((r6b[0] + r8b[0]) // 2, r6b[1])
    s.wire(r6b, r8b); s.wire(mid, (mid[0], mid[1] + 2)); s.power("VREF", (mid[0], mid[1] + 2), rot=180)
    r7a, r7b = s.gpin("R7", "1"), s.gpin("R7", "2")
    s.wire(fb2, (fb2[0], r7a[1]), r7a)
    s.wire(r7b, (outb[0] + 2, r7b[1]), (outb[0] + 2, outb[1]))
    s.wire(outb, (outb[0] + 2, outb[1]))
    tp3 = s.place(TP, "TP3", "AFE_OUT", (outb[0] + 5, outb[1] - 5), fp=FP_TP, desc="Test pad, amplifier output", ref_at=(1, -3), val_at=(1, -1))
    s.wire((outb[0] + 5, outb[1]), s.gpin("TP3", "1"))
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
    tp4 = s.place(TP, "TP4", "ADC0", (adc_node[0], adc_node[1] - 8), fp=FP_TP, desc="Test pad, ADC input", ref_at=(1, -3), val_at=(1, -1))
    s.wire(adc_node, s.gpin("TP4", "1"))
    # a scope ground next to the analogue test pads (TP7 is up by U2's supply)
    tp13 = s.place(TP, "TP13", "GND", (adc_node[0] + 14, adc_node[1] + 6), fp=FP_TP, desc="Test pad, ground for the analogue probes", ref_at=(1, -3), val_at=(1, -1))
    s.wire(s.gpin("TP13", "1"), (adc_node[0] + 14, adc_node[1] + 8)); s.power("GND", (adc_node[0] + 14, adc_node[1] + 8))
    s.text("Gain 11 x 11 = 121. Firmware holds GP2 high-Z with its input buffer OFF while receiving (RP2350-E9), so R1 does not load or bias the pad.", (110, 136), size=1.27)
    s.text("Every JP ships OPEN: bridge with solder to bring the board up one stage at a time (README order); wick off to isolate. No test pad on the 10 MOhm node on purpose.", (110, 138), size=1.27)
    s.text("JP3: centre -> R9. Blob to pin 3 = stage 2 (x121); move it to pin 1 = stage 1 only (x11) if stage 2 clips. C6 is DNP: fit it instead of bridging JP2 to AC-couple TX.", (110, 140), size=1.27)

    # =====================================================================
    # 6. STATUS LED  (bottom left)
    # =====================================================================
    s.text("STATUS LED — RGB common cathode on GP16/17/18, direct-solder or JST-XH 4p", (12, 150), size=2.0, bold=True)
    LY = 168
    # J3 rot 0: pins on the left, rows LY (R), LY+2 (K), LY+4 (G), LY+6 (B); each series
    # resistor sits on its own pin row, staggered in x so nothing crosses.
    j3 = s.place(C4, "J3", "LED JST-XH 4p / 5mm RGB", (64, LY), rot=0, fp=FP_XH4,
                 desc="Pin order R, K, G, B = 5 mm common-cathode RGB LED leads", ref_at=(2, -2), val_at=(0, 10))
    for ref, net, colour, pinnum, x, drop, col in (("R12", "LED_R", "R", "1", 34, 0, None),
                                                   ("R13", "LED_G", "G", "3", 42, 6, 56),
                                                   ("R14", "LED_B", "B", "4", 50, 10, 58)):
        jp = s.gpin("J3", pinnum)
        r = s.place(R, ref, "330", (x, jp[1] + drop), rot=90, fp=FP_R, desc=f"LED {colour} series",
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
    s.text("J4 EXPANSION — spare Pico pins for the next idea (1x10 female)", (150, 150), size=2.0, bold=True)
    j4 = s.place(C10, "J4", "EXP 1x10", (170, 178), rot=0, fp=FP_SOCK10, desc="3V3 RUN GP0 GP1 GP4 GP5 GP20 GP21 GP27 GND",
                 ref_at=(-2, -14), val_at=(-2, 14))
    exp = ["+3V3", "RUN", "GP0", "GP1", "GP4", "GP5", "GP20", "GP21", "GP27_ADC1", "GND"]
    for i, net in enumerate(exp, start=1):
        p = s.gpin("J4", str(i))
        if net == "+3V3":
            s.wire(p, (p[0] - 6, p[1]), (p[0] - 6, p[1] - 3)); s.power("+3V3", (p[0] - 6, p[1] - 3))
        elif net == "GND":
            s.wire(p, (p[0] - 6, p[1]), (p[0] - 6, p[1] + 3)); s.power("GND", (p[0] - 6, p[1] + 3))
        else:
            s.wire(p, (p[0] - 6, p[1])); s.label(net, (p[0] - 6, p[1]), 0, "right bottom")

    # =====================================================================
    # 8. BUTTON, ROLE STRAP, MOUNTING  (bottom right)
    # =====================================================================
    s.text("BUTTON (GP15), ROLE strap (GP14), MOUNTING", (238, 150), size=2.0, bold=True)
    BX, BY = 250, 164
    r15 = s.place(R, "R15", "10k", (BX, BY), rot=0, fp=FP_R, desc="BTN pull-up", ref_at=(2, -1), val_at=(2, 1))
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
    s.text("H1-H4: 3.2 mm unplated, one per corner, not tied to GND", (274, BY + 8), size=1.0)

    return s


# --------------------------------------------------------------------------
# The oracle: what the netlist must say. (ref, pin) sets per net. Unit-3 pins
# of U2 are its supply.
# --------------------------------------------------------------------------
EXPECTED_NETS = {
    # every JPx is OPEN as shipped, so each one splits what the design doc calls one net into two
    "BAT+": {("J1", "2"), ("J5", "2"), ("SW1", "2"), ("TP10", "1")},
    "GND": {("J1", "1"), ("J5", "1"), ("U1", "3"), ("U1", "8"), ("U1", "13"), ("U1", "18"), ("U1", "23"),
            ("U1", "28"), ("U1", "33"), ("U1", "38"), ("U2", "4"), ("C3", "2"), ("C5", "2"), ("R11", "2"), ("C4", "2"),
            ("J2", "2"), ("C2", "2"), ("J3", "2"), ("J4", "10"), ("TP7", "1"), ("TP13", "1"), ("SW2", "2"), ("JP8", "2")},
    "SW_OUT": {("SW1", "1"), ("D1", "2")},
    "D1_K": {("D1", "1"), ("JP5", "1"), ("TP11", "1")},
    "VSYS": {("JP5", "2"), ("U1", "39"), ("TP8", "1")},
    "+3V3": {("U1", "36"), ("JP4", "1"), ("J4", "1"), ("R15", "1")},
    "AFE_3V3": {("JP4", "2"), ("U2", "8"), ("C3", "1"), ("C5", "1"), ("R10", "1"), ("TP6", "1")},
    "VREF_DIV": {("R10", "2"), ("R11", "1"), ("C4", "1"), ("JP6", "1")},
    "VREF": {("JP6", "2"), ("R3", "2"), ("R5", "2"), ("R6", "2"), ("R8", "2"), ("TP5", "1")},
    "GP2_TX": {("U1", "4"), ("JP2", "1"), ("C6", "1"), ("TP9", "1")},
    "JP2_R1": {("JP2", "2"), ("C6", "2"), ("R1", "1")},
    "PAD": {("R1", "2"), ("JP7", "1"), ("J2", "1"), ("TP1", "1")},
    "RX_IN": {("JP7", "2"), ("R2", "1"), ("TP12", "1")},
    "HIZ": {("R2", "2"), ("R3", "1"), ("U2", "3")},
    "FB1": {("U2", "2"), ("R5", "1"), ("R4", "1")},
    "OUT1": {("U2", "1"), ("R4", "2"), ("C1", "1"), ("TP2", "1"), ("JP3", "1")},
    "IN2": {("C1", "2"), ("R6", "1"), ("U2", "5")},
    "FB2": {("U2", "6"), ("R8", "1"), ("R7", "1")},
    "OUT2": {("U2", "7"), ("R7", "2"), ("JP3", "3"), ("TP3", "1")},
    "JP3_R9": {("JP3", "2"), ("R9", "1")},
    "R9_JP1": {("R9", "2"), ("JP1", "1")},
    "BTN": {("U1", "20"), ("R15", "2"), ("SW2", "1")},
    "ROLE": {("U1", "19"), ("JP8", "1")},
    "ADC0": {("JP1", "2"), ("C2", "1"), ("TP4", "1"), ("U1", "31")},
    "LED_R": {("U1", "21"), ("R12", "1")},
    "LED_G": {("U1", "22"), ("R13", "1")},
    "LED_B": {("U1", "24"), ("R14", "1")},
    "J3_R": {("R12", "2"), ("J3", "1")},
    "J3_G": {("R13", "2"), ("J3", "3")},
    "J3_B": {("R14", "2"), ("J3", "4")},
    "RUN": {("U1", "30"), ("J4", "2")},
    "GP0": {("U1", "1"), ("J4", "3")},
    "GP1": {("U1", "2"), ("J4", "4")},
    "GP4": {("U1", "6"), ("J4", "5")},
    "GP5": {("U1", "7"), ("J4", "6")},
    "GP20": {("U1", "26"), ("J4", "7")},
    "GP21": {("U1", "27"), ("J4", "8")},
    "GP27_ADC1": {("U1", "32"), ("J4", "9")},
}


def write_project():
    pro = {
        "board": {"design_settings": {"defaults": {}, "rules": {}}, "layer_presets": [], "viewports": []},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 3},
        "net_settings": {"classes": [{"name": "Default", "clearance": 0.2, "track_width": 0.25,
                                      "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6, "bus_width": 12,
                                      "line_style": 0, "priority": 2147483647,
                                      "schematic_color": "rgba(0, 0, 0, 0.000)", "pcb_color": "rgba(0, 0, 0, 0.000)"}],
                         "meta": {"version": 4}},
        "pcbnew": {"page_layout_descr_file": ""},
        "schematic": {"drawing": {"default_line_thickness": 6.0, "default_text_size": 50.0,
                                  "label_size_ratio": 0.375, "pin_symbol_size": 25.0, "text_offset_ratio": 0.15},
                      "legacy_lib_dir": "", "legacy_lib_list": [], "meta": {"version": 1}},
        "sheets": [],
        "text_variables": {},
    }
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
    readme_refs = set()
    for line in (HW / "README.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*((?:[A-Z]+\d+(?:\s*,\s*)?)+)\s*\|", line)
        if m:
            readme_refs |= {r.strip() for r in m.group(1).split(",")}
    copper_only = {r for r in bom_refs if re.match(r"(TP|JP|H)\d+$", r)}
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
