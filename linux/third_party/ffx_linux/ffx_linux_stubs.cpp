// VeyAmux for Linux: VeyAmux uses FidelityFX frame interpolation into its own textures (like the Windows build), not the
// SDK's swapchain replacement. ffx_vk.cpp still references this entry point of the (not compiled) swapchain code.
#include <FidelityFX/host/backends/vk/ffx_vk.h>

FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) { return FFX_ERROR_INVALID_ARGUMENT; }
