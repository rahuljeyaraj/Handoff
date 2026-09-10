# Simplified contact trigger — implementation spec

Status: **implemented.** See §10 for what the implementation measured, and firmware-architecture §13.3 for the divergence record.
Written 2026-09-10. Supersedes `beacon.h`/`elect.h` and architecture §7.3, §7.6.

Owner decision: this is v1 on a bench. **Power is explicitly not a concern.**
Simplicity and provable correctness are. Duty-cycled receiving can be
reintroduced later without changing the rule below.

---

## 1. The rule, in full

Every band free-runs this loop, unsynchronised with any other:

    SHOUT      10 ms flat carrier          (deaf — own amplifier driving)
    SETTLE      1 ms                       (deaf — own amplifier recovering)
    LISTEN     50–100 ms, drawn per cycle  (ears open, continuously)

While listening, anything heard is one of exactly two things:

| what arrives | what it means | what to do |
|---|---|---|
| flat carrier, framer never locks | somebody's shout | wait for silence, then **send our card** |
| alternating preamble, framer locks | a card arriving | **receive it** |
| nothing, for the whole drawn window | nobody there | shout again |

**The listen timer counts silent time only.** It is held while a carrier is
present. Without this a band would shout over a card already in flight.

That is the entire trigger. There is no election, no backoff draw, no
listen-before-talk, no role hint, no redraw, no tie.

## 2. Why no election is needed

To hear the other band's shout you must have your ears open before their shout
ends. Your ears open 11 ms after your own shout began (10 ms shout + 1 ms
settle).

For two shouts starting at `t_A` and `t_B`, with `t_A < t_B`:

- B's ears are open at `t_A` — B always hears A.
- A's ears open at `t_A + 11`, by which time A's own shout is long over. A
  hears B only if B is still shouting then, i.e. `t_B + 10 > t_A + 12`, which
  requires `t_B > t_A + 2`. But if B heard A first, B stops shouting and waits.

The earlier shouter is always too late; the later shouter is always in time.
**At most one band can hear the other's shout, so the sender is decided by
physics.** Both-send and both-listen are unreachable states.

The only degenerate case is `|t_A - t_B|` smaller than the detector latency
(~1 ms), where neither hears. §4.1 covers it.

## 3. Constants

Derived from the existing chain in `firmware/lib/hal/config.h`. At
`HANDOFF_GZ_N` 25 and `HANDOFF_WINDOWS_PER_CHIP` 5 these evaluate to:

| name | value | derivation |
|---|---|---|
| `HANDOFF_CHIP_US` | 250 µs | existing |
| `HANDOFF_DETECT_US` | 1000 µs | 4 chips, existing |
| `HANDOFF_TURNAROUND_US` | 1000 µs | existing, design §9.7 |
| `HANDOFF_SHOUT_US` | 10 000 µs | `10 * HANDOFF_DETECT_US` |
| `HANDOFF_LISTEN_MIN_US` | 50 000 µs | new |
| `HANDOFF_LISTEN_MAX_US` | 100 000 µs | new |
| `HANDOFF_QUIET_WAIT_MAX_US` | 30 000 µs | new — cap on §4.3 |

Retained from the DSP layer, unchanged: preamble 32 chips (8 ms), marker
16 chips (4 ms), frame 624 chips (`FRAME_AIRTIME_US` = 156 ms), carrier
hysteresis 8 chips (2 ms), carrier floor EMA ~128 chips (32 ms).

### Static assertions to carry over or add

- `SHOUT_US <= 64 * CHIP_US` — `beacon.h`'s existing bound. A burst longer than
  `carrier.c`'s floor EMA is absorbed into the floor and stops reading as a
  carrier. 10 ms against a 16 ms limit.
- `SHOUT_US >= 4 * DETECT_US` — the peer must have time to raise the presence
  flag and still see the burst end.
- `LISTEN_MIN_US > SHOUT_US + TURNAROUND_US` — every cycle must contain real
  listening.
- `(LISTEN_MAX_US - LISTEN_MIN_US) >= 16 * DETECT_US` — the draw range is what
  decorrelates a simultaneous-shout collision. See §4.1.
- `QUIET_WAIT_MAX_US > SHOUT_US + 8 * CHIP_US` — the cap must never truncate a
  legal shout plus its detector hysteresis.

`HANDOFF_BACKOFF_MAX_US` and its assertion become dead. Delete both.

## 4. Corner cases and their answers

### 4.1 Both bands shout at the same instant

Both are deaf for their own 10 ms, so neither hears the other. Both then draw a
fresh listen duration. Whichever band's next shout comes first is heard by the
other, because the other is listening continuously — there are no deaf gaps
except during one's own shout.

A repeat requires the two next-shout times to land within ~1 ms of each other.
Over a 50 ms draw range that is roughly 4 % per round, 0.2 % over two rounds.

~~These figures are arithmetic, not simulated.~~ **Measured** (§6.3, 400
forced simultaneous starts): 0.5 % needed a third shout round, worst case
three. The arithmetic above was pessimistic by roughly eight times.

### 4.2 Shouts overlap partially

Covered by the proof in §2. The later shouter hears the earlier one, waits for
silence, and sends. The earlier shouter opens its ears after the other has
stopped, hears nothing, and is still listening when the card arrives. No
deadlock, no wrong-role hint, nothing to recover from.

This is the case that forced `elect_assume()` to discard its TARGET hint in the
old design. It stops existing.

### 4.3 A carrier that never stops

Noise, a stuck transmitter, or a shifted floor. Waiting forever would take the
band off the air. Cap the wait at `HANDOFF_QUIET_WAIT_MAX_US` (30 ms, 3× the
longest legal shout). On expiry: reset the carrier detector — the floor may
genuinely have moved — return to LISTEN, and **do not send**.

This differs from the old `BEACON_HOLD_MAX_US`, which elected anyway on expiry.
Sending into a channel that is provably busy is worse than waiting a cycle.

### 4.4 Contact lost mid-card

Existing behaviour is correct and must be preserved: on the contact budget
expiring, `LINK_COMPLETE` if their record is complete, else `LINK_ABORT`, and
**the partial record is kept** (architecture §8.4). Do not regress this.

### 4.5 Turn handover after the first card

Unchanged. The sender stops after its turn and listens; the receiver has been
listening throughout, sees silence, and takes the channel. The
`frames_per_turn` / `barren_turns` logic in `link_sm.c` stays.

`suspect_collision()` must stop calling `elect_collision()`. Replace with: stop
transmitting, reset the framer and carrier detector, return to LISTEN with a
fresh draw. Keep a bounded retry count so a wedged pair still terminates in
`COMPLETE`/`ABORT` rather than looping forever.

## 5. Code changes

### Rewrite

**`firmware/lib/proto/beacon.h`, `beacon.c`** — keep the filenames to limit
churn, replace the contents. New state set:

| state | listening? | exit |
|---|---|---|
| `TRIG_OFF` | no | armed |
| `TRIG_SHOUT` | no | 10 ms elapsed |
| `TRIG_SETTLE` | no | 1 ms elapsed |
| `TRIG_LISTEN` | yes | carrier → `TRIG_WAIT`; silent-timer expiry → `TRIG_SHOUT` |
| `TRIG_WAIT` | yes | framer locks → `TRIG_RECEIVE`; carrier clears → `TRIG_SEND`; 30 ms → `TRIG_LISTEN` |
| `TRIG_SEND` | — | terminal; caller transmits |
| `TRIG_RECEIVE` | — | terminal; caller receives |

**`TRIG_WAIT` must keep feeding the framer.** Carrier presence arrives ~1 ms
into a transmission but the framer needs ~6 ms of alternating run to lock, so
"flat or preamble?" cannot be answered at the instant the carrier is detected.
It is answered by which happens first: a lock, or silence.

**`firmware/lib/proto/link_sm.c`** — `poll_idle()` routes `TRIG_SEND` straight
to `LINK_TX_FRAME` and `TRIG_RECEIVE` straight to `LINK_RX_FRAME`. Delete the
`LINK_BACKOFF`/`LINK_LISTEN` arm of `link_sm_poll()`. Rework `link_sm_begin()`
per §5.1 and `suspect_collision()` per §4.5.

**`firmware/lib/proto/link_sm.h`** — remove `LINK_BACKOFF`, `LINK_LISTEN` and
the `elect_t` member. `link_sm_role()` may keep returning a sender/receiver
enum for telemetry; it is no longer an election result.

### Delete

- `firmware/lib/proto/elect.h`, `elect.c`
- `firmware/test/host/test_elect.c`, and its entries in `main.c` and `tests.h`
- `lib/proto/elect.c` from `firmware/CMakeLists.txt`
- `HANDOFF_BACKOFF_MAX_US` from `config.h`

`hal->random` stays — it now feeds the listen-duration draw. The injectable-RNG
hooks in `hal_host.h` stay; retarget their comments, which currently talk about
forcing election ties.

### 5.1 The one thing to get right

`link_sm_begin()` currently calls `carrier_reset()` because a band that woke
from a beacon has spent the whole hold feeding a full-power carrier into the
floor EMA, leaving the floor several times ambient. That is still true here.

- On the **`TRIG_SEND`** path: reset the carrier detector. Nothing is being
  received, and there is no longer a listen-before-talk for the reset to blind.
  Strictly simpler than today.
- On the **`TRIG_RECEIVE`** path: **do not reset the framer** — it is already
  locked and that lock is what got us here. The carrier detector is the open
  question. The floor re-primes over ~128 chips (32 ms), comfortably inside a
  156 ms frame, but `carrier_present()` and `last_carrier_us` drive handover
  during the exchange, so resetting mid-frame may make handover misread the
  channel as idle.

**This is the main implementation risk.** Settle it with a test, not by
reasoning. If a reset on the receive path destabilises handover, the fallback
is to reset the floor only, or to let it decay naturally.

> **Settled: the receive path does NOT reset the carrier detector.**
>
> End to end the two choices are bit-identical — same frames sent, same
> turnarounds, same rendezvous, over 60 triggered handshakes and 50
> host-triggered ones — because handover during a receive turn counts decoded
> frames and the framer is untouched either way. So the reset buys nothing.
>
> One layer down it costs something. `carrier.c` re-primes level and floor from
> the next chip after a reset, and during a frame that chip is a Manchester chip
> — high half the time. Primed on a high one, the floor sits at the carrier's
> own level and the slow EMA cannot fall back inside the frame: measured,
> presence never returns across the whole remaining 624 chips. Which chip it
> lands on is a coin flip, and `carrier_present()` is what drives handover.
>
> Nothing for a 50 % chance of blinding handover mid-frame is a bad trade, so
> it is not done. `test_beacon.c` pins the asymmetry, so a change to
> `carrier.c`'s floor constants cannot quietly make this the wrong answer.
>
> The same edge bit the two-node simulator, which used to start both ends in
> the same microsecond: the receiving end then primed on the frame's first
> preamble chip, which is high, and lost the whole first frame. `sim_twonode.c`
> now gives the receiver a millisecond of quiet first, which is what a band on a
> wrist has.

## 6. Tests — definition of done

`firmware/test/host/test_beacon.c`, rewritten. All must pass.

1. **Phase sweep.** Start B at every offset across a full worst-case cycle
   (0 to 111 ms, 1 ms steps). Every offset must rendezvous within a bounded
   time. Report the worst case.
2. **Exactly one sender.** Over the same sweep, assert the pair never ends up
   both-sending or both-receiving. This is the claim in §2 and it is the single
   most important test in the file.
3. **Simultaneous start.** Force identical start times across many seeds.
   Assert resolution within two rounds for all but a measured small fraction,
   and pin that fraction. **This replaces the estimate in §4.1 with a number.**
4. **Stuck carrier.** Hold a carrier present indefinitely. Assert the band
   returns to LISTEN within the cap and never sends.
5. **A shout is never mistaken for a frame.** Feed a 10 ms flat burst straight
   into the framer and assert zero syncs. This is what justifies flat over
   preamble; see §7.
6. **Handover unaffected.** The existing carousel and turn-taking tests must
   still pass — especially frames-sent counts, which are the regression canary
   for §5.1.

## 7. Two decisions re-examined and kept

**The shout stays a flat carrier, not a preamble.** A preamble shout works —
the listener sees no marker follow and concludes it was a shout — but it buys
nothing, because a flat tone already tells the listener a band is present and
about to listen, and the listener must wait for silence either way. The cost is
real: a preamble is exactly the pattern `frame.c` hunts for, so every shout
drags the peer's framer into a half-locked state ~10 times a second, and a
decaying burst tail can supply a false marker. A flat carrier has no
transitions at all, so it cannot be mistaken for a frame — unambiguous by
construction rather than by timeout. Test 6.5 pins this.

**The shout stays short, rather than sending the card blind.** Transmitting the
whole 156 ms card every cycle would remove a round trip, but it makes a band
deaf ~68 % of the time instead of ~15 %, so two bands would frequently transmit
over each other and lose both cards — and the §2 proof, which depends on a
short deaf window, would collapse. The election would have to come back. (Duty
figures are arithmetic from the cycle lengths, not measured.)

## 8. Documentation to update

- architecture §7.1 — state table: drop `BACKOFF` and `LISTEN`; `IDLE` now
  exits to `TX_FRAME` or `RX_FRAME` directly.
- architecture §7.3 — role election. Delete, replace with a pointer to §2 here.
- architecture §7.6 — rewrite around this spec.
- design §9.6 — its backoff-and-redraw election no longer describes the
  firmware. Record the divergence rather than leaving it silently stale.

## 9. Known-stale claims — do not carry forward

`beacon.h` and architecture §7.6 both describe "the 20 ms post-beacon listen".
The constant evaluates to 24 ms (16 + 2 + 6). The prose is wrong. Both
disappear in this rewrite; do not copy the number forward.

`elect.c`'s comment on `elect_assume()` states that two partially-overlapping
beacons make *both* ends conclude TARGET. By the geometry in §2 only one end
can; the observed failure was one end waiting for a card that was never coming.
The design decision it justified was correct, but the reason given was
overstated. Do not repeat it.

## 10. What was unverified in this spec, and what it measured

The spec stated these plainly so they would not be inherited as facts. Three of
the four are now measured; the fourth still is not.

| claim | as written | as measured |
|---|---|---|
| §4.1 repeat rate after a simultaneous shout | ~4 % per round, ~0.2 % over two — arithmetic | **0.5 %** of 400 forced collisions needed a third round; worst case three rounds |
| §2 no-tie proof | timing algebra, not machine-checked | holds at **all 112** phase offsets and all 400 forced collisions; no pair ever both-sent or both-listened |
| rendezvous latency | one cycle, by construction | worst **108 ms**; no offset needed a second cycle |
| §7 duty-cycle and power figures (~10.8 mA against ~3.9 mA, ~39 h against ~85 h on 500 mAh) | arithmetic from cycle lengths; datasheet estimates | **still unmeasured.** Carried into firmware-architecture §13's open items rather than presented as a result |

One thing the spec did not anticipate, found while settling §5.1: a `carrier_t`
re-primed on a high Manchester chip stays blind for the rest of the frame. See
the note in §5.1.

## 11. Visual reference

`docs/simple-trigger/` — seven diagrams covering the rule, the ideal case, both
corner cases, the no-tie proof, and the two rejected alternatives in §7.
