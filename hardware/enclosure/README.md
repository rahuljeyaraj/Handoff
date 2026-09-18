# Enclosure — initial design

The band's box, as two 3D-printable parts. An initial design; a revised one will
replace these files.

| File | What |
|---|---|
| `bottom.3mf` | the bottom half. The board screws into it |
| `top.3mf` | the top half |

- **Board mounting.** The bottom has four bores for M3 brass heat-set inserts,
  5 mm across, on the board's hole pattern (33 × 55 mm between centres). Press
  the inserts in with a soldering iron, then screw the board down with M3 screws.
  The board side of this is `tools/gen_mount_footprint.py`: 3.4 mm unplated holes
  with a 6 mm boss keep-out.
- **Strap.** A 22 mm watch strap.
