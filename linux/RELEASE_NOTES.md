## VeyAmux 2.1.5-linux21

Everything new since **2.1.5-linux15**, the previous release.

### Upscaling
- **NVIDIA Image Scaling (NIS)** is a new upscaling method. It uses 6-tap directional scaling with adaptive sharpening and runs on any GPU (AMD, Intel and NVIDIA). It also works on HDR signals and adds no latency. You can change the sharpness with `VEYAMUX_NIS_SHARP=0..1` (default 0.4).
- The methods are now Lanczos, FSR 1, FSR 3.1 and NIS.

### Frame generation
- **Free AMD FSR 3.1 frame generation** is available for players who don't own Lossless Scaling. It works on the live capture and doubles the frame rate (60 → 120 fps).
- **120 Hz rule:** when the capture signal runs at about 120 fps, frame generation pauses automatically. It resumes when the signal drops below 80 fps.

### HDR
- **HDR output** can use either HDR10 (PQ, BT.2020, the default) or scRGB. HDR sources keep their BT.2020 colours all the way to the display.
- **Display peak "Auto"** reads your monitor's peak brightness from its EDID. The menu shows the value it found.
- With HDR output on, the menu, notices and overlay are now fully opaque. Before, bright HDR scenes shone through the menu and turned it grey.
- When an HDR source is copied to SDR (OBS, recordings, screenshots), its colours are converted from BT.2020 to BT.709 with Veyra's ToneGamut.

### Streaming, recording and overlay
- **Go live** with RTMP(S) streaming to Twitch, YouTube or your own server, without OBS. It uses H.264 at a constant bit rate (3500, 6000 or 8000 kbps) and is encoded on the GPU through VA-API.
- **Frame-time overlay (F3)** shows a frame-time graph, fps, 1% low, jitter and the delay inside VeyAmux.
- Instant replay (F9), recording (F10) and screenshots (F12) are unchanged from linux15.

### Sound
- You can choose the **sound output device**.
- **Loudness levelling** evens out loud and quiet parts.
- **Lower-latency audio pass-through:** capture audio is passed on in 5 ms blocks instead of 20 ms, with a shorter output queue.

### Capture card
- **Autostart:** an optional small helper starts VeyAmux in fullscreen as soon as the capture card you used last is plugged in (`--watch`).
- **Reconnect:** if the card drops out, VeyAmux reconnects. If the stream stalls after a signal change, it restarts.
- `--capture last` finds the card again even when it gets a new `/dev/videoN`.

### Steam
- **Add to Steam** adds VeyAmux to your Steam library as a non-Steam game. It then starts in fullscreen on the last capture card, also from Big Picture and Game Mode. Close Steam before using it.

### Fixes
- If the UI fails to start with Vulkan, VeyAmux no longer remembers the software fallback. It tries Vulkan again at the next start.
- The HDR peak "Auto" label is no longer cut off.
- **gamescope:** the "start inside gamescope" option is now hidden. On some systems, gamescope's Vulkan layer rejected VeyAmux and could freeze the desktop. HDR and VRR work directly on Wayland (KDE Plasma 6 / GNOME 48 or newer). The option can still be turned on with `VEYAMUX_GAMESCOPE=1`.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. Once installed, the AppImage can update itself through the `.zsync` file (for example with Gear Lever or AppImageUpdate).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not include either of them.
