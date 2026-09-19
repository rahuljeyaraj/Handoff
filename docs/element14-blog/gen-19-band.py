# Generates docs/element14-blog/19-band.svg and .png -- section 4.2.
# The band page; the ring is Find my band, the row he tries.
# Battery reads "USB power" because the bench band runs off USB: reshoot on
# battery (shots/42-1-band.png) to match the text's "Full".

from phonestrip import draw

STAGES = [
    {"shot": "42-1-band", "caption": "The band page",
     "ring": (540, 1366, 1010, 180)},
]

draw("19-band", STAGES)
