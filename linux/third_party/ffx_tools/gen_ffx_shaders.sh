#!/usr/bin/env bash
# Generates the FidelityFX SDK 1.1.4 Vulkan shader permutation headers for optical flow + frame interpolation + FSR 3.1 upscaler on Linux.
# usage: gen_ffx_shaders.sh <ffx-sdk-root>/sdk <out-dir> <ffx_reflect> [glslangValidator]
set -euo pipefail
SDK="$1"; OUT="$2"; REFLECT="$3"; GLSLANG="${4:-glslangValidator}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GPU="$SDK/include/FidelityFX/gpu"
API=(-e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1)
mkdir -p "$OUT"

gen() {  # gen <component> <shader.glsl> <base-args...> -- <perm-args...>
  local comp="$1" src="$2"; shift 2
  local base=() perm=()
  while [ "$1" != "--" ]; do base+=("$1"); shift; done; shift
  perm=("$@")
  local stem; stem="$(basename "$src" .glsl)"
  for variant in "" "_wave64" "_16bit" "_wave64_16bit"; do
    local half=0; [[ "$variant" == *16bit* ]] && half=1
    python3 -I "$HERE/ffx_sc_linux.py" --reflect "$REFLECT" --glslang "$GLSLANG" --out "$OUT" --name "${stem}${variant}" \
      "${base[@]}" "${API[@]}" "${perm[@]}" -DFFX_HALF=$half "-I$GPU" "-I$GPU/$comp" "$src" >/dev/null &
  done
  wait
}

FI_BASE=(-DFFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0 -DFFX_FRAMEINTERPOLATION_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
         -DFFX_FRAMEINTERPOLATION_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1 -DFFX_FRAMEINTERPOLATION_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
         -DFFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2 -DFFX_GPU=1)
FI_PERM=("-DFFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS={0,1}" "-DFFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS={0,1}"
         "-DFFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH={0,1}")
if [ "${VEYAMUX_FFX_ONLY:-}" != fsr3 ]; then
for s in "$SDK"/src/backends/vk/shaders/frameinterpolation/*.glsl; do gen frameinterpolation "$s" "${FI_BASE[@]}" -- "${FI_PERM[@]}"; done

OF_BASE=(-DFFX_GPU=1)
OF_PERM=("-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT={0,1}")
for n in prepare_luma_pass compute_luminance_pyramid_pass generate_scd_histogram_pass compute_scd_divergence_pass \
         compute_optical_flow_advanced_pass_v5 filter_optical_flow_pass_v5 scale_optical_flow_advanced_pass_v5; do
  gen opticalflow "$SDK/src/backends/vk/shaders/opticalflow/ffx_opticalflow_$n.glsl" "${OF_BASE[@]}" -- "${OF_PERM[@]}"
done
fi
# FSR 3.1 upscaler (same options as the SDK's CMakeCompileFSR3UpscalerShaders.txt). VEYAMUX_FFX_ONLY=fsr3 limits a run.
UP_BASE=(-DFFX_GPU=1 -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0 -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
         -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1 -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
         -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2)
UP_PERM=("-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}" "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
         "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}" "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}"
         "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}" "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}")
for s in "$SDK"/src/backends/vk/shaders/fsr3upscaler/*.glsl; do gen fsr3upscaler "$s" "${UP_BASE[@]}" -- "${UP_PERM[@]}"; done
echo "generated $(ls "$OUT"/*_permutations.h | wc -l) permutation headers in $OUT"
