# Third-party components in the Linux build

VeyAmux is based on **Veyra-NRVideo 2.1.5** (Windows, GPL-3.0, https://github.com/Likely7/Veyra-NRVideo); its HLSL shaders and
processing design are reused, and VeyAmux is distributed under the same GPL-3.0 licence.

| Component | Where | Licence | Notes |
|---|---|---|---|
| Veyra-NRVideo 2.1.5 shaders: `RgbToLinear`, `AaLuma`, `ScaleBlit`, `AaFxaa`, `HdrColor`, `HdrToSdr`, `ColorGrade`, `AntiAlias` | `shaders/veyra` | GPL-3.0 (`LICENSE`) | Copied unchanged from https://github.com/Likely7/Veyra-NRVideo (`shaders/`). `AaFxaa.hlsl` is Veyra's port of SAOG0721/Magpie commit `d687997` (GPL-3.0), itself from libretro/slang-shaders `fxaa.slang`; the file keeps the Copyright (c) 2011 NVIDIA Corporation notice in its header. |
| AMD FidelityFX SDK 1.1.4 (FSR 3.1 upscaler, optical flow and frame interpolation, FSR1/RCAS shader headers, Vulkan backend) | `third_party/fidelityfx` | MIT (`third_party/fidelityfx/LICENSE.txt`) | Source: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK tag v1.1.4. Linux changes are applied by `third_party/ffx_linux/prepare_sdk.sh` and listed in `third_party/fidelityfx/VEYAMUX_README.md`. |
| SPIRV-Reflect (as shipped in the FidelityFX SDK tools) | `third_party/ffx_tools/SPIRV-Reflect` | Apache-2.0 (`LICENSE`) | Build-time tool only (shader reflection). |
| FidelityFX_SC header format | `third_party/ffx_tools/ffx_sc_linux.py` | MIT (AMD) | Independent Python re-implementation of the GLSL path of the SDK's shader compiler tool. |
| Qt 6 (incl. Qt Network for the local OBS stream), FFmpeg, PulseAudio client, libva | bundled in the AppImage by linuxdeploy | LGPL-3.0 / LGPL-2.1+ | Unmodified distribution packages. |
| Microsoft DirectXShaderCompiler | build tool only | University of Illinois/NCSA | Not shipped. |

Not included: **lsfg-vk** (CC BY-NC-ND 4.0) and **Lossless Scaling** (commercial, Steam). VeyAmux only detects a copy the user
installed and enables it for its own window through lsfg-vk's documented environment variables; no code, shaders or DLLs
from either are copied, translated or redistributed.
