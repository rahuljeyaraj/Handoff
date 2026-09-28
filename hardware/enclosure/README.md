# Enclosure

The band's box, as three 3D-printable parts.

| File | What |
|---|---|
| `bottom.3mf` | the bottom half. The board screws into it |
| `top.3mf` | the top half |
| `button.3mf` | the cap that presses SW2 through the top, 9 x 5.3 x 5 mm |

- **Board mounting.** The bottom has four bores for M3 brass heat-set inserts,
  5 mm across, on the board's hole pattern (33 × 55 mm between centres). Press
  the inserts in with a soldering iron, then screw the board down with M3 screws.
  The board side of this is `tools/gen_mount_footprint.py`: 3.4 mm unplated holes
  with a 6 mm boss keep-out.
- **Strap.** A 22 mm watch strap.
- **Printing.** PLA or PETG, 0.2 mm layers. `top.3mf` and `bottom.3mf` are
  slicer project files and carry the print settings they were sliced with;
  `button.3mf` is a plain mesh.
