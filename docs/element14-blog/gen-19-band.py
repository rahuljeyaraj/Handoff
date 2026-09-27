# Generates docs/element14-blog/19-band.svg and .png -- section 4.2.
# The band page; the ring is Find my band, the row he tries.
# Shot with the band on cells, so Battery reads "Full" as the text says; on USB
# the same row reads "USB power".

from phonestrip import draw

STAGES = [
    {"shot": "42-1-band", "caption": "The band page",
     "ring": (540, 1366, 1010, 180)},
]

draw("19-band", STAGES)
