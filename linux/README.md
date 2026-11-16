<p align="center">
  <img src="packaging/veyamux.svg" width="96" alt="VeyAmux logo">
</p>

<h1 align="center">VeyAmux</h1>

<p align="center">
  A native <b>Linux</b> video player and capture-card viewer with GPU enhancement and Lossless Scaling frame generation,
  shipped as a single <b>AppImage</b>.<br>
  Zero-copy GPU path, <b>under 1 ms of CPU work per frame</b> at 4K; the menu shows the delay VeyAmux adds, measured live ·
  GPL-3.0 ·
  Linux port of <a href="https://github.com/Likely7/Veyra-NRVideo">Veyra-NRVideo 2.1.5</a> (Windows)
</p>

---

## What is VeyAmux?

VeyAmux is a Linux program, distributed as an AppImage that runs on most distributions without installation. It shows
the picture from a capture card (a console or another PC) or plays a video file, and enhances it on the graphics card:
upscaling, noise reduction, sharpening and colour. The frame rate can be doubled with **Lossless Scaling** frame
generation.

It is built for **low latency**. The capture card's frame is uploaded once and converted, enhanced and displayed
entirely on the GPU, inside the UI's own Vulkan frame: no copy back to the CPU, no extra queue, and only the newest
frame is ever shown.

What that means in numbers, measured on an AMD RX 9070 XT with a 3840×2160 NV12 capture, noise reduction (both) and
FSR 1: **under 1 ms of CPU work** per frame and **about 7 ms of GPU work**. The menu shows one figure, *Delay in VeyAmux*:
the time from the capture driver's timestamp until the picture is finished on the GPU. In that setup it reads
**about 29–32 ms**; most of it is the picture waiting for the next display frame, not processing. Lighter settings
(no noise reduction, no upscaling) lower it. The capture card, USB transfer, Lossless Scaling and the display add their
own latency on top, as with any capture setup.

VeyAmux is a standalone Linux version (Qt 6 / QML, FFmpeg, Vulkan) based on Veyra-NRVideo by Likely7. Veyra's original
HLSL shaders are reused, compiled to SPIR-V. NVIDIA- and Windows-specific parts (DLSS, NVOF, Reflex, AMD HIP-NR, etc.)
are not included.

Developed and tested on **Bazzite** with an **AMD RX 9070 XT** and an **AVerMedia Live Gamer Ultra 2.1** (4K60 NV12).

## Does it need Lossless Scaling?

**No — only frame generation does.**

| Feature | Without Lossless Scaling | With Lossless Scaling + lsfg-vk |
|---|:---:|:---:|
| Capture card and video playback | ✅ | ✅ |
| Upscaling (Lanczos / AMD FSR 1 / NVIDIA NIS / AMD FSR 3.1) | ✅ | ✅ |
| Noise reduction, sharpening, FXAA, colour | ✅ | ✅ |
| Repeated-frame removal (30 fps games in a 60 Hz signal) | ✅ | ✅ |
| OBS browser source | ✅ | ✅ |
| HDR capture (P010 → SDR), HDR video files, HDR output (experimental) and VRR mode | ✅ | ✅ (VRR only with frame generation off) |
| Instant replay, recording and screenshots | ✅ | ✅ (recorded before Lossless Scaling, so at the game's frame rate) |
| Profiles per source, audio delay | ✅ | ✅ |
| **Frame generation 2× / 3×** | ❌ | ✅ |

Frame generation (*Fixed 2×*) is selected by default. If Lossless Scaling or lsfg-vk is missing, everything else runs
normally and the menu tells you what is missing. Frame generation can also be turned *Off*.

> Lossless Scaling is a paid application on Steam (by THS). lsfg-vk (CC BY-NC-ND 4.0) is a separate open-source project
> that runs Lossless Scaling's frame generation on Linux. Neither is included with VeyAmux — VeyAmux finds and uses
> **your own** installation.

## Features

**Sources**
- **Capture cards** via V4L2 (YUYV, UYVY, NV12, P010, I420, RGB, MJPEG) with a choice of resolution, frame rate and audio
  source. Zero-copy GPU path (see above). If the card drops out, or stalls when the console switches resolution,
  refresh rate or HDR, VeyAmux reconnects by itself.
- **A window or a whole screen** (Wayland: KDE Plasma, GNOME, ...): pick *Window or screen …* in the capture dialog and
  your desktop asks what to share. The program's picture then gets the same upscaling, frame generation, recording and
  streaming as a capture card. Uses the desktop's screen-share portal and PipeWire (the same route as OBS); the choice
  is remembered for `--capture last`.
- **Video files** via FFmpeg with VAAPI hardware decoding (AMD/Intel), audio through PipeWire/PulseAudio.
- **HDR capture (P010)** and **HDR video files** (HDR10/PQ and HLG): BT.2020 HDR is tone-mapped properly to your SDR
  display (BT.2390, using Veyra's own HDR functions) instead of looking washed out.
- **HDR output (experimental)**: on a desktop with HDR turned on (KDE Plasma 6, GNOME 48 or newer), VeyAmux can draw
  into an HDR swapchain (HDR10 or scRGB), so HDR sources keep their highlights up to the display's peak brightness and
  their full BT.2020 colours. Turn it on under *HDR output* in the menu and restart. Recordings, screenshots and OBS
  still get normal SDR.
- **gamescope start** (experimental, only with `VEYAMUX_GAMESCOPE=1`): runs VeyAmux fullscreen inside gamescope. On some
  systems gamescope's WSI layer rejects VeyAmux's window; HDR and VRR also work directly on Wayland.
- **Start on plug-in**: VeyAmux can open by itself, fullscreen, when the capture card is plugged in.
- **Frame-time overlay** (F3): a graph of the last two seconds with fps, 1%-low, jitter and the delay in VeyAmux.
- **HDR display brightness** is read from the display's EDID automatically (*Auto*).
- **Steam / Game Mode**: *Add to Steam* puts VeyAmux in your Steam library as a non-Steam game that starts fullscreen on
  the last capture card, also from Big Picture and Bazzite's Game Mode.
- **Sound output and levelling**: pick the output device (headset, speakers) and turn on *Even out loudness* for a
  steadier volume.
- **Low-latency audio**: the capture card's sound is passed through in 5 ms blocks (`VEYAMUX_AUDIO_MS` changes it).
- **Audio delay** (−300 … +300 ms) to line up the sound with the processed picture, and **profiles**: save the picture
  settings under a name, switch with keys 1–9, and bind a profile to a capture signal so it is applied automatically.
- **VRR mode**: shows the game's own frames at an even pace (e.g. 45 fps) so a VRR display can follow the game.
- **Free frame generation**: without Lossless Scaling, *AMD FSR 3.1 2× (free)* doubles the frame rate inside VeyAmux
  (open source, any GPU, live capture). The motion is estimated from the pictures, so the quality is below Lossless
  Scaling, and the real frame is shown half a frame later.
- **120 Hz signals**: when the capture runs at about 120 fps, frame generation pauses automatically (2× would ask for
  240 fps and only add latency) and resumes when it drops below 80 fps.
- The info line shows what the game **actually** runs at, not just the signal, e.g.
  `≈1920×1080 @ 30 fps → 60 fps (Lossless Scaling 2×) · signal 3840×2160 NV12 @ 60.00 fps`.

**Picture enhancement**
- **Upscaling** 1.5× / 2× / 3× with Lanczos-3, **AMD FSR 1** (EASU edge-adaptive upscaling + RCAS sharpening, frame by
  frame, no added latency), **NVIDIA Image Scaling** (NIS: 6-tap directional scaling + adaptive sharpening, runs on any
  GPU, frame by frame) or **AMD FSR 3.1** (FidelityFX SDK 1.1.4, fed by VeyAmux's own motion estimation, since video
  has no motion vectors).
- **Noise reduction**: *Spatial* (non-local means), *Temporal* (motion-compensated average over several frames) or
  *Both*, in three strengths. Removes grain, dithering and compression noise, so *Sharpness* can be raised without
  sharpening the noise.
- **Sharpness** (contrast-adaptive), **FXAA** (Veyra's original shaders), exposure, contrast and saturation.
- **Retro TV**: makes the picture look like an old CRT television, with a curved screen, rounded corners, scanlines and an
  aperture-grille phosphor mask. This is CRT-Geom as in ShaderGlass' `crt-geom` preset (cgwg, Themaister and DOLLS,
  GPL-2.0+). Choose 240 or 480 scanlines, or the source's own. It replaces the upscaling method.

**Frame generation (Lossless Scaling)**
- *Fixed 2×* (default) and *Fixed 3×* through lsfg-vk, for VeyAmux's own window only. VeyAmux finds Lossless Scaling
  in all Steam libraries (Flatpak Steam included) and writes its own lsfg-vk profile; the rest of your lsfg-vk setup is
  left untouched.
- **Performance mode** (lighter model) and an automatic *flow scale* based on the screen resolution.
- **Repeated frames are dropped**: a 30 fps game in a 60 Hz signal gives 30 distinct frames, which Lossless Scaling
  then turns into smooth 60 fps instead of 60 fps with duplicated frames.
- Status and the lsfg-vk log are shown in the menu. If VeyAmux crashes during start-up with lsfg-vk, it is turned off
  automatically until you press *Try again*.

**Instant replay, recording and screenshots**
- **Instant replay** keeps the last 30/60/120 seconds in memory; **F9** saves them as an MP4. **F10** starts/stops a
  recording, **F12** saves a PNG screenshot. Files go to `~/Videos/VeyAmux` and `~/Pictures/VeyAmux`.
- What is recorded is VeyAmux's finished picture (after upscaling, noise reduction and colour), with the sound, at
  720p/1080p/1440p or the original size. Encoding runs on the graphics card through **VA-API** (HEVC, or H.264), with
  libx264 as a fallback up to 1440p; the picture is converted to NV12 on the GPU, so the display path is not slowed down.

**Live streaming**
- **Go live** straight to Twitch, YouTube or your own RTMP server: VeyAmux's finished picture and sound, H.264 encoded
  on the GPU at 3500/6000/8000 kbps, without OBS. Recordings and the replay keep running at the same time.

**Streaming and UI**
- **OBS browser source**: VeyAmux serves the finished picture (after all enhancements) as MJPEG on `127.0.0.1:47920`.
  Save an HTML file wherever you like and add it in OBS as *Browser → Local file*.
- Menu at half the screen width with every setting visible, no scrolling. English and Danish (chosen automatically).

## Installation

### 1. VeyAmux (AppImage)

Download `VeyAmux-x86_64.AppImage` from **Releases** and make it executable:

```sh
chmod +x VeyAmux-x86_64.AppImage
./VeyAmux-x86_64.AppImage
```

Or start straight on the capture card: `./VeyAmux-x86_64.AppImage --capture /dev/video0`.

**Updates:** the AppImage knows where its updates come from (GitHub Releases). In **Gear Lever** press *Check for
updates*; with **AppImageUpdate** run `appimageupdatetool VeyAmux-x86_64.AppImage`. Only the changed parts are
downloaded (zsync).

The AppImage contains everything it needs (Qt, FFmpeg, shaders); nothing is installed on the system. A Vulkan driver is
required (Mesa RADV ships with Bazzite, Fedora, SteamOS and most distributions). Without FUSE:
`APPIMAGE_EXTRACT_AND_RUN=1 ./VeyAmux-x86_64.AppImage`.

### 2. Lossless Scaling (frame generation only)

1. Buy and install **[Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/)** on Steam.
   It does not need to be started.
2. Select the **lsfg-vk** branch: Steam → *Library* → right-click *Lossless Scaling* → *Properties* → *Betas* tab →
   choose **lsfg-vk** in the list (not "beta" or "None"). Let Steam update. (lsfg-vk 2 uses the file `lsfg-vk.dll`
   from this branch; the default branch only has `Lossless.dll`, and VeyAmux will tell you so.)

### 3. lsfg-vk 2 (frame generation only)

The official guide is at **[lsfg-vk.dev](https://lsfg-vk.dev/docs/installation/)**. In short:

**Bazzite, Fedora Atomic and other distributions without a package** — install into your home folder (the system is
not touched):

1. Download the latest build (`.tar.xz`) from **[builds.lsfg-vk.dev](https://builds.lsfg-vk.dev)**.
2. Extract it into `~/.local`:
   ```sh
   tar -xvf lsfg-vk-*.tar.xz -C ~/.local
   ```
3. Check the installation (finds missing files and leftovers from lsfg-vk 1.x):
   ```sh
   ~/.local/bin/lsfg-vk-cli healthcheck
   ```

**Arch Linux** (AUR, not maintained by the lsfg-vk author):
```sh
yay -S lsfg-vk        # or lsfg-vk-bin / lsfg-vk-git
```

**SteamOS**: same as Bazzite (tarball into `~/.local`).

lsfg-vk's own UI (`lsfg-vk-ui`) and profiles are **not** needed for VeyAmux — VeyAmux writes its own profile
(`~/.config/VeyAmux/lsfg-vk.toml`) and enables lsfg-vk for its own window. If you have an older lsfg-vk 1.x, remove it
first (VeyAmux requires 2.0 or newer).

### 4. Turn on frame generation in VeyAmux

1. Start VeyAmux and press **P** (or the ✨ button) to open the menu.
2. Under *Frame generation*, choose **Fixed 2× (doubles)** (the default). If the layer has to be loaded first, the menu
   shows *Restart now* — press it.
3. The status box should say **"Active through lsfg-vk"** and show a line from the lsfg-vk log, e.g.
   `lsfg-vk is loaded: [INFO]: Using profile with name 'VeyAmux'`.

**Important — refresh rate:** lsfg-vk 2 has no frame pacing, so motion is only smooth when the display's refresh rate
matches the result. 60 fps × 2 = 120 fps → set the display to **120 Hz** (or 240 Hz) and turn VRR/adaptive sync off for
VeyAmux. A 30 fps game × 2 = 60 fps fits 60/120 Hz.

## Troubleshooting

| Problem | Solution |
|---|---|
| The menu says Lossless Scaling was not found | Install it through Steam (step 2). If it is in an unusual place, use *Choose folder…* in the menu. |
| "Only Lossless.dll was found" | Select the **lsfg-vk** branch in Steam (step 2). |
| "lsfg-vk is not installed" / older lsfg-vk (1.x) | Install lsfg-vk 2 (step 3) and run `lsfg-vk-cli healthcheck`. |
| Frame generation turned off after a crash | Press *Try again* in the menu. Start without lsfg-vk: `VEYAMUX_LSFG=0 ./VeyAmux-x86_64.AppImage` |
| Motion does not look smoother | The display's refresh rate must match (see above). Log: `~/.config/VeyAmux/lsfg-vk.log` |
| "VIDIOC_S_FMT: Device or resource busy" | Another program (or a hung VeyAmux) is using the capture card. VeyAmux retries for 5 seconds and shows which program holds the card. |
| HDR games look washed out | Choose a **P010 (HDR)** mode in the capture dialog (C) and set *HDR capture: source peak brightness* to match the game's HDR calibration. |
| Stutter in games below 60 fps without Lossless Scaling | *Frame generation → Off → VRR* (restart), turn on *Adaptive sync* in the KDE display settings, run VeyAmux fullscreen. |
| Recording says "no usable video encoder" | VA-API needs the Mesa VA driver: `vainfo` should list HEVC/H.264 *EncSlice*. On Bazzite/Fedora it is `mesa-va-drivers` (included in Bazzite). |
| HDR output says "no HDR swapchain" | Turn on HDR for the display in the system settings (KDE: *Display & Monitor → High Dynamic Range*) and restart VeyAmux. |
| Lossless Scaling's generated frames are missing in OBS | The browser source gets VeyAmux's picture *before* lsfg-vk. Use OBS's *Screen Capture (PipeWire)* of the VeyAmux window instead. |

## Keyboard shortcuts

Space play/pause · ←/→ ±5 s (Shift ±30 s) · ↑/↓ volume · **F** fullscreen · **E** enhancement on/off ·
**P** menu · **M** mute · **O** open file · **C** capture card · **F3** frame-time overlay · **1–9** profiles · **F9** save replay ·
**F10** record · **F12** screenshot

## What was done in the Linux port

Besides porting Veyra-NRVideo from Windows (D3D12/C#) to Linux (Vulkan/Qt 6/FFmpeg) as a self-contained AppImage:

- **Low-latency capture path**: V4L2 → Vulkan directly inside Qt's frame (NV12/YUYV/UYVY/P010 conversion on the GPU),
  under 1 ms of CPU work per frame at 4K; retries, shows which program holds the card when it is "busy", and
  reconnects when the card drops out or stalls on a signal change.
- **Content measurement**: the real frame rate (new frames per second, with noise filtering) and an estimate of the
  game's actual rendering resolution (FFT analysis of the mirrored spectrum that upscaling leaves behind).
- **Repeated-frame removal** before processing, so frame generation interpolates between distinct frames.
- **AMD FSR 3.1 upscaling** on Linux (FidelityFX SDK 1.1.4, shader generation rebuilt for Linux) with its own
  sub-pixel motion estimation (Lucas-Kanade pyramid). Measured on video it is softer than Lanczos, which is therefore
  the default.
- **Noise reduction** (non-local means and motion-compensated temporal), measured on synthetic noise: from 31 dB up to
  37–41 dB without losing fine detail.
- **HDR capture (P010)** and HDR video files with BT.2390 tone mapping to SDR, experimental **HDR output** (HDR10 or
  scRGB swapchain, BT.2020 colour), and a **VRR mode** that paces the game's own frames evenly.
- **gamescope start** and **Add to Steam** (non-Steam game shortcut written to `shortcuts.vdf`).
- **AMD FSR 1** upscaling (EASU + RCAS from the FidelityFX SDK headers).
- **NVIDIA Image Scaling (NIS)** upscaling (NIS SDK, MIT, linear-HDR mode).
- **GPU timing**: Vulkan timestamp queries per stage, shown live in the menu.
- **Instant replay, recording and screenshots** with VA-API hardware encoding (NV12 conversion on the GPU).
- **Profiles** per source with automatic switching, and an **audio delay** setting.
- **Automatic updates**: the AppImage carries update information for GitHub Releases (Gear Lever / AppImageUpdate).
- **Lossless Scaling integration** through lsfg-vk 2: detection in all Steam libraries, own profile, 2×/3×,
  performance mode, flow scale from resolution, log in the menu, crash protection.
- **OBS browser source** (local MJPEG server with GPU downscaling).
- Renamed to **VeyAmux** with a new logo, Danish translation, menu without scrolling.

Tried and removed again: AMD FSR 3.1 *frame generation* (fine detail flickered while panning), RIFE (neural frame
generation — too slow in practice), an adaptive Lossless Scaling multiplier, and a RIFE + Lossless Scaling combination.

Not possible on Linux: FSR 4 (closed Windows DLLs only), DLSS and NVIDIA-specific features, and Veyra's "AMD NR" (runs
NVIDIA's DLSS network on AMD through Windows HIP with weights taken from NVIDIA's DLL).

Technical documentation, measurements and environment variables (in Danish): [docs/TEKNISK.md](docs/TEKNISK.md).

## Building from source

```sh
sudo apt install cmake ninja-build g++ pkg-config patchelf file python3 glslang-tools qt6-base-dev qt6-declarative-dev \
  qt6-svg-dev qt6-wayland libvulkan-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libpulse-dev libva-dev qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts \
  qt6-shader-baker zsync
./scripts/build-appimage.sh          # -> dist/VeyAmux-x86_64.AppImage
```

`scripts/fetch-tools.sh` downloads DXC (HLSL → SPIR-V), linuxdeploy and appimagetool automatically (Ubuntu 24.04
package names).

## License and credits

- VeyAmux: **GPL-3.0**, like the original [Veyra-NRVideo](https://github.com/Likely7/Veyra-NRVideo) by Likely7, which
  VeyAmux is built on (shaders and processing design).
- AMD FidelityFX SDK 1.1.4 (MIT), Qt 6 and FFmpeg (LGPL), SPIRV-Reflect (Apache-2.0) — see
  [THIRD_PARTY_NOTICES_LINUX.md](THIRD_PARTY_NOTICES_LINUX.md).
- [lsfg-vk](https://lsfg-vk.dev) and [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) are
  **not** part of VeyAmux; no code, shaders or DLLs from them are copied or distributed.
