#!/usr/bin/env bash
# Downloads DXC (HLSL->SPIR-V), wgpu-native (the demo scene's WebGPU) and the AppImage tooling into .tools
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$ROOT/.tools"; mkdir -p "$T/dxc" "$T/appimage"
if [ ! -x "$T/dxc/bin/dxc" ]; then
  if [ -n "${GITHUB_TOKEN:-}" ] && command -v gh >/dev/null; then   # CI: authenticated, no API rate limit
    GH_TOKEN="$GITHUB_TOKEN" gh release download -R microsoft/DirectXShaderCompiler -p 'linux_dxc*' -O "$T/dxc.tgz" --clobber
  else
    url=$(curl -sL https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest \
          | python3 -c "import json,sys; print(next(a['browser_download_url'] for a in json.load(sys.stdin)['assets'] if a['name'].startswith('linux_dxc')))")
    echo "DXC: $url"; curl -sL -o "$T/dxc.tgz" "$url"
  fi
  tar xzf "$T/dxc.tgz" -C "$T/dxc"; chmod +x "$T/dxc/bin/dxc"
fi
# wgpu-native: WebGPU on Vulkan for the demo scene (Abyssal Garden), a fixed release, checked
WGPU_VER=v29.0.0.0
WGPU_SHA=cf614af80f23c6364a13f6569e7ae4ca7367ebf6062e3d3d6e80e205264636b4
if [ ! -f "$T/wgpu/lib/libwgpu_native.a" ]; then
  curl -sL --fail -o "$T/wgpu.zip" "https://github.com/gfx-rs/wgpu-native/releases/download/$WGPU_VER/wgpu-linux-x86_64-release.zip"
  echo "$WGPU_SHA  $T/wgpu.zip" | sha256sum -c -
  rm -rf "$T/wgpu"; mkdir -p "$T/wgpu"; (cd "$T/wgpu" && python3 -m zipfile -e "$T/wgpu.zip" .)
fi
get() { [ -x "$T/appimage/$(basename "$1")" ] || { curl -sL --fail -o "$T/appimage/$(basename "$1")" "$1"; chmod +x "$T/appimage/$(basename "$1")"; }; }
get https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
get https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
get https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
echo "tools ready in $T"
