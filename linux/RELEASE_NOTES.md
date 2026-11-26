## VeyAmux-V.2.2.1

A small fix release.

### Fixes
- When the menu closes, a button or stick you are already holding reaches the game or Remote Play at once (before,
  only after the next movement).
- The emulator process uses less CPU: the controller is still read every half millisecond, but game info is only
  updated every 20 ms. Less power use, most noticeable on handhelds.
- A controller stick could be missed right after a game started.
- No busy loop in the sound thread while no game is running.

Everything new in 2.2 (N64, Dreamcast, PSP and DS, RetroAchievements, run-ahead and rewind, search, skip intro and
more) is in the [2.2 release notes](https://github.com/8BiTw0LF/VeyAmux/releases/tag/VeyAmux-V.2.2).

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

VeyAmux includes no emulators, BIOS files or games: the emulators are your RetroArch's or libretro's own builds, under
their own licences. Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and
lsfg-vk. FSR 4 is AMD's (MIT); the add-on is an unofficial build and not from AMD.
