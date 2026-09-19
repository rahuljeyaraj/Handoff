# Generates docs/element14-blog/21-settings.svg and .png -- section 4.4.
# Settings, the Theme picker, home in Dark.

from phonestrip import draw

STAGES = [
    {"shot": "44-1-settings", "caption": "Settings",
     "ring": (540, 742, 1010, 180)},
    {"shot": "44-2-theme", "caption": "Theme",
     "ring": (300, 1390, 400, 118)},
    {"shot": "44-3-dark", "caption": "Dark"},
]

draw("21-settings", STAGES)
