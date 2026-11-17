## VeyAmux-V.1.0

VeyAmux has its own version numbers from now on. This is **1.0**, and every update to GitHub and Flathub goes up one
step: 1.1, 1.2 and so on up to 1.9, then 2.0. This release is 2.1.5-linux21 plus the changes below.

### Game controller on the start screen
- The controller now also steers the **start screen** and the **capture dialog**, not just the menu.
- **D-pad or left stick:** moves between *Open file*, *Capture*, the demo buttons, the themes and the languages.
- **A** chooses and **B** closes the capture dialog.
- **Select + Start** still opens and closes the menu.

### Flatpak
- The Flatpak version now builds and runs. In it, Lossless Scaling frame generation works through the Flathub
  extension `org.freedesktop.Platform.VulkanLayer.lsfgvk`.
- The Flathub release is on its way.

### Everything from 2.1.5-linux21
- **Built-in demo scene** (Abyssal Garden): a live 3D underwater world in 1080p at 30 or 60 fps. You can try every
  feature without a capture card.
- **Game controller in the menu**: the game gets no input while the menu is open.
- **Pixel-perfect scaling** for retro consoles.
- **Four themes** and **eight languages**.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. Once installed, it can update
itself through the `.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
