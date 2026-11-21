## VeyAmux-V.1.6

### New start screen and menu
- The start screen shows the sources as three large cards (capture card, PlayStation, video file), a button to
  continue with the last capture card, the demo, and the themes and languages.
- The menu has six tabs: Picture, Frame generation, Display, Sound, Record and System. It grows with the screen, so it
  reads well on a TV, and **LB / RB** switch tabs on a controller. On handhelds it still fills the screen.

### Updates from inside VeyAmux
VeyAmux looks for a new version on GitHub by itself, at most twice a day (it can be turned off under *System*). One
click downloads it, checks it against the release's checksum and puts it in place of the AppImage under the same
name, so a Steam shortcut or Gear Lever entry keeps working. The new version starts the next time, or right away with
*Restart now*. Gear Lever is no longer needed.

### Smoother frame generation
- **Repeated frames are found by comparing the whole picture.** Before, only a sparse grid was compared, and a small
  movement seen from a still camera (an idle animation, a distant figure) could be taken for a repeat and thrown away,
  which frame generation shows as a judder. In a 4K test with card noise and only a 12-pixel element moving, the old
  check was right on 68 of 119 frames, the new one on 119 of 119. It costs about 1 ms per frame.
- **Screen check:** VeyAmux warns when frame generation asks for more frames than the screen can show (for example
  30 fps × 3 on a 60 Hz screen) and says what to change.
- **Capture format:** if you pick compressed MJPEG while the card has an uncompressed mode, the capture window says so
  and offers it. If no capture card is listed, it explains why (no device, or no access to it).

### Picture
- **SMAA** anti-aliasing (medium, high, ultra) next to FXAA: sharper edges with a little more GPU time.
- **Texture, clarity and dehaze** (from Veyra-NRVideo).

### Recording
- **Comparison clips:** the original capture next to VeyAmux's picture, as split, side by side, vertical 9:16 (for
  TikTok, Shorts and Reels) or a sweeping reveal, with labels. Lossless Scaling's frames are made after VeyAmux, so
  they are not in recordings.
- **Clip markers:** **F6** while recording notes the moment in `<recording>-markers.txt`.

### Help
- **Check my setup** (System tab) finds what costs sharpness, smoothness or sound, each with a button that fixes it.
- **Performance report:** copies the graphics card, settings and measurements for a bug report, with nothing personal.

Several of these features come from [PushFPS](https://github.com/DosukaSOL/PushFPS) 4.1 (GPL-3.0-or-later). SMAA is
MIT-licensed (iryoku/smaa).

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. From now on, VeyAmux can update
itself.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
