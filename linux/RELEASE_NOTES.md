## VeyAmux 2.1.5-linux19

What's new since **2.1.5-linux17**.

### Sound
- **All your sound outputs are listed.** *Sound output* now shows every output device: TV, headset, speakers, virtual surround and so on. Before, it often only showed "System default", because the list came from the `pactl` tool, which isn't installed everywhere. VeyAmux now asks the sound server (PipeWire / PulseAudio) directly, and the audio sources in the capture dialog use the same route.
- **Sound from the source** is a new live spectrum meter (50 Hz – 16 kHz) under *Sound output*. It shows whether sound is coming in from the capture card or file at all, and says "Sound is coming in" or "No sound".
- **Mute for you, not for your viewers.** Mute (M) and the volume only affect your own speakers. Recordings, replays and the live stream to Twitch, YouTube or your own server always get the sound. This was already how it worked, and the menu now says so.

### Menu
- **Bigger text and controls.** Text and controls are bigger, and the menu now fills the panel instead of leaving empty space at the sides.
- **New layout.** *Sound output* now sits under anti-aliasing, and *Retro TV* under the sound meter. *Steam* and *OBS* are in the right-hand column, and *Frame generation* is in the middle.
- **Contrast centred.** The contrast slider now runs from 0.6 to 1.4, so the neutral 1.00 sits in the middle like the other sliders.
- **"Capture" button.** The start screen button is now called **Capture**, because it opens windows and screens as well as capture cards.

### Look
- **New logo.** The V is laid over the A so the two letters form an X.

### Fixes since the first linux17 build
- **Sharing a whole screen** no longer makes VeyAmux crash. The current linux17 download already had this fix.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. Once installed, it can update itself through the `.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not include either of them.
