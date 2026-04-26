#!/bin/bash
# Build a macOS .pkg installer for SimeIME.
#
# Output: macOS/package/dist/Sime-<version>.pkg
# Install target: /Library/Input Methods/SimeIME.app
set -euo pipefail

VERSION="${1:-1.0}"
IDENTIFIER="com.sime.inputmethod.SimeIME"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
BUILD="$ROOT/build"
APP_BUILT="$BUILD/Release/SimeIME.app"
PAYLOAD="$ROOT/package/payload"
DIST="$ROOT/package/dist"
SCRIPTS="$ROOT/package/pkg-scripts"

echo ">> Configure & build (Release)"
cmake -S "$ROOT" -B "$BUILD" -G Xcode >/dev/null
cmake --build "$BUILD" --config Release >/dev/null

echo ">> Stage resources into bundle"
RES="$APP_BUILT/Contents/Resources"
mkdir -p "$RES"
cp -R "$ROOT/resources/en.lproj"    "$RES/"
cp -R "$ROOT/resources/zh_CN.lproj" "$RES/"
cp    "$ROOT/resources/sime.tiff"   "$RES/"
cp    "$REPO/pipeline/output/sime.cnt"  "$RES/"
cp    "$REPO/pipeline/output/sime.dict" "$RES/"

echo ">> Ad-hoc codesign"
codesign --force --deep --sign - "$APP_BUILT" >/dev/null

echo ">> Build payload tree"
rm -rf "$PAYLOAD"
mkdir -p "$PAYLOAD/Library/Input Methods"
cp -R "$APP_BUILT" "$PAYLOAD/Library/Input Methods/"

chmod +x "$SCRIPTS/postinstall"

echo ">> pkgbuild"
mkdir -p "$DIST"
OUT="$DIST/Sime-$VERSION.pkg"
pkgbuild \
  --root "$PAYLOAD" \
  --identifier "$IDENTIFIER" \
  --version "$VERSION" \
  --install-location "/" \
  --scripts "$SCRIPTS" \
  --ownership recommended \
  "$OUT"

echo
echo "Built: $OUT"
