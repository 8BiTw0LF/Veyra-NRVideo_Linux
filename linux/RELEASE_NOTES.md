## VeyAmux-V.1.2

### PlayStation Remote Play (PS5 and PS4)
Play your PlayStation in VeyAmux over your home network, without a capture card. The picture goes through all of
VeyAmux's processing: upscaling, frame generation, Retro TV, recording and streaming.
- **Start:** press **PlayStation** on the start screen (or *PlayStation Remote Play* in the capture window).
- **Once:** log in to PlayStation Network. VeyAmux opens Sony's login page in your browser. Afterwards, copy the
  address the browser ends on (it contains `code=`) and paste it into VeyAmux.
- **Once per console:** on the PS5 open *Settings → System → Remote Play → Link Device*. On the PS4 open *Settings →
  Remote Play Connection Settings → Add Device*. Then type the 8-digit number the console shows into VeyAmux.
- **Then:** press **Play**. A console in rest mode is woken up first.
- **Picture:** 720p or 1080p, 30 or 60 fps, and HDR on the PS5.
- **Controller:**
  - The controller has to be connected to the computer (the PlayStation does not take input from its own controller
    during Remote Play). On a Steam Deck the built-in controls are used.
  - A DualSense or DualShock 4 touchpad click works, and rumble from the game reaches the controller.
  - **Select + Start** opens the VeyAmux menu.
- **Login passcode:** if the console has a 4-digit login passcode, it can be saved with the console.
- **Delay in the frame-time overlay (F3):**
  - **Shown:** the network (half the round trip to the console), the decoding, VeyAmux's own processing and their
    total, plus the bitrate, the packet loss and any lost frames.
  - **Not measurable:** the console's own encoding is shown as an estimate of about 8 ms.
- Remote Play only works on the local network for now (not over the internet). Microphone, motion controls and touchpad
  swipes are not supported yet.
- Built on the protocol library of [chiaki-ng](https://github.com/streetpea/chiaki-ng) v1.10.1. Because that library
  is AGPL-3.0, the VeyAmux program as distributed is now covered by the AGPL-3.0. VeyAmux's own code stays GPL-3.0.

### Steam Deck and other handhelds
- **Full-window menu:** on the Steam Deck, ROG Ally, Legion Go and other handhelds (and in small windows), the menu fills
  the whole window. It is shown at a readable size and scrolls with a finger, the touchpad or the controller. **✕**
  closes it.
- **Touchscreen:** a tap on the picture now shows the controls with the menu button and no longer pauses the picture.
- **Menu buttons:** on the Deck, Select + Start are the **View** and **Menu** buttons.

### Flatpak removed
VeyAmux is now only released as an AppImage. If you installed the Flatpak, remove it with
`flatpak uninstall --user io.github._8BiTw0LF.VeyAmux` and `flatpak remote-delete --user veyamux`.

### Fixes
- If AMD FSR 3.1 cannot start, the message now says where it failed and what Vulkan answered.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. It can update itself through the
`.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
