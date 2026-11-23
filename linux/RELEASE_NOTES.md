## VeyAmux-V.1.8

### Smoother frame pacing, less delay
- **Even out frame pacing** now holds a picture only as long as it has to. Before, every picture waited one extra
  capture refresh, even when there was nothing to even out. And with MJPEG at 60 fps, decoding took longer than that
  refresh, so the pictures came out uneven again (17/50 ms instead of 33/33). Now the hold follows the slowest recent
  picture. The rhythm is even, and a signal that is already even gets almost no extra delay. F3 shows the hold in ms.
- **A new picture is drawn at once.** Before, Qt waited for the compositor's next frame on Wayland, which added a little
  jitter to every picture.
- **Steam's gamescope session (SteamOS, Big Picture):** frame generation was reported to stop a few seconds after the
  mouse stopped moving. While frame generation runs there, VeyAmux now keeps the pointer alive. The pointer stays
  invisible and the controls stay hidden.

### Capture list
- The best modes come first: the largest uncompressed mode (on a 4K card: **4K NV12 30**, the default), then
  **1080p NV12 30** (a quarter of the USB traffic, so much less delay), then **4K MJPEG 60** for 60 fps games.
- **Favourite signals:** click ☆ on up to three modes. They come first, and the one used last is chosen automatically.

### Fixes from a code review
A code review ([MarkStokes-outlook/VeyAmux](https://github.com/MarkStokes-outlook/VeyAmux/issues)) found real
problems. What was fixed and why: [docs/REVIEW-FIXES.md](docs/REVIEW-FIXES.md). The most important ones:
- **Capture cards:** a picture read straight from the card's buffer was handed back to the card at once, so the card
  could write a new picture into it while VeyAmux was still copying it. This could cause torn or out-of-order frames.
- Recording and streaming no longer change resolution, or stop, when the window is resized. A recording that could not
  be saved completely now says so.
- Resizing the window no longer stalls the graphics card or resets noise reduction and anti-flicker.
- The OBS browser source can no longer be read by web pages.
- The settings file with the stream key and the Remote Play keys is readable by your user only.
- Updates are checked harder: the update must come from GitHub, have the right size and be a real AppImage.
- The build tools are fixed versions checked by SHA-256.

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
