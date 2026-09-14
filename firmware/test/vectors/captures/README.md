# Captures

Recordings from real hardware. **Committed, deliberately.**

Development plan §1, second supporting rule:

> **Every hardware failure becomes a host test.** Any capture that broke the
> decoder on the bench is saved into `firmware/test/vectors/` and replayed in
> CI forever. The bug is not fixed until it is a regression test.

That is what this directory is for. It is the opposite case from
`../generated/`, which is derived from `tools/gen_vectors.py` on every test run
and is therefore git-ignored — a stale generated vector would quietly stop
matching a changed `HANDOFF_GZ_N` and the cross-check would pass anyway.

These files cannot be regenerated. You cannot re-record the moment a link
failed on a real body, in a particular room, with particular shoes on.

## Adding one

```
python tools/replay.py bench-dump.s16 --adopt m5-attenuator-680r \
    --milestone M5 --note "1000-packet run, one CRC failure at ~700"
```

That copies the file here, records it in `index.json`, and replays it through
the same decoder the test suite uses. Commit both.

## Format

`int16` little-endian ADC samples at 500 ksps, DC-centred — exactly what
`hal_pico/tlm_usb.c` emits for a triggered raw burst (development plan M4:
raw ADC is a *triggered burst*, never continuous, because 1 MB/s exceeds
full-speed USB CDC and design §13 forbids the tether during body contact
anyway).

Expect the first entries here around M5, when the bench and the M1 simulator
disagree for the first time. That disagreement is the entire point of M5 —
expect it rather than dread it.
