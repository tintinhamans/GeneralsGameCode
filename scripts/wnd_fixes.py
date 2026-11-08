"""Per-layout rect overrides for scripts/go_wnd_layouts.py.

FIXES maps a layout's archive path to {window NAME: (x0, y0, x1, y1)} in 800x600 units. The rects
are absolute and applied after every rule; descendants move along, and the rules never move these
windows. Add an entry only for what the rules cannot decide, after a screenshot review.

HEADINGS maps a layout's archive path to {heading NAME: column NAME}. After every rule and override
the heading moves sideways, keeping its width, so its left edge is the column window's final left edge.
"""

FIXES = {
    # the game speed slider rect reached 10 units below its row; centred on the row, its label and value box follow it
    "Window/Menus/SkirmishGameOptionsMenu.wnd": {
        "SkirmishGameOptionsMenu.wnd:SliderGameSpeed": (175, 335, 265, 362),
    },
}

# column headings of the game setup screens, left aligned over their player list column or map preview
HEADINGS = {
    "Window/Menus/GameSpyGameOptionsMenu.wnd": {
        "GameSpyGameOptionsMenu.wnd:StaticTextMapPreview": "GameSpyGameOptionsMenu.wnd:MapWindow",
    },
    "Window/Menus/LanGameOptionsMenu.wnd": {
        "LanGameOptionsMenu.wnd:StaticTextFaction": "LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate0",
    },
    "Window/Menus/SkirmishGameOptionsMenu.wnd": {
        "SkirmishGameOptionsMenu.wnd:StaticTextPlayers": "SkirmishGameOptionsMenu.wnd:TextEntryPlayerName",
        "SkirmishGameOptionsMenu.wnd:StaticTextMapPreview": "SkirmishGameOptionsMenu.wnd:MapWindow",
    },
}
