## VeyAmux 2.1.5-linux21

What's new since **2.1.5-linux19**.

### Built-in demo: try VeyAmux without a capture card
- **Abyssal Garden** is now part of VeyAmux. It is a live 3D underwater world with kelp forests, light shafts, a
  shipwreck, a stone whale fluke and manta rays. Press **Demo · 30 fps** or **Demo · 60 fps** on the start screen.
- It is rendered in **1920×1080 on your graphics card** and goes through VeyAmux exactly like a console's picture. So
  upscaling, noise reduction, Retro TV, pixel-perfect scaling, frame generation, recording, replay and streaming can
  all be tried on a moving scene. It has its own underwater sound, which also reaches the sound meter, recordings and
  the stream.
- **Play while the menu is closed:**
  - **Keyboard:** **W A S D** to swim, **Q / E** down / up, **Shift** faster, **1–6** for six viewpoints.
  - **Mouse:** drag to look around.
  - **Controller:** sticks to swim and look, triggers for down / up, **Y** for the next viewpoint.
- **The menu still works as normal:** **P** or **Select + Start** opens it, and the swimming stops while it is open.
- The first start builds the world, which takes a few seconds, and then it is kept until you close VeyAmux.
- Abyssal Garden is by Maksymilian Dendura (MIT license) and was originally made for the browser. VeyAmux runs it
  natively, with no browser needed.

### Game controller in the menu
- **Select + Start** opens and closes the menu, or hold **Guide / PS** for half a second.
- While the menu is open, the controller only steers the menu, and **the game gets nothing**:
  - the D-pad or stick moves,
  - **A** chooses and **B** closes,
  - left/right change sliders,
  - **LB / RB** jump between the columns.
- Works with Xbox, PlayStation, Switch and most other controllers, including through Steam Input. The menu shows
  which controllers are connected.

### Picture
- **Pixel-perfect scaling** (under Retro TV) enlarges the picture by a whole number with sharp, square pixels.
- **Game pixels** (Auto, 240p or 480p) first finds a retro game's own pixel grid inside the capture signal, so every
  game pixel becomes an exact, even block.

### Look and language
- **Four themes** on the start screen, and the logo changes with them:
  - **Vice**: violet with a pink-to-orange sunset.
  - **Code**: black with terminal green.
  - **Inferno**: blood red and ember.
  - **Retro**: 8-bit greys with a pixel font.
- **Eight languages**: English, Danish, German, Spanish, French, Swedish, Norwegian and Russian. They are picked from
  the system, or chosen on the start screen.
- In Danish, "Støjfjerning" is now called **Støjreduktion**.

### Other
- Preparations for a **Flathub** release (Flatpak manifest and metadata). In the Flatpak version, lsfg-vk comes as the
  Flathub extension `org.freedesktop.Platform.VulkanLayer.lsfgvk`.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. Once installed, it can update
itself through the `.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
