# Vbot

A macro tool for botters, made by botters. Record a run in practice mode, replay it frame by frame, and render it to video.

## Features

- **Record and replay.** Build one macro across as many attempts as you like using checkpoints, then play it back from frame 0.
- **Accurate macros.** Every macro stores position snapshots for both players, and replay corrects any drift, so long macros stay on track.
- **Video rendering.** Turn a replay into an `.mp4` with the level's song, through the FFmpeg API mod.
- **Hitboxes.** Draws hazards, orbs, blocks and the player's boxes, with colours and options you can change.
- **Cheats.** Noclip, a TPS bypass (240 to 6000) and a speed hack.
- **Frame windows (experimental).** Tests how many frames of leeway each click in a macro has.
- **Custom `.vb` format.** A small binary macro format.
- **Themed menu.** The menu is built with ImGui and comes with six themes and a custom accent colour.

## Quick start

1. Open a level and pause it.
2. Tap the **Vb** button to open the menu (on Windows, press **Tab**).
3. Turn on practice mode, press **Record**, restart the level and play.
4. Press **Record** again to stop and save.
5. Press **Replay**, then restart the level to watch it back.

## Good to know

- Recording only works in practice mode.
- Noclip, a TPS other than 240 and a speed other than 1 count as cheats, so levels finished with them won't count.
- Rendering needs the **FFmpeg API** mod. Install it from Geode's mod list.
- Works on Windows and Android (64-bit), game version 2.2081.

This is a beta. If something breaks, please [open an issue](https://github.com/T1fyOwO/Vbot/issues).

[Source code](https://github.com/T1fyOwO/Vbot) · Licensed under GPL-3.0
