"""Per-layout rect overrides for scripts/go_wnd_layouts.py.

FIXES maps a layout's archive path to {window NAME: (x0, y0, x1, y1)} in 800x600 units. The rects
are absolute and applied after every rule; descendants move along, and the rules never move these
windows. Add an entry only for what the rules cannot decide, after a screenshot review.

HEADINGS maps a layout's archive path to {heading NAME: column NAME}. After every rule and override
the heading moves sideways, keeping its width, so its left edge is the column window's final left edge.

LIST_HEADERS maps a layout's archive path to {list NAME: {"cells": (header NAME or None per list column), "corner":
NAME, "arrow": NAME}}, corner optional. Every pass the cells sit on the list top over their columns, None for a column
without a header (always for a 0% column), the corner cell over the scroll bar and the sort arrow inside the first sorting
cell; the rules never move them.
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

# GO signs in on the GameSpy login screen; only the backdrop, the frame, the title and the GO sign-in panel show there
BLANK_LAYOUTS = {
    "Window/Menus/GameSpyLoginProfile.wnd": (
        "GameSpyLoginProfile.wnd:Line",
        # the GO sign-in panel (scripts/wnd_sources); WOLLoginMenu.cpp shows the windows of each sign-in state
        "GameSpyLoginProfile.wnd:StaticTextSignInStatus",
        "GameSpyLoginProfile.wnd:ButtonProviderDiscord",
        "GameSpyLoginProfile.wnd:ButtonProviderSteam",
        "GameSpyLoginProfile.wnd:ButtonProviderGameReplays",
        "GameSpyLoginProfile.wnd:ButtonUseAnotherAccount",
        "GameSpyLoginProfile.wnd:ButtonCancel",
    ),
    "Window/Menus/GameSpyLoginQuick.wnd": ("GameSpyLoginQuick.wnd:MainMenuBackground",),
}

# sort headers of the online game list; the columns follow the COLUMN_ enum in Core LobbyUtils.cpp
LIST_HEADERS = {
    "Window/Menus/WOLCustomLobby.wnd": {
        "WOLCustomLobby.wnd:ListboxGamesLarge": {
            # one flat strip on the list top: the game name label, the map, players and ping sort buttons
            "cells": (
                "WOLCustomLobby.wnd:StaticTextHeaderGame",
                "WOLCustomLobby.wnd:ButtonHeaderMap",
                None,  # ladder
                "WOLCustomLobby.wnd:ButtonHeaderPlayers",
                None,  # password lock icon
                None,  # observers
                None,  # ranked stats icon
                "WOLCustomLobby.wnd:ButtonHeaderPing",
            ),
            "arrow": "WOLCustomLobby.wnd:WindowHeaderArrow",
        },
    },
}
