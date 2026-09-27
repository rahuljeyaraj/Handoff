# Generates docs/element14-blog/14-pairing.svg and .png -- section 2.1.
# Scan the band's QR code, allow the phone to reach it, paired.

from phonestrip import draw

STAGES = [
    {"shot": "21-1-scan", "caption": "Scan the QR code"},
    {"shot": "21-3-allow", "caption": "Allow",
     "ring": (540, 1450, 845, 150)},
    {"shot": "21-4-paired", "caption": "Paired",
     "ring": (540, 2086, 910, 140)},
]

draw("14-pairing", STAGES)
