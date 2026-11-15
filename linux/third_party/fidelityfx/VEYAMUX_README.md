# FidelityFX SDK 1.1.4 subset (AMD, MIT licence — see LICENSE.txt)

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
