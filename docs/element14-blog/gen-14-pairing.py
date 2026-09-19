# Generates docs/element14-blog/14-pairing.svg and .png -- section 2.1.
# Scan the band's QR code, allow the phone to reach it, paired.
# Stage 1 is a PLACEHOLDER: the viewfinder was shot with nothing in front of the
# camera. When the real scan of the band's label is shot, save it as
# shots/21-1-scan.png and change "shot" below; nothing else moves.

from phonestrip import draw

STAGES = [
    {"shot": "21-1-viewfinder",  # PLACEHOLDER until the real QR scan is shot
     "caption": "Scan the QR code"},
    {"shot": "21-3-allow", "caption": "Allow",
     "ring": (540, 1450, 845, 150)},
    {"shot": "21-4-paired", "caption": "Paired",
     "ring": (540, 2086, 910, 140)},
]

draw("14-pairing", STAGES)
