## VeyAmux-V.1.1

### AMD FSR 3.1.6
- **FSR upscaling and FSR frame generation are updated from 3.1.4 to 3.1.6** (the effect code from AMD's FidelityFX
  SDK 2.4.0).
  - Upscaling: on GPUs that use FSR's 16-bit path (most current GPUs, including the Radeon RX 6000 to 9000 series),
    the reprojection now reads the history at the previous frame's size.
  - Frame generation: works inside the actual picture area, and it now prepares its motion fields the way AMD does in
    3.1.6.
- FSR 4 is not included. AMD only ships it as signed Windows DLLs, so it cannot run on Linux.

### Switch between Lossless Scaling and FSR frame generation
- **AMD FSR 3.1.6 2× (free)** is now always in the *Frame generation* menu, right below the Lossless Scaling modes.
  Before, it was only shown when Lossless Scaling was missing.
- **Switching while VeyAmux runs:** when Lossless Scaling was active at start, you can switch between *Fixed 2×*,
  *Fixed 3×* and *FSR 3.1.6 2×* while VeyAmux runs.
- **Started in FSR mode:** if VeyAmux was started in FSR mode, switching to Lossless Scaling asks for one restart
  (*Restart now*). Lossless Scaling has to be loaded when VeyAmux starts.

### Download
- **AppImage:** download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. It can update
  itself through the `.zsync` file (for example with Gear Lever).
- **Flatpak:**
  - **Repository:** if you added the VeyAmux repository, `flatpak update` (or Discover) brings 1.1.
  - **Single file:** download `VeyAmux-x86_64.flatpak` below and install it with
    `flatpak install --user VeyAmux-x86_64.flatpak`. The Flatpak is added a few minutes after the AppImage.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
