#!/usr/bin/env bash
# Copies the parts of FidelityFX SDK 1.1.4 that VeyAmux uses (optical flow, frame interpolation, FSR 3.1 upscaler, Vulkan backend) and
# applies the Linux patches. Original files are MIT licensed by AMD; LICENSE.txt is copied along.
# usage: prepare_sdk.sh <fidelityfx-sdk-root> <dest>
set -euo pipefail
SRC="$1/sdk"; DST="$2"
rm -rf "$DST"; mkdir -p "$DST"
cp "$SRC/LICENSE.txt" "$DST/"
mkdir -p "$DST/include" "$DST/src/components" "$DST/src/backends"
mkdir -p "$DST/include/FidelityFX/gpu"
cp -r "$SRC/include/FidelityFX/host" "$DST/include/FidelityFX/"
cp "$SRC"/include/FidelityFX/gpu/*.h "$DST/include/FidelityFX/gpu/"
cp -r "$SRC/include/FidelityFX/gpu/frameinterpolation" "$SRC/include/FidelityFX/gpu/opticalflow" "$SRC/include/FidelityFX/gpu/spd" "$SRC/include/FidelityFX/gpu/fsr3upscaler" "$SRC/include/FidelityFX/gpu/fsr1" "$DST/include/FidelityFX/gpu/"
cp -r "$SRC/src/shared" "$DST/src/"
cp -r "$SRC/src/components/frameinterpolation" "$SRC/src/components/opticalflow" "$SRC/src/components/fsr3upscaler" "$DST/src/components/"
mkdir -p "$DST/src/backends/vk" "$DST/src/backends/shared/blob_accessors"
cp "$SRC/src/backends/vk/ffx_vk.cpp" "$DST/src/backends/vk/"
mkdir -p "$DST/src/backends/vk/shaders"
cp -r "$SRC/src/backends/vk/shaders/frameinterpolation" "$SRC/src/backends/vk/shaders/opticalflow" "$SRC/src/backends/vk/shaders/fsr3upscaler" "$DST/src/backends/vk/shaders/"
cp "$SRC/src/backends/shared/ffx_shader_blobs.cpp" "$SRC/src/backends/shared/ffx_shader_blobs.h" "$DST/src/backends/shared/"
cp "$SRC"/src/backends/shared/blob_accessors/ffx_{frameinterpolation,opticalflow,fsr3upscaler}_shaderblobs.{cpp,h} "$DST/src/backends/shared/blob_accessors/"
# wchar_t is 4 bytes on Linux (2 on Windows): the private contexts no longer fit the default opaque size.
sed -i 's/^#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 \* 128)/#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)  \/\/ VeyAmux Linux: doubled for 4-byte wchar_t/' \
    "$DST/include/FidelityFX/host/ffx_types.h"
grep -q 'VeyAmux Linux' "$DST/include/FidelityFX/host/ffx_types.h" || { echo "context-size patch failed" >&2; exit 1; }

# Unified-memory GPUs (APUs, software rasterisers) only expose host-visible device-local memory; the SDK skipped those
# types and failed to allocate. Keep them as a fallback (a non-host-visible type found later still wins).
python3 -I - "$DST/src/backends/vk/ffx_vk.cpp" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = """            if (requestedProperties == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                continue;"""
new = """            if (requestedProperties == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            {   // VeyAmux Linux: keep as fallback for unified-memory devices
                if (bestCandidate == UINT32_MAX) { bestCandidate = i; outProperties = memProperties.memoryTypes[i].propertyFlags; }
                continue;
            }"""
assert old in s, "memory-type patch anchor not found"
open(p, "w").write(s.replace(old, new))
PY

# The Vulkan backend carves its tables out of the scratch buffer with 4-byte alignment. GCC (rightly) assumes natural
# alignment and emits aligned SSE stores -> SIGSEGV. Align every sub-array (and the first one) to 64 bytes.
python3 -I - "$DST/src/backends/vk/ffx_vk.cpp" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
n = 0
def fix(m):
    global n; n += 1
    return m.group(1) + "64);  // VeyAmux Linux: was sizeof(uint32_t)"
s = re.sub(r"(uint32_t \w+ArraySize\s*=\s*FFX_ALIGN_UP\(.*?,\s*)sizeof\(uint32_t\)\);", fix, s)
assert n >= 12, f"alignment patch matched {n} lines"
old_start = "uint8_t* pMem = (uint8_t*)((BackendContext_VK*)(backendContext + 1));"
assert old_start in s, "pMem anchor not found"
s = s.replace(old_start, "uint8_t* pMem = (uint8_t*)FFX_ALIGN_UP((uintptr_t)(backendContext + 1), 64);  // VeyAmux Linux: aligned start")
old_ret = "    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + extensionPropArraySize"
assert old_ret in s, "scratch size anchor not found"
s = s.replace(old_ret, "    return FFX_ALIGN_UP(64 + sizeof(BackendContext_VK) + extensionPropArraySize  /* VeyAmux Linux: +64 alignment slack */")
open(p, "w").write(s)
PY

# Optical flow combines per-wave partial results only for wave64 (1 wave) and wave32 (2 waves) per 64-thread group.
# GPUs with smaller subgroups (Intel 8/16, software rasterisers) got wrong block costs -> no motion. Add a generic
# path for any subgroup size below 32; the wave32/wave64 paths used by AMD GPUs are unchanged.
python3 -I - "$DST/include/FidelityFX/gpu/opticalflow/ffx_opticalflow_compute_optical_flow_v5.h" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
decl_old = "FFX_GROUPSHARED FfxUInt32 sWaveMin[MaxWaves];"
decl_new = decl_old + "\n// VeyAmux Linux: partials for subgroups smaller than 32 lanes (up to 64 waves of 1 lane).\nFFX_GROUPSHARED FfxUInt32 sSmallWave[ThreadCount];"
sad_old = """        FFX_GROUP_MEMORY_BARRIER;
        blockSadSum += sWaveSad[waveId ^ 1];
    }

    return blockSadSum;"""
sad_new = """        FFX_GROUP_MEMORY_BARRIER;
        blockSadSum += sWaveSad[waveId ^ 1];
    }
    else if (ffxWaveLaneCount() < 32)
    {   // VeyAmux Linux: generic combination for small subgroups
        FfxUInt32 lanes = ffxWaveLaneCount();
        FfxInt32 waveId = iLocalIndex / FfxInt32(lanes);
        if (ffxWaveIsFirstLane())
        {
            sSmallWave[waveId] = blockSadSum;
        }
        FFX_GROUP_MEMORY_BARRIER;
        FfxUInt32 total = 0u;
        for (FfxInt32 w = 0; w < ThreadCount / FfxInt32(lanes); ++w)
        {
            total += sSmallWave[w];
        }
        FFX_GROUP_MEMORY_BARRIER;
        blockSadSum = total;
    }

    return blockSadSum;"""
min_old = """        FFX_GROUP_MEMORY_BARRIER;
        min0123 = ffxMin(min0123, sWaveMin[waveId ^ 1]);
    }"""
min_new = min_old + """
    else if (ffxWaveLaneCount() < 32)
    {   // VeyAmux Linux: generic combination for small subgroups
        FfxUInt32 lanes = ffxWaveLaneCount();
        FfxInt32 waveId = iLocalIndex / FfxInt32(lanes);
        if (ffxWaveIsFirstLane())
        {
            sSmallWave[waveId] = min0123;
        }
        FFX_GROUP_MEMORY_BARRIER;
        FfxUInt32 m = 0xFFFFFFFFu;
        for (FfxInt32 w = 0; w < ThreadCount / FfxInt32(lanes); ++w)
        {
            m = ffxMin(m, sSmallWave[w]);
        }
        FFX_GROUP_MEMORY_BARRIER;
        min0123 = m;
    }"""
for a, b in ((decl_old, decl_new), (sad_old, sad_new), (min_old, min_new)):
    assert s.count(a) == 1, "optical-flow patch anchor: " + a[:40]
    s = s.replace(a, b)
open(p, "w").write(s)
PY

# The Vulkan backend recycles descriptor sets, uniform-buffer slices and image views per "queued frame" (4). Frame
# interpolation executes twice per frame (prepare + dispatch), so they were reused after only 2 frames while Qt keeps
# 2-3 frames in flight. 8 keeps them alive for 4 real frames.
python3 -I - "$DST" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1]); n = 0
for f in list(root.rglob("*.h")) + list(root.rglob("*.cpp")):
    s = f.read_text(errors="replace")
    t = re.sub(r"#define FFX_MAX_QUEUED_FRAMES\s+\(?4\)?", "#define FFX_MAX_QUEUED_FRAMES (8)  // VeyAmux Linux: was 4", s)
    if t != s:
        f.write_text(t); n += 1
assert n >= 1, "FFX_MAX_QUEUED_FRAMES not found"
PY
echo "prepared FidelityFX SDK subset in $DST"
