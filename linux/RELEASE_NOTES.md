## VeyAmux-V.2.0

### AMD FSR 4: AI upscaling on Linux
AMD's machine-learning upscaler (FSR 4.0.2, published by AMD under the MIT licence) now runs in VeyAmux through
standard Vulkan. Choose **FSR 4** under *Picture*. The first time, VeyAmux downloads it as a free add-on (about 10 MB,
built in the separate [VeyAmux-FSR4](https://github.com/8BiTw0LF/VeyAmux-FSR4) project) and checks it against a
checksum it knows. It upscales 1.5×, 2× and 3×, and at *Native* it works as AI anti-aliasing.

- **RDNA 4 (RX 9000):** AMD's own **FP8 model on the matrix units**, through VK_KHR_cooperative_matrix,
  VK_EXT_shader_float8 and VK_EXT_cooperative_matrix_maintenance1 (Mesa 26.3 or newer). On an RX 9070 XT it costs
  about as much as FSR 3.1: 3.8 ms for a 4K picture, 3.4 ms for 1080p → 4K, 2.7 ms for 1.5×.
- **Other GPUs** with Vulkan 1.3 and 8-bit dot products (AMD RDNA 2 and 3, NVIDIA Turing or newer, Intel Arc): AMD's
  **INT8 model**, 1.4–1.6× heavier.
- The hint under the method shows which one runs, and VeyAmux falls back to INT8 by itself if the driver refuses FP8.

### AMD Anti-Lag and NVIDIA Reflex
Under *Display*. They start each frame as late as it can and still be on time, which shortens the queue to the screen.
That queue is only there when the graphics card is nearly full: on an RX 9070 XT with room to spare a picture reached
the screen about 2 ms after VeyAmux finished it, with or without Anti-Lag. It is off by default, and the menu says
when it is worth turning on. (Reflex is not used together with Lossless Scaling.)

### Also new
- **Upscalers:** NIS, SGSR, FSR 3.1, FSR 4, in that order. FSR 1 is gone: it looked like SGSR and cost more. A saved
  FSR 1 becomes SGSR.
- **VSync with Lossless Scaling:** lsfg-vk's extra frames only come out evenly when every present waits for the
  screen, so VSync is on while Lossless Scaling runs (`VEYAMUX_VSYNC=0` keeps it off).

### Fixes
- Changing the capture card's mode (or the benchmark's switch to 1080p and back) could leave the card "in use by
  another program": with *Even out frame pacing* on, a held capture buffer kept the old device open.
- FSR 4 now also works together with FSR 3.1 frame generation (it fell back to SGSR there).
- The Anti-Lag / Reflex switch and the Debug hardware-counter switch no longer stay grey after start.
- A message about an upscaler that could not start now names FSR 4 too.

### VeyAmux Debug
- **GPU hardware counters** (VK_KHR_performance_query) in the report and the benchmark.
- **Measure to the screen:** capture timestamp → picture really on the screen (VK_KHR_present_wait), as the
  benchmark's *to screen* column.
- The benchmark measures FSR 4 with both models, and Anti-Lag / Reflex off and on.

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them. FSR 4 is AMD's (MIT); the add-on is an unofficial build and not from AMD.
