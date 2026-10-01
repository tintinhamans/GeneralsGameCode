# Control bar art

The sheets in this folder are the art of Control Bar Pro, from TheSuperHackers' GeneralsControlBar:

- Source: https://github.com/TheSuperHackers/GeneralsControlBar
- Commit: 05299349d0722eebf04a0b31d3702cb28f219dc2
- Licence: MIT, Copyright (c) 2021 xezon (the full text is in `LICENSE` next to this file)

Control Bar Pro's art is derived from the original art of Command & Conquer Generals: Zero Hour by
Electronic Arts.

The sheets were exported at full resolution from the Photoshop masters in
`ControlBarProZH/GameFilesEdited/Art/Textures`, using each master's merged image and alpha channel exactly
as its .tga ships. The pieces were then cut at the rects in Pro's MappedImages INI and ControlBarScheme.ini
and packed without any redrawing:

| Sheet | Contents | Source |
| --- | --- | --- |
| `cb_usa.png`, `cb_china.png`, `cb_gla.png` | radar, production and selection panels; side, general and min/max buttons; logo, experience bar, power trays, alarm light | `<Side>CommandBarPro_4096_1024`, `<Side>PowersPro_2048_2048` |
| `cb_usa_power.png`, `cb_china_power.png`, `cb_gla_power.png` | the general's promotions window | `<Side>PowersPro_2048_2048` |
| `cb_neutral.png` | the observer bar: radar, player window, selection panel and buttons | `ObsCommandBarPro_4096_1024`, `ObsPlayerWindowPro_1024_512`, `ControlButtonsPro_512_512`, `AmericaPowersPro_2048_2048` |
| `cb_common.png` | help box and the promotions window's Close button | `HelpboxPro_1024_256`, `ControlButtonsPro_512_512` |

The sheets are drawn at 2 px per dp (`resolution: 2x` in `ControlBar.rcss`), the size Pro's art has on a
3840x2160 screen.
