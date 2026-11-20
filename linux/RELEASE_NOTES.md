## VeyAmux-V.1.5

### Smoother Lossless Scaling
- **Even out frame pacing** (optional, under Frame generation): many games locked to 30 fps show their pictures unevenly,
  for example 17 ms and 50 ms apart instead of 33 and 33. Frame generation then keeps that judder. VeyAmux now moves each
  picture to an even rhythm before frame generation. This costs one refresh of delay, about 17 ms at 60 Hz. The
  frame-time overlay (F3) shows how many pictures were moved. Based on PushFPS's frame pacing repair, by way of
  Veyra-NRVideo 2.2.0 (GPL-3.0-or-later).
- **VRR warning:** lsfg-vk 2 needs a fixed refresh rate to space its frames evenly. If Adaptive Sync (VRR) is on in KDE,
  VeyAmux now shows a warning in the menu and a notice when you choose Lossless Scaling. Set *Adaptive sync* to
  *Never* under *Display & Monitor*.

### Sharp in any window size
The finished picture is now resampled to exactly the pixels it covers in the window, and then drawn 1:1. Before, Qt
stretched it with a simple bilinear filter. It is sharper when the picture is enlarged, and thin lines and text no
longer crawl when it is shrunk (for example 4K in a 1440p window). In fullscreen at the source's own size, nothing
changes. To compare with the old way, start VeyAmux with `VEYAMUX_NO_FIT=1`.

### Lock the HUD (AMD FSR frame generation)
A new option keeps still text, maps and gauges from warping in the generated frames. It takes still, sharp-edged pixels
from the real frame. Based on PushFPS, by way of Veyra-NRVideo 2.2.0.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. It can update itself through the
`.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
