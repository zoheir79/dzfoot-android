# dzfoot-android

Android AR client for DZFoot — Football 3D multiplayer in augmented reality.

## Stack
- Kotlin + C++17 NDK
- ARCore (marker tracking)
- OpenGL ES 3.0 (custom renderer)
- LiveKit Android SDK 2.4.0 (data channels + audio)

## Build

```bash
cd app
# Place marker.jpg in src/main/assets/
# Place .ogg sounds in src/main/assets/sounds/
# Place .glb models in src/main/assets/models/
./gradlew assembleDebug
```

## Architecture

```
MainActivity.kt
├── GLSurfaceView.Renderer
│   └── nativeOnFrame() ──► jni_main.cpp
│       ├── ARManager (ARCore session, anchor)
│       ├── ARRenderer (camera background + 3D)
│       └── GameBridge (GameState deserialization)
├── LiveKitManager.kt (topics gs/ev/in)
└── JniBridge.kt (8 native methods)
```

## Assets Required

| Asset | Path | Source |
|---|---|---|
| marker.jpg | `assets/marker.jpg` | Custom ArUco A4 |
| player.glb | `assets/models/player.glb` | Mixamo |
| ball.glb | `assets/models/ball.glb` | Mixamo/Self |
| sounds/*.ogg | `assets/sounds/*.ogg` | Generated/Freesound |

## Hardware Requirements
- Android 7.0+ (API 24)
- ARCore certified device
- 6 GB RAM minimum

## License
MIT
