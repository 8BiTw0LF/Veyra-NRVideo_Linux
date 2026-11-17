// VeyAmux for Linux: VeyAmux uses FidelityFX frame interpolation into its own textures (like the Windows build), not the
// SDK's swapchain replacement. ffx_vk.cpp still references this entry point of the (not compiled) swapchain code.
#include <FidelityFX/host/backends/vk/ffx_vk.h>

FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) { return FFX_ERROR_INVALID_ARGUMENT; }

// VeyAmux for Linux: last backend failure (see ffx_linux_compat.h)
static int g_vxLine = 0, g_vxVk = 0;
static char g_vxText[96];
void ffxVeyamuxNoteVk(int vkResult) { g_vxVk = vkResult; }
int ffxVeyamuxBackendErrorAt(int line) {
    g_vxLine = line;
    std::snprintf(g_vxText, sizeof g_vxText, "ffx_vk.cpp:%d%s%d", line, g_vxVk ? ", VkResult " : "", g_vxVk);
    if (!g_vxVk) std::snprintf(g_vxText, sizeof g_vxText, "ffx_vk.cpp:%d", line);
    std::fprintf(stderr, "FidelityFX Vulkan backend: error at %s\n", g_vxText);
    g_vxVk = 0;
    return int(FFX_ERROR_BACKEND_API_ERROR);
}
const char* ffxVeyamuxLastBackendError() { return g_vxLine ? g_vxText : ""; }
