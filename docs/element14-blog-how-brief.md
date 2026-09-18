# Writing brief — element14 blog, chapter 5 "The how"

Working brief, for execution. Every decision below was settled with the user over
a long session. **Do not relitigate them.** Four earlier structures were rejected;
the reasons are recorded under "Rejected approaches" so they are not rediscovered.

On first use, copy this to `docs/element14-blog-how-brief.md` — repo convention
(`docs/android-pairing-page-brief.md`, `docs/band-ownership-brief.md`).

---

## Target

Append to `docs/element14-blog.md`, after the existing §5.1. No changes to
chapters 1–4.

## Voice and format

- Chapters 1–4 and §5.1 are narrative fiction: Rohit at an expo. §5.1 ends on
  *"He knew what it was now. The next question was how."*
- A handover paragraph (~4 lines) retires Rohit, then it is the author explaining.
- **Fiction returns exactly once, at the close** (Rohit, the airport).
- No C code anywhere. No long paragraphs. Short sections, plain words.
- The user is dyslexic. Diagrams carry the explanation; prose is support. If a
  section needs three paragraphs to land, it is the wrong section.

## The shape

| § | Section | Job |
|---|---|---|
| 1 | The layer table | **what it is** |
| 2 | The wire is a person | **why** |
| 3 | The card is too big | **why** |
| 4 | Nothing says go | **why** |
| 5 | Where the reasoning was wrong | why, continued — honestly |
| 6 | Watch it work | the numbers |
| 7 | Rohit, the airport | the close |
| 8 | Appendix — build one yourself | reference |

## Rules that govern every section

1. **§1 is what. §2–5 are why.** Not how. Every paragraph in §2–5 is a decision
   and the reason for it. If a paragraph explains a mechanism without justifying a
   choice, it belongs in the build log.
2. **Numbers:** none in §1. Sparse in §2–4 — a number appears only when the number
   *is* the reason. Free hand in §6, which is where they finally pay off.
3. **Ordinary layers get a sentence, never a section.** Nobody chose the checksum;
   it is what anyone would do. Only the layers a body breaks are opened.
4. **§5 is design errors, not bugs.** "We built it wrong" is a bug and goes in the
   build log. "It was correct on paper and false in reality" is a reasoning error
   and belongs here.
5. Never repeat a value from the design document without checking it against the
   code — see the stale-values table at the end.

---

## §1 — The layer table

Purpose: the whole technology in one read. The reader should be able to explain
Handoff to someone else after this section alone.

**One diagram**, four stacks, two media. The band is double-width because it has
two interfaces. Only the physical layer is a real connection — no peer lines
between the upper layers.

```
  PHONE A          BAND A             BAND B          PHONE B
 ┌────────┐      ┌─────┬─────┐    ┌─────┬─────┐      ┌────────┐
 │  app   │      │    app    │    │    app    │      │  app   │
 ├────────┤      ├─────┼─────┤    ├─────┼─────┤      ├────────┤
 │  pres  │      │pres │pres │    │pres │pres │      │  pres  │
 ├────────┤      ├─────┼─────┤    ├─────┼─────┤      ├────────┤
 │ trans  │      │trans│trans│    │trans│trans│      │ trans  │
 ├────────┤      ├─────┼─────┤    ├─────┼─────┤      ├────────┤
 │        │      │     │ mac │    │ mac │     │      │        │
 │   BT   │      │ BT  │link │    │link │ BT  │      │   BT   │
 │        │      │     │phys │    │phys │     │      │        │
 └───┬────┘      └──┬──┴──┬──┘    └──┬──┴──┬──┘      └───┬────┘
     └─── radio ────┘     └── bodies ──┘   └─── radio ───┘
```

**Then the responsibility table.** Boxes say what each layer is *for* — one job,
plainly. Not the data it carries, not the format it uses.

| | **body side** | **phone side** |
|---|---|---|
| **app** | keep my card, take theirs, know when the swap is done | |
| **pres** | turn the card into as few bytes as possible, and back | turn the card into vCard text, and back |
| **trans** | cut those bytes into frame-sized pieces, put them back together | same, in Bluetooth-sized pieces |
| **mac** | decide when to talk and who talks | *Bluetooth's* |
| **link** | mark where a message starts and ends; throw away damaged ones | *Bluetooth's* |
| **phys** | turn bytes into a tone on the skin, and back | *Bluetooth's* |

Points to make, briefly:

- `app` spans both columns on the band — that is what makes it a **bridge**.
- **Six layers ours on the right, two on the left.** Bluetooth gives us four for
  free. That asymmetry is why the rest of the chapter is about the right side.
- **There is no network layer.** No addresses, no routing — there is no one else
  it could be for. **The touch is the address.**
- The `trans` row on the phone side **is ours** (a `seq | total` header we wrote,
  because a Bluetooth message is only guaranteed ~20 bytes). Say so — it is the
  one thing below `pres` that Bluetooth does not do for us.

**Then the three facts.** These generate everything that follows:

> A handshake is about a second long. Nobody announces it. And the wire is a
> person.

Each fact breaks exactly one thing, one-to-one, and each becomes a section:

| fact | breaks |
|---|---|
| the wire is a person | can we send anything at all, and hear it back |
| a second long | the card does not fit |
| nobody announces it | nothing tells the band to start |

**No numbers in this section.** Not one.

---

## §2 — The wire is a person

Decisions to justify, in this order:

- The pad is **insulated and never touches skin** — it is half a capacitor, not a
  contact. Why that is the safer and the better-coupled choice.
- The other face of the band **faces the room**; the circuit closes through the
  air. Explain why a return path is needed at all.
- **A megohm in series, both directions.** Why it is not negotiable, and what it
  costs — this is the single biggest loss in the system.
- **Therefore two amplifier stages.** The gain exists because of the resistor,
  not despite it. Make that causal link explicit.
- **No mixer, no oscillator.** The filtering moved into software: ask one
  question, over and over — *is the tone there right now?* Listening only where
  the tone is means noise elsewhere is never counted.
- **Carrier frequency:** as high as possible, because coupling improves with
  frequency; capped by the converter, not by physics.

Numbers permitted — only as the reason for a choice: ~1.3 mV surviving, ×121 of
gain, ~17 dB from the maths.

Diagrams: new coupling-loop sketch; `m1-walkthrough/02-signal-journey.svg`;
`m1-walkthrough/03-goertzel.svg`.

Also absorbs the safety story — "here is what safety cost and why we paid it",
not a compliance checklist. There is no separate safety section.

---

## §3 — The card is too big

Decisions to justify:

- A real vCard is mostly boilerplate identical on every card ever written. At one
  second, it does not fit — **not even once**.
- So it does not cross as text: property names become tags, phone digits pack two
  to a byte, common email domains become one byte.
- **The real vCard is rebuilt at the far end.** Only the boilerplate stops
  crossing. The user still gets a proper `.vcf`.
- The band stores the packed form, never the text — that work happens once at
  setup, never during a handshake.
- Still too big for one frame, so it is **cut into pieces ordered by importance,
  not position**: name, then first number, then email, then the rest.
- Cut on field boundaries, so **any piece that arrives is readable on its own**.
- **No asking again.** There is no time for a retry conversation. Graceful
  degradation instead: a brief touch gets a name and a number; a proper handshake
  gets the whole card; too short gets nothing — never something wrong.

Numbers permitted: 169 bytes → 79.

Diagrams: `m1-walkthrough/09-compact.svg`, `m1-walkthrough/10-record.svg`.

---

## §4 — Nothing says go

The centrepiece. Most room, most figures.

- The band has **no button for this, no accelerometer, no touch sensor.** Nothing
  tells it a hand was shaken.
- **Listen-before-talk cannot work here.** On Ethernet or Wi-Fi the other machines
  are always reachable, so listening tells you something. Here, before skin meets
  skin, the other band is *unreachable* — its signal does not arrive at all.
  Silence means nothing. Listening cannot tell "nobody is here" from "someone is
  here but we have not touched".
- And if every band waits to hear someone, no band ever speaks.
- **The insight: being heard IS the touch.** No sensor needed — the channel is the
  sensor.
- So each band free-runs, out of step with every other: shout briefly, wait for
  its own amplifier, listen, repeat.
- **And there is no election, because there cannot be one.** Your ears open only
  after your own shout is over, so of any two shouts only the *later* can be
  heard. Both-send and both-listen are unreachable, not unlikely.
- One pad, so you are deaf while you speak and for a moment after. The end that
  just stopped talking gets twice that long before the other starts.

Numbers permitted, at the end only: every relative phase swept — 112 offsets, one
sender every time.

Diagrams: `m1-walkthrough/01-the-problem.svg` → `02-the-trick.svg` →
`docs/simple-trigger/01-the-rule.svg`. Optional fourth:
`docs/simple-trigger/07-why-not-just-send.svg`.

---

## §5 — Where the reasoning was wrong

Three. Design errors only — each was correct on paper and false in reality.

1. **A zero was a pin held low.** It should have been a pin let go of entirely.
   Holding it low dragged the band's own amplifier onto the rail; 17 ms to
   recover, against a 1 ms budget. Every frame ended in self-inflicted deafness.
   Found on paper, before the analogue circuit existed. Changed the firmware *and*
   a capacitor on the board.
2. **Repeating the first fragment.** The design said re-send name-and-number every
   other frame so it gets half the airtime. Measured: that delivers a complete card
   in **0 %** of one-second contacts. Plain round-robin delivers **50 %**. The
   spec had assumed frames were cheaper than they are. What keeps the promise is
   the priority *ordering*, not the repetition.
3. **The inductor that would have been free gain.** Resonating out the body's
   capacitance would have bought 20–30 dB for one cheap part. The safety resistor
   destroys the Q. Recorded so it is never rediscovered.

**Excluded — bugs, not reasoning. Do not put these here:** the stack that
overflowed into the other core, the DMA latency that made two boards talk over
each other, the chip erratum on a released pin. Build log.

---

## §6 — Watch it work

No new mechanism. The layer table running at once, with a clock on it. This is
where numbers get a free hand and finally pay off.

- Hands meet; one band's shout lands on the other.
- The one that heard it waits for silence, then sends.
- A moment to turn round; the other card comes back.
- They keep trading for as long as the grip lasts.
- Hands part. Both bands buzz. Both phones have a contact.
- **Measured:** 105 handshakes, 105 completed, both cards decoded every time,
  exactly one sender every time, mean 888 ms, zero collisions in 739 frames.

Diagram: `m1-walkthrough/05-ideal-end-to-end.svg`.

---

## §7 — Rohit, the airport

Fiction returns. Chapter 1 already set this up:

> he only meant to peep in on his way past, and now he has a flight to catch

Resolve it. The conversation that ran long, the flight, the handshake that keeps
it. Ends the post where it started.

---

## §8 — Appendix: build one yourself

The replication steps. Long and mechanical is fine — nobody is reading for
narrative here.

**Needs a hard visual break and a heading that says it is an appendix**, or
readers hit it after the emotional close and think the post is rambling on.

Source: `docs/hardware-bringup.md` (steps 0–12, multimeter only, each with a
`scripts/bringup.py` command and a pass window), `README.md` build/flash, and
`hardware/build/order.md`.

---

## Rejected approaches — do not go back to these

| Approach | Why it failed |
|---|---|
| Follow-the-signal walk (body → board → bits → card) | Linear. Reader has no full picture to place topics into. |
| Four named parts with questions as headings | Still linear, just grouped. |
| Level 0 / Level 1 zoom | Level 1 blocks were *disciplines*; Level 0 boxes were *places*. They did not map onto each other. |
| Layer stack labelled by cargo ("carrier", "chips", "frames") | That is content, not structure. Use layer names. |
| Phone drawn above the band | Phone is *beside* the band. Bluetooth is a second physical layer, not a higher one. |
| Boxes labelled with data ("the contact card", "vCard") | Boxes say **responsibility** — one job, plainly. |
| Peer lines drawn between upper layers | Only the physical layer is a real connection. |
| "written for the phone / packed for the body" | Every link is bidirectional. Labels must be neutral. |
| A second table to justify skipping ordinary layers | Five of six rows existed to say "nothing to see". Replaced by the three facts, which are causal rather than a filter. |

---

## Fact sources

| For | Read |
|---|---|
| Rates, timings, every constant | `firmware/lib/hal/config.h` |
| Component values as built | `hardware/README.md`, `hardware/review.md` |
| The trigger and the no-tie proof | `docs/simple-trigger-spec.md` §1–2 |
| Payload, fragmentation, carousel | `docs/firmware-architecture.md` §8 |
| Measured bench results (M13, M14) | `docs/development-plan.md` |
| Physics, circuit, safety | `docs/body-coupled-handshake-design.md` §3–6, §13 |
| Build and bring-up steps | `docs/hardware-bringup.md` |

## Stale values — trust the code, not the design document

| Thing | Design doc | Actually |
|---|---|---|
| Chip / bit rate | 2000 chips/s, 1000 bps | **4000 chips/s, 2000 bps** |
| Detection window | 50 samples | **25 samples** |
| Frame airtime | ~350 ms | **156 ms** |
| TX pin | GP2 | **GP11** on the PCB |
| Fragment repetition | weighted `0,1,0,2` | **round robin** |
| Interstage cap C1 | 100 nF | **330 pF** on the board |
| Bias resistor R3 | 10 MΩ | **1 MΩ** on the board |
| LED series resistors | 330 Ω | **100 Ω** on the board |
| Battery | 500 mAh | **1500 mAh** in the current BOM |
| Wearer feedback | "no LED or motor" | **LED, motor and button all exist** |

## Checks before each section is called done

1. Every paragraph in §2–5 justifies a choice. If it only describes a mechanism,
   cut it.
2. §1 contains no numbers.
3. No value repeated from the design doc without checking the table above.
4. The section is short enough to read in one sitting without losing the thread.
5. Nothing presents a datasheet estimate as a measurement — notably the power
   figures in `11d-power.svg`, which were never measured.
