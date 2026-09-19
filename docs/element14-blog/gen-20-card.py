# Generates docs/element14-blog/20-card.svg and .png -- section 4.3.
# Top row: the card page, delete, the band page with the red card icon.
# Bottom row: set up again, the phone label menu, Custom "IRQ", the card page
# back on the band.

from phonestrip import draw

STAGES = [
    {"shot": "43-1-card", "caption": "On the band",
     "ring": (990, 203, 112, 112)},
    {"shot": "43-2-delete", "caption": "Delete",
     "ring": (850, 1398, 200, 110)},
    {"shot": "43-3-band-nocard", "caption": "No card on the band",
     "ring": (540, 934, 1010, 180)},
    {"shot": "43-4-labels", "caption": "Pick a label",
     "ring": (834, 1510, 380, 112)},
    {"shot": "43-5-irq", "caption": "Custom: IRQ",
     "ring": (975, 203, 150, 105)},
    {"shot": "43-7-card-irq", "caption": "Back on the band"},
]

draw("20-card", STAGES, rows=[3, 3])
