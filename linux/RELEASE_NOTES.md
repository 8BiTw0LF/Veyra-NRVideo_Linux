## VeyAmux 2.1.5-linux17

What's new since **2.1.5-linux16**.

### Retro TV
- **Retro TV** is a new option in the right-hand menu. It makes the picture look like an old CRT television: a slightly curved glass screen with rounded corners, visible scanlines and the TV's red, green and blue phosphor stripes. It is best for retro games.
- It uses **CRT-Geom**, the same shader as ShaderGlass' `crt-geom` preset (by cgwg, Themaister and DOLLS, GPL-2.0+), with ShaderGlass' default settings. VeyAmux runs it on the GPU through Vulkan.
- **Scanlines:** you can choose 240 lines (old consoles), 480 lines (PS2 and GameCube era) or the source's own lines. A capture card sends 240p games scaled up to 1080p, so VeyAmux rebuilds the original lines from the capture.
- While Retro TV is on, it replaces the upscaling method and skips the sharpness setting. Exposure, contrast and saturation still apply. It works whether or not *Enhance* is switched on.

### Window or screen as a source
- **Window or screen …** is a new entry at the bottom of the device list in the capture dialog (C). After *Start*, your desktop asks which window or screen to share. VeyAmux then treats the picture like a capture card, so upscaling, Retro TV, frame generation, recording and streaming all work on it.
- It uses the desktop's own screen-share dialog (xdg-desktop-portal) and PipeWire, the same route OBS uses, so it needs Wayland (KDE Plasma, GNOME, ...). VeyAmux uses your system's PipeWire, which the AppImage does not ship.
- Your choice is remembered, so `--capture last` opens the same window again without asking. If the shared window is closed, VeyAmux says so.
- Audio is off by default, because the shared program plays its own sound.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. Once installed, it can update itself through the `.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not include either of them.
