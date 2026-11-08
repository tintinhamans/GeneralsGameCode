"""Per-layout rect overrides for GenOnline/tools/go_wnd_layouts.py.

FIXES maps a layout's archive path to {window NAME: (x0, y0, x1, y1)} in 800x600 units. The rects
are absolute and applied after every rule; descendants move along, and the rules never move these
windows. Add an entry only for what the rules cannot decide, after a screenshot review.
"""

FIXES = {}
