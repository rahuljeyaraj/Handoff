# Generates docs/element14-blog/22-met-again.svg and .png -- section 3.2.
# A busy morning, and Savithri (already renamed) at the bottom from 9:14. She
# shakes his hand again at 11:04: the same entry moves to the top, no second
# Savithri. Staged with stage-story-db.py modes before11 and eleven.

from phonestrip import draw

STAGES = [
    {"shot": "32b-1-before", "caption": "A busy morning"},
    {"shot": "32b-2-after", "caption": "Met again: back on top"},
]

draw("22-met-again", STAGES)
