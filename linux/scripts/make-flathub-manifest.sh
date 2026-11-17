#!/usr/bin/env bash
# Writes the files for the Flathub repository (github.com/flathub/io.github._8BiTw0LF.VeyAmux) into dist/flathub:
# the Flatpak manifest with VeyAmux taken from a released git tag (Flathub builds only from fixed sources) and
# flathub.json (x86_64 only: the HLSL compiler used while building exists only for x86_64).
#
#   scripts/make-flathub-manifest.sh v2.1.5-linux20
#
# The tag must already be pushed to github.com/8BiTw0LF/VeyAmux.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:?usage: $0 <tag, e.g. v2.1.5-linux20>}"
COMMIT="$(git -C "$ROOT" rev-parse "$TAG^{commit}")"
ID=io.github._8BiTw0LF.VeyAmux
OUT="$ROOT/dist/flathub"
mkdir -p "$OUT"
python3 - "$ROOT/packaging/flatpak/$ID.yml" "$OUT/$ID.yml" "$TAG" "$COMMIT" <<'EOF'
import sys
src, dst, tag, commit = sys.argv[1:]
text = open(src).read()
local = text[text.index("      - type: dir\n        path: ../..\n"):]
git = ("      - type: git\n"
       "        url: https://github.com/8BiTw0LF/VeyAmux.git\n"
       f"        tag: {tag}\n"
       f"        commit: {commit}\n"
       "        x-checker-data:\n"
       "          type: git\n"
       "          tag-pattern: ^(v[\\d.]+-linux\\d+)$\n")
# the header comment describes the local build; Flathub's copy starts at the id
text = text[text.index("id: "):].replace(local, git)
open(dst, "w").write(text)
EOF
printf '{\n  "only-arches": ["x86_64"]\n}\n' > "$OUT/flathub.json"
echo "written: $OUT/$ID.yml, $OUT/flathub.json ($TAG = $COMMIT)"
