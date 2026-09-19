# Generates docs/element14-blog/16-first-contact.svg and .png -- section 3.1.
# Savithri's card lands in the list; her page has the time, number and email.

from phonestrip import draw

STAGES = [
    {"shot": "31-1-home", "caption": "Her card arrives",
     "ring": (540, 815, 1010, 200)},
    {"shot": "31-2-savithri", "caption": "Met today, 9:14 am"},
]

draw("16-first-contact", STAGES)
