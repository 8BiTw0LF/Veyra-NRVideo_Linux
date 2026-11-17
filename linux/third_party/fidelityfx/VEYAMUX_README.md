# FidelityFX SDK 1.1.4 subset with the FSR 3.1.6 effect code from SDK 2.4.0 (AMD, MIT licence — see LICENSE.txt)

Only what VeyAmux's Linux build uses for AMD FSR 3.1 frame generation and upscaling: optical flow, frame interpolation, the FSR 3.1 upscaler, the Vulkan
backend, and the GPU headers those shaders include. Taken unmodified from
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK (tag v1.1.4) by `../ffx_linux/prepare_sdk.sh`, with one
Linux changes: `FFX_SDK_DEFAULT_CONTEXT_SIZE` is doubled in `include/FidelityFX/host/ffx_types.h` because `wchar_t` is
4 bytes on Linux, and `findMemoryTypeIndex` in the Vulkan backend falls back to host-visible device-local memory on
unified-memory GPUs instead of failing, and the backend's scratch-buffer tables are 64-byte aligned (they were only
4-byte aligned, which GCC's SSE code does not tolerate). The optical-flow block reductions also get a generic path for
subgroups smaller than 32 lanes (Intel, software rasterisers); the wave32/wave64 paths used by AMD GPUs are unchanged.
`FFX_MAX_QUEUED_FRAMES` is raised from 4 to 8 so per-frame descriptors/views survive Qt's frames in flight (frame
interpolation executes twice per frame). Everything MSVC provides implicitly comes from `../ffx_linux/ffx_linux_compat.h` (force-included),
and the Windows-only FidelityFX_SC shader tool is replaced by `../ffx_tools/ffx_sc_linux.py`.

## FSR 3.1.6 (FidelityFX SDK 2.4.0)

SDK 2.x moved the effects behind its new loader API and ships their shaders as HLSL only. The framework here (host
API, Vulkan backend, GLSL entry points and shader tool) therefore stays at 1.1.4, and the effect code of FidelityFX
SDK v2.4.0 (https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v2.4.0) is carried over into it:

- `include/FidelityFX/gpu/{fsr3upscaler,frameinterpolation,opticalflow}/*.h` (all but the callbacks files) are the
  2.4.0 versions; only the `ffx_core.h` include path is changed back to this layout. The upscaler's 16-bit
  reprojection now samples the history at the previous frame's upscale size, and frame interpolation works in the
  new "colour rect" (the area of the upscaled picture) instead of the upscaler target size.
- The frame-interpolation constant buffer gains `colorRectBase`/`colorRectSize` (GLSL and HLSL callbacks,
  `ffx_frameinterpolation_private.h`), filled as in 2.4.0.
- As in 2.4.0, the game and optical-flow vector fields are cleared with clear jobs instead of in the setup pass, and
  aliasable resources get a transition barrier before their discard (upscaler and frame interpolation).
- Version numbers: FSR 3 upscaler 3.1.6, frame interpolation 3.1.6.

FSR 4 (upscaling 4.1.2, frame generation 4.0.2) and the Ray Regeneration denoiser are only distributed as signed
Windows DLLs in SDK 2.4.0 and cannot be used here; the frame-generation swapchain (3.1.7 / 4.0.0) replaces an
application's own swapchain and has no place in VeyAmux, which presents through Qt.
