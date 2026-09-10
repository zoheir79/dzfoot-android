# DZFoot Godot Integration — Status & Next Steps

## What's been created

### Godot Project (`godot/`)
- `project.godot` — Godot 4.5 project config (Forward+ renderer, 1080p, input mappings)
- `scenes/MainScene.tscn` — Main 3D scene root
- `scripts/DZFootScene.gd` — GDScript orchestrator:
  - Creates camera, lighting, stadium, ball, players, officials at runtime
  - Reads GameStatePacket data from DZFootBridge (GDExtension)
  - Maps AnimId → AnimationPlayer clip names
  - Handles keyboard/touch input → PlayerInputPacket
  - Coordinate conversion: GF (X-right, Y-up, Z-forward) → Godot (X-right, Y-up, -Z-forward)
- `icon.svg` — App icon

### GDExtension Bridge (`godot/src/`)
- `dzfoot_gdext.cpp` — C++ GDExtension exposing `DZFootBridge` class to Godot:
  - `set_game_state_bytes(PackedByteArray)` — parse GameStatePacket
  - `set_match_setup_bytes(PackedByteArray)` — parse MatchSetupPacket
  - `set_match_event_bytes(PackedByteArray)` — parse MatchEventPacket
  - Typed accessors: `get_ball_position()`, `get_player_position(idx)`, `get_player_anim(idx)`, etc.
  - Input serialization: `set_input_direction()`, `set_input_buttons()`, `get_input_bytes()`
  - Thread-safe with mutex
  - Full validation using `DZFootProtocol.h` helpers
- `DZFootProtocol.h` — Copy of shared binary protocol header
- `SConstruct` — SCons build file for godot-cpp

### GDExtension Config (`godot/addons/dzfoot_gdext/`)
- `dzfoot.gdextension` — GDExtension resource file (maps platform → .so/.dll)

### Android Integration
- `GodotAdapter.kt` — Kotlin wrapper managing Godot engine lifecycle:
  - `initialize()` → returns Godot View for layout
  - `sendGameState(bytes)` → forward LiveKit data to Godot
  - `getPlayerInput()` → get PlayerInputPacket from Godot
  - `onPause()`, `onResume()`, `destroy()`
- `build.gradle` — Updated:
  - Added `godot-lib.4.5.1.stable.template_release.aar` dependency
  - Added `godot/` as assets source directory
  - Restricted ABI to `arm64-v8a`
- `AndroidManifest.xml` — Updated:
  - ARCore now optional (`required="false"`)
  - Added Vulkan feature declarations
- `proguard-rules.pro` — Updated with Godot keep rules

### Setup Scripts
- `setup_godot.sh` — Linux/Mac setup
- `setup_godot.ps1` — Windows PowerShell setup

## What's pending

### 1. Export Templates Download (in progress)
- Godot 4.5.1 export templates (~1.1GB) downloading
- Once downloaded: extract and copy `godot-lib.4.5.1.stable.template_release.aar` to `libs/`

### 2. Build GDExtension
```bash
cd godot
git clone https://github.com/godotengine/godot-cpp.git --branch 4.5.1-stable --depth 1
scons platform=windows target=template_release       # For editor testing
scons platform=android target=template_release arch=arm64  # For APK
```

### 3. Import 3D Assets
- Stadium `.glb` → import into Godot with PBR materials
- Player `.glb` (with skeleton) → Skeleton3D + AnimationPlayer
- Ball model
- Pitch texture + normal map

### 4. Animation Mapping
17 AnimId values need corresponding AnimationPlayer clips:
| AnimId | Name | Description |
|--------|------|-------------|
| 0 | idle | Standing idle |
| 1 | walk | Walking |
| 2 | run | Running |
| 3 | sprint | Sprinting |
| 4 | shoot_right | Right-foot shot |
| 5 | shoot_left | Left-foot shot |
| 6 | pass_short | Short pass |
| 7 | pass_long | Long pass |
| 8 | header | Header |
| 9 | tackle | Tackle |
| 10 | dribble | Dribble |
| 11 | fall | Falling |
| 12 | celebrate | Goal celebration |
| 13 | gk_idle | Goalkeeper idle |
| 14 | gk_dive_left | GK dive left |
| 15 | gk_dive_right | GK dive right |
| 16 | gk_catch | GK catch |

### 5. Adapt MainActivity.kt
- Replace `GLSurfaceView` with `GodotAdapter.initialize()` view
- Route LiveKit callbacks to `GodotAdapter.sendGameState()`
- Route touch input to Godot via `GodotAdapter`
- Keep Compose UI overlay (score, timer, loading, controls)

### 6. Build & Test APK
```bash
cd e:\dzfoot\repos\dzfoot-android
.\gradlew assembleDebug
```

## Architecture

```
Kotlin (LiveKit + UI)
    ↓ GameStatePacket bytes
GodotAdapter.sendGameState()
    ↓ PackedByteArray
DZFootBridge (GDExtension C++)
    ↓ typed accessors
DZFootScene.gd (GDScript)
    ↓ Node3D positions, rotations, animations
Godot Renderer (Vulkan/GLES3)
    ↓
Screen
```
