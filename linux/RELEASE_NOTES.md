## VeyAmux-V.1.3

### Native + FSR 3.1 (TAA)
Choose Upscale **Native** and Method **FSR 3.1** to use AMD FSR 3.1.6 as anti-aliasing without upscaling. It smooths
edges and crawling detail at the source resolution.

### Anti-flicker
A new **Anti-flicker** switch under Enhance calms pixels that flicker between frames: thin lines, fences, foliage and
distant detail. Only the flickering pixels are touched, and it adds no delay.

### Add to Steam, all by itself
- **Steam is handled for you:** if Steam is open, VeyAmux closes it, adds the shortcut and starts Steam again. You no
  longer have to close Steam yourself. This also works with the Flatpak Steam.
- **Library artwork:** VeyAmux gets its own cover, wide cover, background and logo in the Steam library. Artwork you set
  yourself as .jpg or .webp is kept.
- **Started from Steam:** if VeyAmux was started from Steam (Game Mode or the library), it cannot close Steam without
  closing itself. It asks you to start VeyAmux from the desktop instead.

### lsfg-vk 2 installed automatically
- **Automatic install:** if VeyAmux finds an older lsfg-vk (1.x), it downloads the official lsfg-vk 2 build from
  builds.lsfg-vk.dev, checks it, and unpacks it into `~/.local`. Restart VeyAmux afterwards.
- **Your old files:** the old lsfg-vk 1 files in your home folder are moved to
  `~/.local/share/VeyAmux/lsfg-vk-1-backup/`, not deleted.
- **Button:** the menu also has an **Install lsfg-vk 2** button.
- **System package:** an lsfg-vk 1 installed as a system package is left alone. Remove it with your package manager.
- **Turn it off:** `VEYAMUX_LSFG_AUTOINSTALL=0`.

### Download
Download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. It can update itself through the
`.zsync` file (for example with Gear Lever).

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them. It only downloads lsfg-vk from its official site on your computer.
