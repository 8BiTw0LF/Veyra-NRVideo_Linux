## VeyAmux-V.1.9

### Find the best signal
A capture card offers many modes, and which one works best depends on the whole PC: the USB controller, the graphics
card (AMD, Intel or NVIDIA) and the CPU. Press **C**, then *Find the best signal for this PC…*. VeyAmux plays each
sensible mode for about 7 seconds with your own picture settings and measures what reaches your PC:
- the frame rate against what the mode promises, how evenly the pictures are shown, pauses and broken pictures;
- the delay inside VeyAmux, CPU and GPU;
- for MJPEG: decoding on the graphics card and on the CPU, and how long each takes.

It then names the **best mode for 30, 60 and 120 fps games** (and 240 on a 240 Hz screen), the one with the **least
delay**, the **sharpest**, and **recommends** the one for the game that ran during the test. Each row has **Use** and ☆
(favourite). Keep the game running and moving while it measures; it takes about a minute.

### MJPEG on the graphics card
MJPEG is decoded on the graphics card where it can be (VA-API on AMD and Intel, NVDEC on NVIDIA) and on the CPU
otherwise, and goes to the GPU as NV12, a third of the data it used to be. F3 and the performance report show where it
is decoded, how long it takes and how many broken pictures came in. On some cards fetching the decoded picture back
costs as much as the decoding saves; the test above shows which is faster on yours.

### Frame generation up to 240 fps
The pause for fast games (frame generation stopped at about 120 fps) now follows the screen. On a 240 Hz screen a
120 fps signal is doubled to 240 fps. On a 120 Hz screen it pauses as before, since the extra frames could not be shown
and would only add delay. With Lossless Scaling 3× the limit is counted for 3×.

### Fixes
- **Switching signals:** in 1.8 the picture could freeze when you changed mode, or when the card reconnected after the
  console changed resolution. A capture buffer still held by the renderer kept the old stream open, so the card
  refused the new one as busy. Buffers now go back as soon as the picture is on the graphics card.
- **FSR 3.1 is faster:** it reuses the motion estimate that temporal noise reduction and anti-flicker already make,
  instead of computing its own (about 3 ms less GPU time per 4K picture with all three on).
- F3's frame times count only new pictures, not redraws of the same one.

### VeyAmux Debug
**[VeyAmux Debug](https://github.com/8BiTw0LF/VeyAmux/releases/tag/debug-build)** is a new test build made after every
change. It runs next to VeyAmux with its own settings, writes a full bug report (machine, driver, displays, capture
card, settings with secrets hidden, log and crashes) and has a benchmark of what every setting costs on your machine.

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
