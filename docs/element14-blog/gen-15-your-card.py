# Generates docs/element14-blog/15-your-card.svg and .png -- section 2.2.
# From "Set it up": the empty card, filled in and saved, home with the card on
# the band (green card icon) and no handshakes yet.

from phonestrip import draw

STAGES = [
    {"shot": "22-1-empty-editor", "caption": "Your card, empty"},
    {"shot": "22-2-filled", "caption": "Fill it in, Save",
     "ring": (975, 203, 150, 105)},
    {"shot": "22-3-home", "caption": "Card on the band"},
]

draw("15-your-card", STAGES)
