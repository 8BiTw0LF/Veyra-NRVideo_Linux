## VeyAmux-V.2.1

VeyAmux is no longer only for what comes through a cable: **your films and series** and **your retro games** now
have their own pages on the start screen, with the same look, the controller and every picture feature.

### Media (in place of *Video file*)
- **Your own folders, Jellyfin and Plex** as posters, with *Continue watching*.
  - **Jellyfin:** user name and password, or *Quick Connect*.
  - **Plex:** sign in on plex.tv in your browser; every server of the account is added. At home Plex is free; outside
    your home network Plex wants Plex Pass or a Remote Watch Pass.
- Film and series libraries open on **All, Recently added, Not watched and Categories** (genres).
- **Where you left off** goes back to the server, so *Continue watching* matches the Jellyfin and Plex apps.
- **Subtitles:** the tracks inside the file (also Blu-ray and DVD picture subtitles), files beside the video
  (`Film.da.srt`) and the server's subtitle files.
- **Sound tracks:** language and format (e.g. *English · AC3 5.1*). Both have a button in the bottom bar; the
  languages you pick are remembered.
- Films are played as they are stored and get all of VeyAmux's processing: FSR 4, noise reduction, HDR, frame
  generation.

### Games
- **Pick one folder** and every game is sorted under its system by itself (folder name, file type or the disc's
  header). `.zip`, `.7z` and `.rar` work.
- **No emulator to choose:** your RetroArch's is used, or libretro's own build is fetched the first time. 26 systems:
  NES, Super Nintendo, Game Boy (Color, Advance), Mega Drive, Master System, Game Gear, Mega-CD, 32X, Saturn, PC
  Engine (CD), PlayStation, Atari, Neo Geo Pocket, WonderSwan, arcade and more.
- **Box art** with one button (*Get box art*), from libretro's thumbnails; arcade games get their full titles.
- **Games on several discs** are one game; the menu changes the disc when the game asks.
- **BIOS files** in your games folder are kept out of the list and copied to the BIOS folder.
- Save states, battery saves, controller with rumble, keyboard; the game pauses while the menu is open.

### Also
- A **home button** in the bottom bar goes back to the start screen, or to the page the film or game came from.
- Since 2.0.1: less delay on a console's dashboard and menus.

Everything new in 2.0 (AMD FSR 4 with FP8 on RDNA 4, AMD Anti-Lag / NVIDIA Reflex, and more) is in the
[2.0 release notes](https://github.com/8BiTw0LF/VeyAmux/releases/tag/VeyAmux-V.2.0).

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

VeyAmux includes no emulators, BIOS files or games: the emulators are your RetroArch's or libretro's own builds, under
their own licences. Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and
lsfg-vk. FSR 4 is AMD's (MIT); the add-on is an unofficial build and not from AMD.
