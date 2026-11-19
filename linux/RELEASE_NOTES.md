## VeyAmux-V.1.4

### SGSR replaces Lanczos
**SGSR** (Qualcomm's Snapdragon Game Super Resolution, open source) is the new default upscaling method, in place of
Lanczos. It sharpens edges while it scales, gives less ringing around text and edges, and adds no delay. It is also used
whenever AMD FSR 3.1 cannot start. If you used Lanczos before, you now get SGSR automatically.

### Abyssal Garden in 720p
The demo on the start screen now has four buttons: **720p** or **1080p**, at **30** or **60 fps**. 720p is ideal for
trying upscaling. Switching between them does not build the underwater world again. From a terminal, use
`--capture demo:60:720`.

### Faster
- **PlayStation Remote Play:** the console's picture is decoded on the graphics card (VA-API) instead of the CPU, which
  helps most on a Steam Deck. The frame-time overlay (F3) shows which one is used. If the graphics card cannot, VeyAmux
  switches to the CPU by itself.
- **Video files:** the pictures go straight to the graphics card, the same way as a capture card's. There is no
  conversion to RGB on the CPU and no reading the picture back. HLG video and video with non-square pixels still use
  the old way.
- **Capture cards:** one copy less per picture, which saves about 2 ms of CPU time for each 4K picture. VeyAmux now asks
  the card for 6 buffers.
- **Less work for every picture:** picture buffers are reused instead of allocated anew, and the sound spectrum is only
  updated while the menu is open.

### Fixes
- **Lossless Scaling:** setting frame generation to **Off** (or switching to FSR) did not turn Lossless Scaling off,
  because lsfg-vk 2 cannot be switched off while it runs. Now VeyAmux restarts by itself without lsfg-vk, on the same
  capture card or video file and in fullscreen if you were. Switching between 2× and 3× still happens at once.
- **Lossless Scaling at 120 Hz:** lsfg-vk cannot pause, so VeyAmux now says so once instead of claiming that frame
  generation is paused. AMD FSR frame generation still pauses by itself.

### If something does not work
Each new path can be turned off when you start VeyAmux from a terminal:
- `VEYAMUX_CAPTURE_COPY=1`: capture cards copy as before.
- `VEYAMUX_PS_SWDECODE=1`: Remote Play is decoded on the CPU.
- `VEYAMUX_FILE_GPU=0`: video files use the old way.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. It can update itself through the
`.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
