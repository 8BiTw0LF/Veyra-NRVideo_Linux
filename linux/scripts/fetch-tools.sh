#!/usr/bin/env bash
# Downloads DXC (HLSL->SPIR-V), wgpu-native (the demo scene's WebGPU) and the AppImage tooling into .tools
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$ROOT/.tools"; mkdir -p "$T/dxc" "$T/appimage"
# Every tool is a fixed release whose SHA-256 is checked: a changed or replaced download stops the build instead of
# ending up inside the AppImage.
check() { echo "$2  $1" | sha256sum -c --quiet - || { echo "SHA-256 mismatch for $1 (got $(sha256sum "$1" | cut -d' ' -f1))"; rm -f "$1"; exit 1; }; }
DXC_TAG=v1.9.2609
DXC_SHA=96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
if [ ! -x "$T/dxc/bin/dxc" ]; then
  if [ -n "${GITHUB_TOKEN:-}" ] && command -v gh >/dev/null; then   # CI: authenticated, no API rate limit
    GH_TOKEN="$GITHUB_TOKEN" gh release download "$DXC_TAG" -R microsoft/DirectXShaderCompiler -p 'linux_dxc*' -O "$T/dxc.tgz" --clobber
  else
    url=$(curl -sL "https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/tags/$DXC_TAG" \
          | python3 -c "import json,sys; print(next(a['browser_download_url'] for a in json.load(sys.stdin)['assets'] if a['name'].startswith('linux_dxc')))")
    echo "DXC: $url"; curl -sL --fail -o "$T/dxc.tgz" "$url"
  fi
  if [ "$DXC_SHA" = PENDING ]; then echo "DXC $DXC_TAG SHA-256: $(sha256sum "$T/dxc.tgz" | cut -d' ' -f1)"; else check "$T/dxc.tgz" "$DXC_SHA"; fi
  tar xzf "$T/dxc.tgz" -C "$T/dxc"; chmod +x "$T/dxc/bin/dxc"
fi
# wgpu-native: WebGPU on Vulkan for the demo scene (Abyssal Garden), a fixed release, checked
WGPU_VER=v29.0.0.0
WGPU_SHA=cf614af80f23c6364a13f6569e7ae4ca7367ebf6062e3d3d6e80e205264636b4
if [ ! -f "$T/wgpu/lib/libwgpu_native.a" ]; then
  curl -sL --fail -o "$T/wgpu.zip" "https://github.com/gfx-rs/wgpu-native/releases/download/$WGPU_VER/wgpu-linux-x86_64-release.zip"
  check "$T/wgpu.zip" "$WGPU_SHA"
  rm -rf "$T/wgpu"; mkdir -p "$T/wgpu"; (cd "$T/wgpu" && python3 -m zipfile -e "$T/wgpu.zip" .)
fi
get() {   # url sha256 [name]
  local f="$T/appimage/${3:-$(basename "$1")}"
  if [ ! -x "$f" ]; then curl -sL --fail -o "$f" "$1"; check "$f" "$2"; chmod +x "$f"; fi
}
get https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
get https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
get https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
# the AppImage runtime (appimagetool would otherwise download the newest one while building)
get https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
echo "tools ready in $T"
