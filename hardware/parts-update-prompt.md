# Prompt for the next session — the all-0603 order and the SOD-123FL Schottky

> **Applied, 12 Sep 2026.** Everything below was carried out in the session
> logged in `README.md` under *This session: every passive 0603, and the
> SOD-123FL Schottky*. Kept as the record of what was asked, the way
> `schematic-prompt.md` and `review-prompt.md` are. Two things the brief got
> wrong, for anyone re-reading it: KiCad's 0603 capacitor hand-solder
> footprint is `C_0603_1608Metric_Pad1.08x0.95mm_HandSolder` (pads at ±0.8625),
> and the AFE pocket's exit that closed was between R3's pads, not under R5.
> The six parts were bought from Robu (R136892, R172322, R144171, R134792,
> R134878, R241663), so the supplier column stands.

Copy everything below the line into the new chat.

---

You are a senior PCB designer. A new parts order has arrived that moves every
remaining 1206 and 0805 passive on this board to 0603 and replaces the two
Schottky diodes with a smaller part. The board is fully routed and DRC-clean
(commit `512e2fb`); your job is to apply the new parts, keep it that way, and
leave the README and BOM telling the truth.

Read first: `hardware/README.md` (all of it — the *Parts* table is the parts
authority the generator diffs against, and the *"This session"* sections are the
history of every trap this board has already sprung), `hardware/bom.csv`, and
`hardware/tools/gen_schematic.py`, `gen_pcb.py`, `gen_sw_footprint.py`.

**The generators are the schematic and the board.** Edit
`hardware/tools/gen_schematic.py` and `gen_pcb.py`, never the `.kicad_sch` or
`.kicad_pcb`. After every change re-run both; the schematic run must end:

```
ERC: 0 violation(s), 0 error(s)
netlist: NN nets, 0 missing, 0 unexpected
BOM: ... README table diff: 0 difference(s)
```

and the board run must end DRC-clean with the pour-island check and the short
check passing. `EXPECTED_NETS` is the oracle — none of these changes touch a
net, so it must not change at all. If it wants to, you have broken something.

## The parts — exactly these, nothing else changes

| Refs | Old | New part | Package | Footprint |
|---|---|---|---|---|
| C1, C2, C6 | 330 pF C0G 1206 (KEMET C1206C331J5GACTU) | **CL10C331JC8NNNC** Samsung, 330 pF **C0G/NP0** ±5 % 100 V | 0603 | `Capacitor_SMD:C_0603_1608Metric_Pad0.98x0.95mm_HandSolder` |
| C3 | 100 nF X7R 1206 | **CL10B104KB8NNNC** Samsung, 100 nF X7R ±10 % 50 V | 0603 | same |
| C4, C5 | 10 µF "marked 100 V" 0805 | **GRM188R61E106MA73D** Murata, 10 µF X5R ±20 % **25 V** | 0603 | same |
| R1, R2, R3 | 1 MΩ 1206 | **0603WAF1004T5E** Uniohm, 1 MΩ ±1 % 100 mW 75 V | 0603 | `Resistor_SMD:R_0603_1608Metric_Pad0.98x0.95mm_HandSolder` |
| R9 | 1.5 kΩ 1 % 1206 (Yageo RC1206FR-071K5L) | **0603WAF1501T5E** Uniohm, 1.5 kΩ ±1 % 100 mW 75 V | 0603 | same |
| D1, D3 | SS220F Slkor, on `Diode_SMD:D_SMB` | **PMEG3020ER-TP** Tech Public, 40 V 2 A Schottky | **SOD-123FL** | **`handoff:D_SOD-123FL`** — does not exist yet, see below |

C6 stays **DNP** and stays "the C2 part": same SKU, `dnp=True`, as now.

Everything else — R4–R8, R10–R17, Q1, U1, U2, SW1, SW2, J1–J6, BT1, D2, M1,
JP1–JP8, TP1–TP8, H1–H4 — is untouched. After this there is **no 1206 and no
0805 on the board**; every passive is 0603.

The parts table's supplier column is currently `Robu SKU`. These six were not
bought from Robu — ask the user where, and record it; do not guess.

## Facts about PMEG3020ER-TP you must not get wrong

**It is not a Nexperia PMEG3020ER.** It borrows the number. Do not cross-reference
the Nexperia datasheet for anything. The Tech Public datasheet (the user has it;
ask for it if it is not in the repo) says:

- **40 V** V<sub>RRM</sub> / V<sub>R</sub> (Nexperia's is 30 V), 2 A I<sub>F(AV)</sub>,
  50 A I<sub>FSM</sub> at 8.3 ms, C<sub>J</sub> 100 pF at 4 V, R<sub>θJA</sub> 200 °C/W.
- V<sub>F</sub>: **0.41 V typ at 1 A**, 0.5 V max at 2 A, at 25 °C. There is **no
  100 mA figure** in the datasheet; Fig. 4 puts 100 mA at roughly 0.35–0.4 V.
  Use "~0.4 V at 100 mA" in the README, not Nexperia's 0.23 V.
- I<sub>R</sub>: 2 µA at 10 V, 7 µA at 30 V, 100 µA max at 40 V, 25 °C.
- Package **SOD-123FL** (Nexperia's genuine part is SOD-123W; a re-order against
  the Nexperia number will arrive in the wrong package). Body dimensions from
  the outline drawing, mm: A width 1.5–2.0; B tip-to-tip 3.4–3.9; C terminal
  width 0.7–1.2; D body length 2.5–2.9; H height 0.95–1.35; L terminal 0.35–0.9.
  Pin 1 = cathode (marking bar), pin 2 = anode.

Write the parts-table row as **"PMEG3020ER-TP (Tech Public), SOD-123FL, 40 V 2 A"**
so nobody later substitutes the Nexperia part.

## The footprint: `handoff:D_SOD-123FL`

KiCad 10 has no SOD-123FL land pattern (`D_SOD-123`, `D_SOD-123F`, `D_SOD-128`
and `Nexperia_CFP3_SOD-123W` are all wrong — the terminals sit anywhere from
1.25 to 1.95 mm from centre across the tolerance band, and none of those cover
it with a fillet). Generate one in `handoff.pretty` with a new
`tools/gen_sod123fl_footprint.py` modelled on `gen_sw_footprint.py`:

- Two rect pads **1.2 mm along the axis × 1.6 mm across, centred at ±1.65 mm**
  (inner edge 1.05, outer 2.25). Pad 1 = cathode, at −x, matching every
  KiCad `D_*` footprint so the symbol's pin map is unchanged.
- Body 2.9 × 2.0 on F.Fab; courtyard 4.9 × 2.6; cathode bar on silk at the
  pad-1 end. The existing D1/D3 silk rule holds — D3 carries no reference on
  silk but its cathode bar is printed, because a hand-assembled diode needs
  its band.
- Prove it the way SW1's was: a `fp_test.kicad_pcb` in `build/`, DRC clean,
  rendered and looked at.

Courtyard goes from `D_SMB`'s 7.3 × 4.5 mm to 4.9 × 2.6 mm. That is ~20 mm² back
at each of D1 and D3 — the pour and the flyback loop will move. Do not leave
the freed copper as a dead island.

## What the 0.64 mm shift will break — it has before

A 1206 hand-solder pad sits 1.55 mm from the body centre and an 0603 one
0.9125 mm, so R1, R2, R3, R9, C1, C2, C3 and C6 each pull both pads 0.64 mm
inward. `ROUTES` references pads symbolically (`P("R6", "1")`, `dx`/`dy`) so
the track *ends* follow — but the geometry around them does not. Last time this
exact shift on the other passives did three things, all found by DRC and the
pour check, not by eye (README, *"What a package change actually costs"*):

1. **The 0.25 mm pour channel under R5 closed** — between R5's pad 1 and FB1's
   run under U2's body, the AFE pour's only exit to the electrode. Closed, C3's
   decoupling return to U2 pin 4 became a 40 mm² island. **C3 is one of the
   parts moving this time.** Check this channel first.
2. A lane moved onto a band another net crossed in.
3. C4/C5 changed face when they lost their leads.

Also known tight spots: **R2** in the AFE column is placed 0.225 mm from its
neighbour with its 1.3 mm pad on x 19.025–20.325 — its pad narrows to 0.98 mm
and moves; and **the R2/R3/U2-pin-3 junction is the only critical node**, a
deliberately tiny copper island with no probe pad. Shrinking R2/R3 shrinks it —
that is a good thing (less leakage on a 10 MΩ node), but keep it an island and
do not let a pour or a lane creep into the space the 1206 bodies vacated.

C4/C5 stay on the bottom face anchored on pad 1 where they are now; their
0805→0603 pad shift is under 0.1 mm.

## README — update the present, append the past

Do **not** rewrite the historical *"This session"* sections; they are the log.
Update the current-state sections and append a new `## This session:` section.

Current-state edits:

- **Parts table** — every row above. Delete the *"10 MΩ … keep it"* note only
  if it is no longer true; it still is. Rewrite the *"10 µF is marked 100 V"*
  paragraph — C4/C5 are now a real 25 V Murata part; say what that buys
  (~8 µF effective at 3.3 V instead of ~5, worst case ~5 instead of ~3).
- **Deviations, D1 paragraph** — now PMEG3020ER-TP, 40 V 2 A SOD-123FL.
  *"Cost: ~0.85 V at 100 mA"* becomes ~0.4 V, and *"leaving ~2.5–3.75 V on
  VSYS"* becomes ~2.9–4.2 V. Re-derive the **GP29 VSYS/3 monitor correction**
  (*"add ~0.3 V (0.2 V at idle"*) from the new V<sub>F</sub> — the existing
  0.85 V drop and 0.3 V correction already disagree; fix that while you are
  there.
- **The motor table** D3 row and the *"D3 is an SS220F (SMB)"* paragraph — the
  SMB courtyard argument that kept D3 out of the pocket is void; say so.
- **The passives table** *"0603 where the order changed, 1206 where it did
  not"* and its *"Two package sizes on one board is deliberate"* paragraph —
  no longer true. Replace with the one-package statement.
- **Layout rules** and anywhere else that says SMB, 1206 or 0805 as a current
  fact.

The new session section should record, for the log:

- The SS220F was actually **SMAF (DO-221AC)**, not SMB — Robu's own listing
  says so. The `D_SMB` land pattern was oversized for it. Moot now, but it
  explains why the diodes had so much room.
- Why C1/C2/C6 are C0G and not X7R, because the next person will be tempted:
  X7R is piezoelectric and **this board has a vibration motor on it**; C0G also
  holds the 321 kHz corner (±30 ppm/°C vs ±15 %) and has ~4× less dielectric
  absorption on C2, the DC block across the TX→RX transition.
- Why C4/C5 are 25 V-rated 0603 and not 10 V: DC-bias derating is a field
  effect, and 3.3 V on a 25 V part is 13 % of rated versus 33 %.
- R2/R3 to 0603 is a judgment call the user made: 0603's 1.6 mm body flashes
  over at a lower ESD voltage than 1206's 3.2 mm, and J2 is skin contact.
  Accepted for a bench/research board. Write that down.

## BOM

`hardware/bom.csv` is committed and has a **Package** column that
`kicad-cli sch export bom` does not produce — it was derived from the footprint
(0603 / 0805 / SMB / SOT-23 / MSOP-8 / THT connector types, or the off-board
note for BT1, D2, M1). Regenerate it with the same seven columns
`Reference,Qty,Value,Package,Footprint,Description,DNP`, grouped by
Value+Footprint, refs comma-separated. Put that derivation in a small
`tools/gen_bom.py` so it is not ad hoc again, and have `gen_schematic.py` call
it after its own BOM check. D1/D3's Package is "SOD-123FL".

## Finish

Regenerate everything, render `build/pcb_top.png` / `pcb_bot.png` and look at
the AFE columns, the motor pocket and both diodes. Commit as one change with a
message in the style of the log (`git log --oneline -6`). Then update
`hardware/parts-update-prompt.md` (this file) to say it has been applied, or
delete it.
