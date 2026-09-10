# Setup script for DZFoot Godot integration (Windows PowerShell)
# Run this once to prepare the build environment

$ErrorActionPreference = "Stop"
$GODOT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$ANDROID_DIR = Split-Path -Parent $GODOT_DIR

Write-Host "[DZFoot] Setting up Godot integration in $GODOT_DIR"

# 1. Clone godot-cpp (needed to build GDExtension)
if (-not (Test-Path "$GODOT_DIR\godot-cpp")) {
    Write-Host "[DZFoot] Cloning godot-cpp (4.5.1)..."
    git clone https://github.com/godotengine/godot-cpp.git --branch 4.5.1-stable --depth 1 "$GODOT_DIR\godot-cpp"
    Push-Location "$GODOT_DIR\godot-cpp"
    scons platform=windows target=template_release
    Pop-Location
} else {
    Write-Host "[DZFoot] godot-cpp already exists, skipping clone"
}

# 2. Build GDExtension for Windows (for editor testing)
Write-Host "[DZFoot] Building GDExtension for Windows..."
Push-Location $GODOT_DIR
scons platform=windows target=template_release
Pop-Location

# 3. Build GDExtension for Android (for APK)
Write-Host "[DZFoot] Building GDExtension for Android (arm64)..."
Push-Location $GODOT_DIR
scons platform=android target=template_release arch=arm64
Pop-Location

# 4. Extract Godot Android AAR from export templates
$TEMPLATES_DIR = if ($env:TEMPLATES_DIR) { $env:TEMPLATES_DIR } else { "$env:USERPROFILE\Downloads\Godot\templates" }
$AAR_SRC = "$TEMPLATES_DIR\android-release\arm64-v8a\godot-lib.4.5.1.stable.template_release.aar"
$AAR_DST = "$ANDROID_DIR\libs\godot-lib.4.5.1.stable.template_release.aar"

if (Test-Path $AAR_SRC) {
    Write-Host "[DZFoot] Copying Godot Android AAR..."
    New-Item -ItemType Directory -Force -Path (Split-Path $AAR_DST) | Out-Null
    Copy-Item $AAR_SRC $AAR_DST -Force
} else {
    Write-Host "[DZFoot] WARNING: Godot Android AAR not found at $AAR_SRC"
    Write-Host "         Extract export templates and copy the AAR manually."
}

Write-Host "[DZFoot] Setup complete!"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Open the Godot project in $GODOT_DIR with Godot editor"
Write-Host "  2. Import 3D models (stadium .glb, player .glb) into the project"
Write-Host "  3. Set up AnimationPlayer clips for the 17 AnimId values"
Write-Host "  4. Build the Android APK: cd .. ; .\gradlew assembleDebug"
