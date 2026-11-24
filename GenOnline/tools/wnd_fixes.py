"""Per-layout rect overrides for GenOnline/tools/genonline_wnd_layouts.py.

FIXES maps a layout's archive path to {window NAME: (x0, y0, x1, y1)} in 800x600 units. The rects
are absolute and applied after every rule; descendants move along, and the rules never move these
windows. Add an entry only for what the rules cannot decide, after a screenshot review.

HEADINGS maps a layout's archive path to {heading NAME: column NAME}. After every rule and override
the heading moves sideways, keeping its width, so its left edge is the column window's final left edge.

LIST_HEADERS maps a layout's archive path to {list NAME: {"cells": (header NAME or None per list column), "corner":
NAME, "arrow": NAME}}, corner optional. Every pass the cells sit on the list top over their columns, None for a column
without a header (always for a 0% column), which widens the cell before it, the corner cell over the scroll bar and the sort arrow inside the first sorting
cell; the rules never move them.
"""

def _setup_rects(zero_hour=True):
    """Layout G2 of the online game setup screen: the player rows and the chat on the left, the Game panel on the right, the
    buttons at the retail width. The icon cell is the ping window, the accept markers stay hidden, the kick and ban buttons sit right of the team.
    The Game panel holds a fixed grid of options and the status list under the map. Generals has no starting cash, superweapon or army controls."""
    p = "GameSpyGameOptionsMenu.wnd:"
    rects = {
        p + "StaticTextPlayers": (85, 85, 225, 109),
        p + "StaticTextColor": (229, 85, 317, 109),
        p + "StaticTextFaction": (321, 85, 417, 109),
        p + "StaticTextTeam": (421, 85, 483, 109),
        p + "ListboxChatWindowGameSpyGameSetup": (47, 309, 531, 480),
        p + "TextEntryChat": (47, 486, 497, 511),
        p + "ButtonEmote": (501, 486, 531, 511),
        p + "ButtonStart": (47, 517, 219, 553),
        p + "ButtonBack": (581, 517, 753, 553),
        p + "PanelGame": (537, 85, 753, 511),
        p + "StaticTextMapPreview": (545, 89, 745, 109),
        p + "ButtonEditLobby": (727, 48, 751, 72),
        p + "MapWindow": (545, 113, 745, 313),
        p + "TextEntryMapDisplay": (545, 319, 745, 347),
        p + "ButtonSelectMap": (545, 353, 745, 377),
    }
    # the options under SELECT MAP are a fixed grid of two columns, Generals keeps the camera height in the left one
    left, right = (545, 643), (647, 745)
    camera = left if not zero_hour else right
    rects[p + "MaxCameraHeightLabel"] = (camera[0], 383, camera[1], 399)
    rects[p + "ComboBoxMaxCameraHeight"] = (camera[0], 401, camera[1], 421)
    if zero_hour:
        rects[p + "StartingCashLabel"] = (left[0], 383, left[1], 399)
        rects[p + "ComboBoxStartingCash"] = (left[0], 401, left[1], 421)
        rects[p + "CheckboxLimitSuperweapons"] = (left[0], 427, left[1], 447)
        rects[p + "CheckBoxLimitArmies"] = (right[0], 427, right[1], 447)
    rects[p + "CheckBoxUseStats"] = (left[0], 427, left[1], 447)
    rects[p + "ListboxInfo"] = (545, 453, 745, 503)
    for i in range(8):
        top = 111 + i * 24
        rects[p + f"GenericPing{i}"] = (47, top, 81, top + 20)
        rects[p + f"StaticTextPlayer{i}"] = (85, top, 225, top + 20)
        rects[p + f"ComboBoxPlayer{i}"] = (85, top, 225, top + 20)
        rects[p + f"ComboBoxColor{i}"] = (229, top, 317, top + 20)
        rects[p + f"ComboBoxPlayerTemplate{i}"] = (321, top, 417, top + 20)
        rects[p + f"ComboBoxTeam{i}"] = (421, top, 483, top + 20)
        rects[p + f"ButtonKick{i}"] = (487, top, 507, top + 20)
        rects[p + f"ButtonBan{i}"] = (511, top, 531, top + 20)
    return rects


# the Generals retail layout has none of the Zero Hour only controls, which the generator adds after the overrides
_GENERALS_ADDED = {"CheckBoxUseStats", "CheckboxLimitSuperweapons", "CheckBoxLimitArmies", "ComboBoxStartingCash", "StartingCashLabel",
                   "MaxCameraHeightLabel", "ComboBoxMaxCameraHeight"}


def _welcome_rects(zero_hour):
    """Layout of the welcome screen: the message box left of the four buttons, the rank card and the army stats grid in a row under
    both, Main Menu at the Back button position. The Generals stats boxes are not in its retail layout, the generator places them."""
    p = "WOLWelcomeMenu.wnd:"
    rects = {
        p + "InfoListbox": (47, 89, 575, 349),
        p + "ButtonQuickMatch": (581, 89, 753, 124),
        p + "ButtonCustomMatch": (581, 132, 753, 167),
        p + "ButtonMyInfo": (581, 175, 753, 210),
        p + "ButtonOptions": (581, 218, 753, 253),
        p + "ButtonBuddies": (581, 261, 753, 296),
        p + "ButtonBack": (581, 517, 753, 553),
        p + "PlayerInfoParent": (47, 355, 753, 511),
        p + "StaticTextRank": (47, 355, 175, 379),
        p + "RankBorder": (47, 383, 175, 511),
        p + "RankLine": (47, 509, 175, 510),
        p + "ProgressBarRank": (47, 487, 175, 511),
        p + "WinRank": (47, 383, 175, 511),
    }
    if not zero_hour:
        return rects
    rects.update({
        p + "FactionImage": (127, 463, 175, 511),
        p + "StaticTextCommunity": (181, 355, 753, 379),
        p + "CommunityInfoParent": (181, 383, 753, 511),
    })
    armies = (("USA", "AmericaSuperWeaponGeneral", "AmericaLaserGeneral", "AmericaAirForceGeneral"),
              ("China", "ChinaTankGeneral", "ChinaInfantryGeneral", "ChinaNukeGeneral"),
              ("GLA", "GLAToxinGeneral", "GLADemolitionGeneral", "GLAStealthGeneral"))
    for row, names in enumerate(armies):
        for column, name in enumerate(names):
            left, top = 187 + 140 * column, 387 + 42 * row
            rects[p + "Percent" + name] = (left, top, left + 140, top + 36)
    return rects


def _communicator_rects():
    """The communicator: a compact window under the ruler user section, on the lobby grid of 20 unit rows, 4 unit gaps and a
    6 unit padding. A header holds the title and the Friends, Requests and Blocked tabs. The search field and the friends
    list sit left of the conversation, the requests and blocked lists span the whole body. The conversation header holds
    the dot, name, status and buttons of the open player, where Add friend and Invite share one place."""
    p = "WOLBuddyOverlay.wnd:"
    body = (331, 68, 759, 356)
    return {
        p + "BuddyMenuParent": (331, 36, 759, 356),
        p + "StaticTextTitle": (337, 42, 421, 62),
        p + "RadioButtonBuddies": (425, 42, 485, 62),
        p + "RadioButtonRequests": (489, 42, 569, 62),
        p + "RadioButtonIgnore": (573, 42, 633, 62),
        p + "ButtonHide": (733, 42, 753, 62),
        p + "Line": (331, 67, 759, 68),
        p + "BuddiesParent": body,
        p + "TextEntryFind": (337, 74, 497, 94),
        p + "ListboxBuddies": (337, 98, 497, 350),
        p + "LineSide": (503, 68, 504, 356),
        p + "WindowPeerDot": (510, 84, 518, 92),
        p + "StaticTextPeerName": (522, 74, 605, 88),
        p + "StaticTextPeerStatus": (522, 88, 605, 102),
        p + "ButtonAdd": (609, 78, 673, 98),
        p + "ButtonInvite": (609, 78, 673, 98),
        p + "ButtonProfile": (677, 78, 729, 98),
        p + "ButtonMore": (733, 78, 753, 98),
        p + "LinePeer": (504, 107, 759, 108),
        p + "ListboxBuddyChat": (510, 114, 753, 326),
        p + "TextEntryChat": (510, 330, 753, 350),
        p + "IgnoreParent": body,
        p + "ListboxIgnore": (337, 74, 753, 350),
        p + "RequestsParent": body,
        p + "ListboxRequests": (337, 74, 753, 350),
    }


FIXES = {
    # the game speed slider rect reached 10 units below its row; centred on the row, its label and value box follow it
    "Window/Menus/SkirmishGameOptionsMenu.wnd": {
        "SkirmishGameOptionsMenu.wnd:SliderGameSpeed": (175, 335, 265, 362),
    },
    "Window/Menus/GameSpyGameOptionsMenu.wnd": _setup_rects(),
    "Window/Menus/WOLWelcomeMenu.wnd": _welcome_rects(True),
    "Window/Menus/WOLBuddyOverlay.wnd": _communicator_rects(),
    # layout E: the games list runs the full height, the sidebar holds the players or game details panel under their header over the chat under its header, the chat entry and emote sit under the chat
    "Window/Menus/WOLCustomLobby.wnd": {
        "WOLCustomLobby.wnd:ParentGameListLarge": (47, 77, 531, 511),
        "WOLCustomLobby.wnd:ListboxGamesLarge": (47, 97, 531, 511),
        "WOLCustomLobby.wnd:ListboxPlayers": (537, 91, 753, 315),
        "WOLCustomLobby.wnd:ListboxChat": (537, 335, 753, 480),
        "WOLCustomLobby.wnd:TextEntryChat": (537, 486, 717, 511),
        "WOLCustomLobby.wnd:ButtonEmote": (723, 486, 753, 511),
        "WOLCustomLobby.wnd:ButtonRefresh": (47, 49, 67, 69),
        "WOLCustomLobby.wnd:TextEntrySearch": (73, 49, 391, 69),
        "WOLCustomLobby.wnd:ComboBoxLobbyFilter": (395, 49, 531, 69),
        "WOLCustomLobby.wnd:ParentGameDetails": (537, 77, 753, 315),
        "WOLCustomLobby.wnd:StaticTextDetailsName": (545, 93, 749, 109),
        "WOLCustomLobby.wnd:StaticTextDetailsHost": (545, 111, 749, 125),
        "WOLCustomLobby.wnd:WindowDetailsMap": (545, 129, 749, 185),
        "WOLCustomLobby.wnd:StaticTextDetailsMapName": (545, 187, 749, 201),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelPlayers": (545, 207, 637, 221),
        "WOLCustomLobby.wnd:StaticTextDetailsValuePlayers": (641, 207, 749, 221),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelAnticheat": (545, 221, 637, 235),
        "WOLCustomLobby.wnd:StaticTextDetailsValueAnticheat": (655, 221, 749, 249),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelCash": (545, 249, 637, 263),
        "WOLCustomLobby.wnd:StaticTextDetailsValueCash": (641, 249, 749, 263),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelSuperweapons": (545, 263, 637, 277),
        "WOLCustomLobby.wnd:StaticTextDetailsValueSuperweapons": (641, 263, 749, 277),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelStats": (545, 277, 637, 291),
        "WOLCustomLobby.wnd:StaticTextDetailsValueStats": (641, 277, 749, 291),
        "WOLCustomLobby.wnd:StaticTextDetailsLabelObservers": (545, 291, 637, 305),
        "WOLCustomLobby.wnd:StaticTextDetailsValueObservers": (641, 291, 749, 305),
        "WOLCustomLobby.wnd:WindowDetailsShield": (641, 221, 653, 233),
    },
}

# the same overrides for the Generals layouts, which start from the Generals retail layouts: its score screen runs 30 units past the ruler,
# so the chat list shortens and the chat entry and the button row move up inside the frame
GENERALS_FIXES = {
    "Window/Menus/WOLWelcomeMenu.wnd": _welcome_rects(False),
    "Window/Menus/GameSpyGameOptionsMenu.wnd": {k: v for k, v in _setup_rects(False).items() if k.split(":")[-1] not in _GENERALS_ADDED},
    "Window/Menus/ScoreScreen.wnd": {
        "ScoreScreen.wnd:ListboxChatWindowScoreScreen": (69, 387, 735, 480),
        "ScoreScreen.wnd:TextEntryChat": (68, 488, 696, 513),
        "ScoreScreen.wnd:ButtonEmote": (700, 488, 736, 512),
        "ScoreScreen.wnd:ButtonSaveReplay": (68, 518, 241, 554),
        "ScoreScreen.wnd:ButtonBuddy": (252, 518, 425, 554),
        "ScoreScreen.wnd:ButtonContinue": (436, 518, 609, 554),
        "ScoreScreen.wnd:ButtonOk": (620, 518, 748, 554),
    },
}

# the communicator buttons the user section in the ruler strip replaces; they stay in the layouts for the code's lookups but never show
HIDDEN_WINDOWS = {
    "Window/Menus/WOLWelcomeMenu.wnd": ("WOLWelcomeMenu.wnd:ButtonBuddies", "WOLWelcomeMenu.wnd:RankLine", "WOLWelcomeMenu.wnd:ProgressBarRank"),
    "Window/Menus/WOLQuickMatchMenu.wnd": ("WOLQuickMatchMenu.wnd:ButtonBuddies",),
    "Window/Menus/GameSpyGameOptionsMenu.wnd": ("GameSpyGameOptionsMenu.wnd:ButtonCommunicator",
                                                *(f"GameSpyGameOptionsMenu.wnd:ButtonAccept{i}" for i in range(8))),
    "Window/Menus/ScoreScreen.wnd": ("ScoreScreen.wnd:ButtonBuddy",),
}

# column headings of the game setup screens, left aligned over their player list column or map preview
HEADINGS = {
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
        # the GO sign-in panel (GenOnline/tools/wnd_sources); WOLLoginMenu.cpp shows the windows of each sign-in state
        "GameSpyLoginProfile.wnd:StaticTextSignInStatus",
        "GameSpyLoginProfile.wnd:ButtonUseAnotherAccount",
        "GameSpyLoginProfile.wnd:ButtonCancel",
    ),
    # retail only pushes the quick login; GO keeps it blank with its backdrop, without the W3DMainMenuDraw strip
    "Window/Menus/GameSpyLoginQuick.wnd": ("GameSpyLoginQuick.wnd:WOLLoginMenuParent",),
}

# sort headers of the online game list; the columns follow the COLUMN_ enum in Core LobbyUtils.cpp
LIST_HEADERS = {
    "Window/Menus/WOLCustomLobby.wnd": {
        "WOLCustomLobby.wnd:ListboxGamesLarge": {
            # one continuous kit header strip on the list top: every cell draws the same fill and bottom line, labelled cells after the first add a faint divider
            "cells": (
                "WOLCustomLobby.wnd:StaticTextHeaderGame",
                "WOLCustomLobby.wnd:ButtonHeaderMap",
                None,  # ladder
                "WOLCustomLobby.wnd:ButtonHeaderPlayers",
                "WOLCustomLobby.wnd:StaticTextHeaderLock",  # one empty cell over the lock, observer, ranked and anti-cheat icons
                None,  # observers
                None,  # ranked
                None,  # anti-cheat
                "WOLCustomLobby.wnd:ButtonHeaderPing",
            ),
            "corner": "WOLCustomLobby.wnd:StaticTextHeaderCorner",
            "arrow": "WOLCustomLobby.wnd:WindowHeaderArrow",
        },
    },
}
