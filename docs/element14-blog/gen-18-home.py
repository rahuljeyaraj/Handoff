# Generates docs/element14-blog/18-home.svg and .png -- section 4.1.
# The home page newest first, flipped to A to Z, then a search that matches
# organisations and notes.

from phonestrip import draw

STAGES = [
    {"shot": "41-1-home", "caption": "Newest first",
     "ring": (848, 203, 125, 125)},
    {"shot": "41-3-az", "caption": "A to Z",
     "ring": (706, 203, 125, 125)},
    {"shot": "41-2-search", "caption": "Search: sens"},
]

draw("18-home", STAGES)
