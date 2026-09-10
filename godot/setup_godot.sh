#!/bin/bash
# Setup script for DZFoot Godot integration
# Run this once to prepare the build environment

set -e

GODOT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "[DZFoot] Setting up Godot integration in $GODOT_DIR"

# 1. Clone godot-cpp (needed to build GDExtension)
if [ ! -d "$GODOT_DIR/godot-cpp" ]; then
    echo "[DZFoot] Cloning godot-cpp (4.5.1)..."
    git clone https://github.com/godotengine/godot-cpp.git --branch 4.5.1-stable --depth 1 "$GODOT_DIR/godot-cpp"
    cd "$GODOT_DIR/godot-cpp"
    scons platform=windows target=template_release
    cd "$GODOT_DIR"
else
    echo "[DZFoot] godot-cpp already exists, skipping clone"
fi

# 2. Build GDExtension for Windows (for editor testing)
echo "[DZFoot] Building GDExtension for Windows..."
cd "$GODOT_DIR"
scons platform=windows target=template_release

# 3. Build GDExtension for Android (for APK)
echo "[DZFoot] Building GDExtension for Android (arm64)..."
scons platform=android target=template_release arch=arm64

# 4. Extract Godot Android AAR from export templates
TEMPLATES_DIR="${TEMPLATES_DIR:-$HOME/Downloads/Godot/templates}"
AAR_SRC="$TEMPLATES_DIR/android-release/arm64-v8a/godot-lib.4.5.1.stable.template_release.aar"
AAR_DST="$(dirname "$GODOT_DIR")/libs/godot-lib.4.5.1.stable.template_release.aar"

if [ -f "$AAR_SRC" ]; then
    echo "[DZFoot] Copying Godot Android AAR..."
    mkdir -p "$(dirname "$AAR_DST")"
    cp "$AAR_SRC" "$AAR_DST"
else
    echo "[DZFoot] WARNING: Godot Android AAR not found at $AAR_SRC"
    echo "         Extract export templates and copy the AAR manually."
fi

echo "[DZFoot] Setup complete!"
echo ""
echo "Next steps:"
echo "  1. Open the Godot project in $GODOT_DIR with Godot editor"
echo "  2. Import 3D models (stadium .glb, player .glb) into the project"
echo "  3. Set up AnimationPlayer clips for the 17 AnimId values"
echo "  4. Build the Android APK: cd .. && ./gradlew assembleDebug"
