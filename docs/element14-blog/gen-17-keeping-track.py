# Generates docs/element14-blog/17-keeping-track.svg and .png -- section 3.2.
# The routine after each stall: Vikram as received, renamed with a note in one
# edit, saved. Saving him to the phone's contacts is left to chapter 7 (the
# taxi), where it really happens.

from phonestrip import draw

STAGES = [
    {"shot": "32-0-vikram-new", "caption": "As received",
     "ring": (852, 203, 125, 125)},
    {"shot": "32-3-note", "caption": "Rename, add a note",
     "ring": (975, 203, 150, 105)},
    {"shot": "32-1-vikram", "caption": "Renamed, with a note"},
]

draw("17-keeping-track", STAGES)
