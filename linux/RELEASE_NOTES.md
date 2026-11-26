## VeyAmux-V.2.2

### New
- **Nintendo 64, Dreamcast, PSP and Nintendo DS** on the Games page. VeyAmux gives the OpenGL emulators (N64,
  Dreamcast, PSP) an OpenGL context of their own, so their picture gets upscaling, Retro TV, frame generation and
  recording like every other game. PPSSPP's own files are fetched the first time a PSP game starts.
- **RetroAchievements:** sign in once on the Games page; achievements are checked in every game RetroAchievements
  knows and unlocks show on screen (softcore: save states, rewind and run-ahead stay on).
- **Run-ahead** (less input delay), **rewind** (hold L2 on consoles without it, or Backspace) and **Continue** where
  you quit a game.
- **Up to 4 players**, one per controller.
- **Per console:** run-ahead, rewind, the emulator, the controller layout and a picture profile (e.g. Retro TV only
  for the SNES).
- **Emulators run in a process of their own:** a crashing emulator ends the game, not VeyAmux.
- **Search** in Games and on Jellyfin / Plex.
- **Media:** skip intro and credits, the next episode by itself, quality (the server converts to 20 / 8 / 4 Mbit/s),
  seek bar pictures, subtitle size and 5.1 sound.
- **Start screen:** *Continue playing* with your last games; *System → Start in* opens VeyAmux on Games or Media.
- New Steam artwork that lists what VeyAmux does.

### Fixes
- Large Plex libraries and the Arcade system (hundreds of games) no longer freeze VeyAmux, the mouse or the controller.
- No more double presses in NES games from Steam Input's copy of a controller.
- BIOS files of Dreamcast / NAOMI, the 64DD and arcade sets are kept out of the game lists.
- The controller's highlight stays on its card while the page scrolls.
- The demo (and any other source) can be closed with the controller from the menu.
- A taskbar pin of VeyAmux no longer opens VeyAmux Debug.

Everything new in 2.1 (Media with Jellyfin and Plex, subtitles and sound tracks, and the Games page) is in the
[2.1 release notes](https://github.com/8BiTw0LF/VeyAmux/releases/tag/VeyAmux-V.2.1).

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

VeyAmux includes no emulators, BIOS files or games: the emulators are your RetroArch's or libretro's own builds, under
their own licences. Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and
lsfg-vk. FSR 4 is AMD's (MIT); the add-on is an unofficial build and not from AMD.
