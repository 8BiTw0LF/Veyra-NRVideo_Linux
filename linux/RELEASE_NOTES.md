## VeyAmux-V.1.9.1

### Fix
- **VeyAmux no longer starts in the demo by itself.** The Steam shortcut starts VeyAmux on the last source you used.
  Trying Abyssal Garden once (on a handheld without a capture card, for example) saved the demo as that source, so
  every later start loaded it. The demo is no longer remembered; if it was, VeyAmux shows the start screen instead.
  A restart while the demo runs (after changing frame generation, say) still comes back to the demo.

Everything new in 1.9 (Find the best signal, MJPEG on the graphics card, frame generation up to 240 fps and more) is
in the [1.9 release notes](https://github.com/8BiTw0LF/VeyAmux/releases/tag/VeyAmux-V.1.9).

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
