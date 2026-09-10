# Godot Android Library

Place the Godot Android AAR file here:

```
libs/godot-lib.4.5.1.stable.template_release.aar
```

This file is extracted from the Godot 4.5.1 export templates (`Godot_v4.5.1-stable_export_templates.tpz`).

## How to extract

1. Download export templates from https://godotengine.org/download/archive/4.5.1-stable/
2. The .tpz file is a zip archive — rename to .zip and extract
3. Find `android-release/arm64-v8a/godot-lib.4.5.1.stable.template_release.aar`
4. Copy it to this directory

## Alternative: Build from source

```bash
git clone https://github.com/godotengine/godot.git --branch 4.5.1-stable
cd godot
scons platform=android target=template_release arch=arm64
# Output: platform/android/java/app/libs/godot-lib.4.5.1.stable.template_release.aar
```
