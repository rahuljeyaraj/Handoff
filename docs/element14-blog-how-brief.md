# Writing brief — element14 blog, chapter 5 "The how" and after

Working brief, for execution. Rewritten 18 Sep 2026 after the session that drew
the 5.2 figure (`449d2ad`). Decisions below were settled with the user —
**do not relitigate them.** The rejected-approaches table at the end records what
was tried and why it failed, so none of it is rediscovered.

---

## Where we are

| Part | State |
|---|---|
| Chapters 1–4, §5.1 | ✅ written, in `docs/element14-blog.md` |
| Post §5.2 What's inside | ✅ committed 18 Sep 2026: hardware block diagram (figure 09) + why a Pico 2 W |
| Post §5.3 – §5.6 (this brief's §5.2 – §5.5) | ✅ committed 18 Sep 2026: bullets + figures 01–06 |
| Chapter 6 | ✅ committed 18 Sep 2026 (`dde2349`): 6.1–6.3, figures 07–08 |
| Chapter 7 *Watch it work* | ❌ **dropped 18 Sep 2026.** A draft (the 09:14 handshake on a clock, plus the M14 bench numbers) read as chapter 5 repeated with numbers. Do not bring it back |
| Post §5.5 From tone to bits | ✅ drafted 18 Sep 2026, uncommitted: Goertzel (what it replaces, why not an FFT), Manchester, the frame; figures 10–12. Old §5.5/5.6 are now **§5.6/5.7**. Kept short on purpose: no chip timing, no 25-vs-50 window, no moving threshold |
| Chapter 7 *The airport* (was 8) | ⬜ **next** |
| Appendix | ⬜ not started |

### Found drafting chapter 7 (18 Sep 2026) — the code against the post

- **Two frames a turn** (`link_sm.c` `frames_per_turn = 2`). Figure 08 draws one
  frame each, alternating; it needs a redraw to two-frame turns.
- **Two 3-frame cards take ~1.7 s** to finish both ways, with the "got yours"
  acknowledgements (R1 R2 | S1 S2 | R3 R1 | S3 S1 | R2 R3). §3.1 and §5.3 say a
  handshake is about a second.
- **A cut-short handshake never reaches the phone.** `handoff.c` `on_done()`
  forwards the card only on COMPLETE. §6.2 and figure 04 promise a brief touch
  still gets a name and number.
- The M14 bench run (105/105, 0.89 s) used **one-frame test cards**. Say so if it is ever quoted.

### Decided 18 Sep 2026, after the drafts

- **Short sections: bullets plus figures.** A long-prose draft of §5.2–5.5 was
  written and dropped as too much text; the bullet version is what is in the post.
  Chapters 6–8 follow the same form.
- **No emphasis on the 1 MΩ resistor**, in pictures or text. Say *where* the
  signal is lost ("getting into the body and out again"), never name the part.
  The §5.3 plan below that opens the megohm is superseded.
- **Only reasons the project records.** Do not present an inference (a GBW
  argument, a torn coating, ESD) as the design's reason for a choice.
- **Write as if every test is done.** No "not measured yet", no "worked out on
  paper" caveats. This overrides check 6 below for the post's wording.
- **Figures are 01–06**, renumbered: 01 layers, 02 the loop, 03 packed, 04 frames
  (shortest handshake at the top), 05 being heard (the old nobody-starts and
  heard-is-touch merged), 06 the hearer sends. The what-survives figure was dropped.
- **Colour means whose, not how important.** Blue is Rohit, green is Savithri,
  red is the handshake itself (the two bodies), grey is everything else — the
  same meaning as figure 01's legs. Solid fill only for a band talking (06).

- **Renumbered 18 Sep 2026, after chapter 6:** a new **§5.2 What's inside**
  (figure 09, the band's hardware as one block diagram) went in after §5.1, so
  the post's §5.2–5.5 are now **§5.3–5.6**. This brief keeps the old numbers
  below: its §5.2 is the post's §5.3, and so on. Figure 09 is numbered by when it
  was drawn, not where it sits.

`docs/element14-blog.md` currently ends on §6.3's last bullet (the inductor, left out). The next figure is `13-`. Figure 09 colours by direction (purple into the Pico, orange out of it, teal the Pico, grey not part of the band), the one exception to colour-means-whose.

## How we work

1. **Agree the words before drawing.** For any figure, propose every box's text
   as a table in chat. Iterate there. Draw only when the user says so.
2. **Test as a first-time reader.** Before calling a figure done, list what would
   confuse someone who has read only the chapters before it. That pass is what
   reshaped the 5.2 figure.
3. **Check wording against the code**, not the design documents. See
   "Verified facts" and "Stale values" below.
4. **Paste a plain-text copy of every finished section in chat** so the user can
   run it through text-to-speech. No tables in that copy.
5. **Commit only when the user confirms.** Repo style:
   `element14 blog: <what changed>`.

---

## Structure

The chapters after "The how" are **chapters of their own, not subsections of 5**.

```
### 5 The how
  #### 5.1 Plate on the back          written
  #### 5.2 What's inside              written (added 18 Sep; not in this brief's old numbering)
  #### 5.3 One card's journey         written   (this brief's "§5.2")
  #### 5.4 The wire is a person       written   (this brief's "§5.3")
  #### 5.5 The card is too big        written   (this brief's "§5.4")
  #### 5.6 Nothing says go            written   (this brief's "§5.5")
### 6 Where the reasoning was wrong   written
### 7 Watch it work                   DROPPED 18 Sep 2026 (numbers recap, no new idea)
### 8 The airport                     becomes 7 once 7 is gone
### Appendix — Build one yourself
```

| Part | Job |
|---|---|
| 5.2 | **what it is** — the whole system in one picture |
| 5.3 – 5.5 | **why** — one section per fact that breaks something |
| 6 | why, continued — the reasoning that was wrong |
| 7 | the numbers |
| 8 | the close |
| Appendix | reference |

## Voice

- **Rohit is not retired.** There is no handover paragraph. From §5.2 on, the
  author explains, and uses Rohit and Savithri as the running example: their
  09:14 handshake at the front desk (§3.1), the morning's stalls, the coffee
  lounge. The 5.2 figure already names its columns after them.
- Chapter 8 is where the story comes back to the front as narrative.
- No C code anywhere. No long paragraphs. Short sections, plain words.
- The user is dyslexic. **Diagrams carry the explanation; prose is support.** If a
  section needs three paragraphs to land, it is the wrong section.

## Rules for every section

1. **5.2 is what. 5.3–5.5 and 6 are why.** Not how. Every paragraph in them is a
   decision and the reason for it. A paragraph that explains a mechanism without
   justifying a choice belongs in the build log.
2. **Numbers:** no measured values in 5.2. Sparse in 5.3–5.5 — a number appears
   only when the number *is* the reason. Free hand in chapter 7, where they pay off.
3. **Ordinary layers get a sentence, never a section.** Nobody chose the checksum.
   Only the layers a body breaks are opened.
4. **Chapter 6 is design errors, not bugs.** "It was correct on paper and false in
   reality" belongs there. "We built it wrong" goes in the build log.
5. Never repeat a value from a design document without checking it against the
   code — see "Stale values".

## Vocabulary — match the 5.2 figure

The figure's words are the reference. Later sections use the same ones.

| Say | Not | Why |
|---|---|---|
| the **"I am here"** shout | beacon, burst, ping | 10 ms of flat carrier, no data, no identity — it says exactly and only that |
| **Bluetooth messages** (radio legs) / **frames** (body leg) | pieces, packets, chunks | two words for two mechanisms; "piece" is retired |
| **the common parts** | boilerplate | what every card has in common; 5.4 may name "boilerplate" once as the engineer's word |
| **the hearer sends, the shouter receives** | the first to shout sends | see Verified facts — this was got backwards once |
| **Rohit's band / Savithri's band** | band A / band B | names never flip owner the way "your" did |

---

## §5.2 — One card's journey

### The figure (done)

`docs/element14-blog/01-layers.svg`, 2200×1223, PNG 3520×1957.

- **One card, one direction:** Rohit's phone → his band → her band → her phone.
  Every box says what that layer does *to this card, going this way*.
- **Three colour-coded legs**, as header bands that partition the width. Each
  band's column title sits on a leg boundary, because the band is the hinge.

  | Leg | Colour | Where | When |
  |---|---|---|---|
  | 1 | blue | Rohit's phone → Rohit's band | at the desk, once |
  | 2 | red | Rohit's band → Savithri's band | the handshake, about a second |
  | 3 | green | Savithri's band → Savithri's phone | moments later |

- **The two app boxes are neutral grey** — the only boxes belonging to two legs.
  They are also where the card genuinely waits (boxes 6 and 17).
- **Bluetooth drawn as MAC, link, physical, greyed**, aligned with the body side's
  own three: the layers exist, they are just not ours.
- 22 numbered badges, arrows between every box, one uniform font size (21 px).
- The body link is labelled *the handshake — two bodies, coupled through skin*.
  The coupling capacitance lives on the medium, not in the physical box.

The 22 box texts live in the `S` dict at the top of `gen-01-layers.py`. Edit and
re-run; the script re-solves the font size.

### The prose (next)

- **Short.** Walk the reader into the figure, then out to the three facts.
- **Caption under the figure** (scope goes here, not in the image):
  *Rohit's card on its way to Savithri. Hers makes the same trip the other way,
  at the same time.*
- **End on the three facts.** They generate 5.3–5.5, one each:

  > A handshake is about a second long. Nobody announces it. And the wire is a
  > person.

  | Fact | Breaks | Section |
  |---|---|---|
  | the wire is a person | can we send anything at all, and hear it back | 5.3 |
  | about a second long | the card does not fit | 5.4 |
  | nobody announces it | nothing tells the band to start | 5.5 |

  Prefer three short lines over this table in the post itself — tables read
  badly in text-to-speech.

- **The figure deliberately leaves two questions open.** *Why not just send the
  file?* (answered in 5.4) and *what tells the band to start?* (5.5). The prose
  should leave the reader itching for both, not answer them.
- **Do not lead with** the bridge, six-layers-versus-two, "no network layer", or
  "the phone-side transport is ours". A draft built on those four points was
  rejected as the wrong emphasis. One may appear in passing only if it earns it.

---

## §5.3 — The wire is a person

Decisions to justify, in this order:

- The pad is **insulated and never touches skin** — half a capacitor, not a
  contact. Why that is the safer and the better-coupled choice. (Rohit already
  worked this out in §5.1; pick up from him.)
- The other face of the band **faces the room**; the circuit closes through the
  air. Why a return path is needed at all.
- **A megohm in series, both directions.** Why it is not negotiable, and what it
  costs — the single biggest loss in the system.
- **Therefore two amplifier stages.** The gain exists *because of* the resistor.
  Make that causal link explicit.
- **No mixer, no oscillator.** Filtering moved into software: ask one question,
  over and over — *is the tone there right now?* Listening only where the tone is
  means noise elsewhere is never counted.
- **Carrier frequency:** as high as possible, because coupling improves with
  frequency; capped by the converter, not by physics.
- The tone is **switched on and off** — on-off keying, the simplest case of
  amplitude-shift keying. No phase or frequency tracking between two crystals.

Numbers permitted, only as the reason for a choice: ~1.3 mV surviving, ×121 of
gain, ~17 dB from the maths. **Verify each against the code before use.**

Absorbs the safety story — "what safety cost and why we paid it", not a
compliance checklist. There is no separate safety section.

Source figures to redraw from: `m1-walkthrough/02-signal-journey.svg`,
`m1-walkthrough/03-goertzel.svg`, plus a new coupling-loop sketch.

## §5.4 — The card is too big

Decisions to justify:

- A real vCard is mostly **the common parts**, identical on every card ever
  written. In about a second it does not fit — **not even once**.
- So it does not cross as text: property names become tags, phone digits pack two
  to a byte, common email domains become one byte.
- **The real vCard is rebuilt at the far end** — by Savithri's *band*, before it
  reaches her phone. Only the common parts stop crossing. She still gets a proper
  `.vcf`.
- The band stores the packed form, never the text — that work happens once at the
  desk (leg 1), never during a handshake.
- Still too big for one frame, so it is **cut into frames ordered by importance,
  not position**: name, then first number, then email, then the rest.
- **Cut on field boundaries**, so any frame that arrives is readable on its own. A
  phone number is one field, so it can never arrive wearing the wrong label.
- **No asking again.** No time for a retry conversation. Graceful degradation
  instead: a brief touch gets a name and a number; a proper handshake gets the
  whole card; too short gets nothing — never something wrong.

Numbers permitted: 169 bytes → 79.

Source figures to redraw from: `m1-walkthrough/09-compact.svg`,
`m1-walkthrough/10-record.svg`.

## §5.5 — Nothing says go

The centrepiece. Most room, most figures.

- The band has **no button for this, no accelerometer, no touch sensor.** Nothing
  tells it a hand was shaken.
- **Listen-before-talk cannot work here.** On Ethernet or Wi-Fi the other machines
  are always reachable, so listening tells you something. Here, before skin meets
  skin, the other band is *unreachable* — its signal does not arrive at all.
  Listening cannot tell "nobody is here" from "someone is here but we have not
  touched".
- And if every band waits to hear someone, no band ever speaks.
- **The insight: being heard IS the touch.** The channel is the sensor.
- So each band free-runs, out of step with every other: the **"I am here"**
  shout, a moment for its own amplifier to recover, then listen. Repeat.
- **And there is no election, because there cannot be one.** Your ears open only
  after your own shout is over. So of any two shouts, **only the earlier one can
  be heard — and it is heard by the later shouter.** The hearer waits for
  silence and sends its card; the shouter receives. Both-send and both-listen
  are unreachable, not unlikely.
- In the figure: Rohit's band **heard** Savithri's shout, so Rohit's band sends
  (box 9); Savithri's band **made** the shout, so it listens and receives (box 14).
- One pad, so a band is deaf while it speaks and for a moment after. The end that
  just stopped talking gets twice that long before the other starts.

Numbers permitted, at the end only: every relative phase swept — 112 offsets, one
sender every time.

Source figures to redraw from: `m1-walkthrough/01-the-problem.svg` →
`02-the-trick.svg` → `simple-trigger/01-the-rule.svg`. Optional fourth:
`simple-trigger/07-why-not-just-send.svg`.

---

## Chapter 6 — Where the reasoning was wrong

Three. Design errors only — each was correct on paper and false in reality.

1. **A zero was a pin held low.** It should have been a pin let go of entirely.
   Holding it low dragged the band's own amplifier onto the rail; 17 ms to
   recover, against a 1 ms budget. Every frame ended in self-inflicted deafness.
   Found on paper, before the analogue circuit existed. Changed the firmware *and*
   a capacitor on the board.
2. **Repeating the first frame.** The design said re-send name-and-number every
   other frame so it gets half the airtime. Measured: that delivers a complete
   card in **0 %** of one-second contacts. Plain round robin delivers **50 %**.
   The spec had assumed frames were cheaper than they are. What keeps the promise
   is the priority *ordering*, not the repetition.
3. **The inductor that would have been free gain.** Resonating out the body's
   capacitance would have bought 20–30 dB for one cheap part. The safety resistor
   destroys the Q. Recorded so it is never rediscovered.

**Excluded — bugs, not reasoning:** the stack that overflowed into the other
core, the DMA latency that made two boards talk over each other, the chip erratum
on a released pin. Build log.

## Chapter 7 — Watch it work

No new mechanism. The 5.2 figure running both ways at once, with a clock on it.
Numbers get a free hand here.

- Hands meet; one band's "I am here" shout lands on the other.
- The one that heard it waits for silence, then sends.
- A moment to turn round; the other card comes back.
- They keep trading for as long as the grip lasts.
- Hands part. Both bands buzz. Both phones have a contact.
- **Measured:** 105 handshakes, 105 completed, both cards decoded every time,
  exactly one sender every time, mean 888 ms, zero collisions in 739 frames.

Source figure to redraw from: `m1-walkthrough/05-ideal-end-to-end.svg`.

## Chapter 8 — The airport

Chapter 1 set this up:

> he only meant to peep in on his way past, and now he has a flight to catch

Resolve it. The conversation that ran long, the flight, the handshake that keeps
it. Ends the post where it started.

## Appendix — Build one yourself

The replication steps. Long and mechanical is fine.

**Needs a hard visual break and a heading that says it is an appendix**, or
readers hit it after the close and think the post is rambling on.

Source: `docs/hardware-bringup.md` (steps 0–12, multimeter only, each with a
`scripts/bringup.py` command and a pass window), `README.md` build/flash, and
`hardware/build/order.md`.

---

## Figure style

Settled on the 5.2 figure. Every new figure follows it.

- **SVG and PNG both.** Files go in `docs/element14-blog/` as `NN-name.svg`,
  `NN-name.png`, with the generator beside them as `gen-NN-name.py`. The next
  figure is `02-`.
- **No header, no footer, no notes** inside the image. Scope and caveats go in the
  markdown caption.
- **The picture must stand alone.** Box text says what happens; a layer name alone
  ("presentation") means nothing.
- **One font size for every box**, title the same size as its sub-text, title bold
  only. Solve for it: the largest size at which every box fits.
- **Text fills its box.** Rows only as tall as their wordiest box. Dead space
  shrinks the text relative to the image.
- **Arrows guide the eye. Colour codes phases.** Never route a line through text.
- **Roughly 16:9.**
- **Redraw, never reuse, the old figures.** Everything in `m1-walkthrough/` and
  `simple-trigger/` has a header and small mixed fonts. They are source material
  only.

PNG render (cairosvg does not work on this machine; headless Chrome does):

```
"/c/Program Files/Google/Chrome/Application/chrome.exe" --headless --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1.6 --window-size=<W>,<H> \
  --default-background-color=ffffff \
  --screenshot="C:\work\Handoff\docs\element14-blog\NN-name.png" \
  "file:///C:/work/Handoff/docs/element14-blog/NN-name.svg"
```

---

## Verified facts

Checked against the code during the 5.2 session. Box numbers refer to the figure.

| Fact | Source |
|---|---|
| The phone sends vCard **text** over Bluetooth; the band parses it, packs it, stores only the packed form | `firmware/lib/record/store.h:6-8` |
| The **receiving band** rebuilds the vCard (box 18), then sends it to the phone in chunks | `firmware/apps/handoff/handoff.c:670`, `firmware/lib/hal_pico/ble.c` chunked `rx_vcard` pump |
| The receiving band **holds** the card until the phone subscribes (box 17) | `firmware/apps/handoff/handoff.c:478` (`s_rx_pending`) |
| **The band that hears a shout sends its card.** The band that shouted receives | `docs/simple-trigger-spec.md` §1–2, `firmware/lib/proto/beacon.h:17,28` |
| Of two shouts, the **earlier** is heard, by the later shouter | `docs/simple-trigger-spec.md` §2 |
| Frames are cut on field boundaries; a phone number is one field | `docs/firmware-architecture.md` §8.2 |
| Modulation is **on-off keying**; Manchester lives in the link layer | `docs/body-coupled-handshake-design.md:326`, `docs/firmware-architecture.md:66` |

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
| Frame repetition | weighted `0,1,0,2` | **round robin** |
| Interstage cap C1 | 100 nF | **330 pF** on the board |
| Bias resistor R3 | 10 MΩ | **1 MΩ** on the board |
| LED series resistors | 330 Ω | **100 Ω** on the board |
| Battery | 500 mAh | **1500 mAh** in the current BOM |
| Wearer feedback | "no LED or motor" | **LED, motor and button all exist** |
| Goertzel processing gain | +17 dB (design §5, at N = 50) | **~14 dB** at N = 25; left out of §5.3 |
| Raw vCard "does not fit, not even once" | true at 1000 bps | at 2000 bps one card as text is 936 ms: it fills the second alone, and the other card never gets a turn |
| 1.3 mV at the preamp | link-budget arithmetic | **an estimate**; the front end is not built. Say "the sums say" |

---

## Rejected approaches — do not go back to these

### Chapter structure

| Approach | Why it failed |
|---|---|
| Follow-the-signal walk with no full picture (body → board → bits → card) | Linear; the reader has nothing to place topics into. *A walk drawn over the full picture is fine — that is what the 5.2 figure is.* |
| Four named parts with questions as headings | Still linear, just grouped. |
| Level 0 / Level 1 zoom | Level 1 blocks were *disciplines*; Level 0 boxes were *places*. They did not map onto each other. |
| Chapters 6–8 and the appendix as subsections of 5 | They are not part of "how". |
| A handover paragraph retiring Rohit, fiction only at the close | Rohit and Savithri are the best running example the post has. |
| 5.2 prose built on four points: bridge, six-versus-two, no network layer, phone-side transport | The wrong emphasis. |

### The 5.2 figure

| Approach | Why it failed |
|---|---|
| Layer stack labelled by cargo ("carrier", "chips", "frames") | Content, not structure. |
| Phone drawn above the band | The phone is *beside* the band. Bluetooth is a second physical layer, not a higher one. |
| Peer lines between upper layers | Only the physical layer is a real connection. |
| Layer names alone in the boxes | "presentation" means nothing to a new reader. |
| A responsibility table beside the diagram | One image must give the whole picture. |
| Two-way text in one box ("turn the card into X, and back") | Too abstract; the reader cannot see the card move. Replaced by one direction of travel. |
| Mirrored B-side with repeated text; then a lean, empty B-side | Superseded by the one-way flow, where the far side has its own, different text. |
| One continuous 26-step path | Collapsed the desk and the handshake into one moment. Replaced by three legs. |
| "Your" for both people | Silently changed owner halfway across. Replaced by names. |
| Bluetooth as one grey box | Implied Bluetooth has no MAC, link or physical layer. |
| MAC boxes that read as opposite rules ("talks in step with nobody" / "waits for silence") | Looked like a contradiction. |
| **"Its shout landed first, so it sends"** | **Factually wrong.** The hearer sends. |
| "Amplitude-shift keyed" in the physical box; capacitance in the physical box | It is on-off keying; the capacitance is the medium, not the layer. |
| A U-turn arrow inside the app box | Ran through the text. |
| Each box auto-fitting its own font size | Uneven. One size for all. |

---

## Checks before a section is called done

1. Every paragraph in 5.3–5.5 and chapter 6 justifies a choice. If it only
   describes a mechanism, cut it.
2. No measured values in 5.2.
3. No value repeated from a design document without checking the tables above.
4. Words match the Vocabulary table and the 5.2 figure.
5. Short enough to read in one sitting without losing the thread.
6. Nothing presents a datasheet estimate as a measurement — notably the power
   figures in `11d-power.svg`, which were never measured.
7. Every new figure passes the first-time-reader test and follows Figure style.
8. A plain-text copy is in chat for text-to-speech.
