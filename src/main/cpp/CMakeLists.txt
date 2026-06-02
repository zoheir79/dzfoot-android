cmake_minimum_required(VERSION 3.22)
project(football_ar)

set(CMAKE_CXX_STANDARD 17)

# Enable 16 KB page size alignment for Android 15+ compatibility
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

# Headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/protocol)
include_directories(${ARCORE_INCLUDE})

add_library(football_ar SHARED
    jni_main.cpp
    ARManager.cpp
    ARRenderer.cpp
    GameBridge.cpp
    Mesh.cpp
    Shader.cpp
    NetworkClient.cpp
    AudioManager.cpp
    AssetLoader.cpp
    AnimationPlayer.cpp
    InputManager.cpp
    GLBLoader.cpp
    SceneGraph.cpp
    Camera.cpp
    EntityInterpolator.cpp
    PredictionBuffer.cpp
    DeadReckoning.cpp
    DirectionalAnimBank.cpp
    tests/test_protocol_layout.cpp
)

# Link ARCore library
add_library(arcore SHARED IMPORTED)
set_target_properties(arcore PROPERTIES IMPORTED_LOCATION
    ${ARCORE_LIBPATH}/${ANDROID_ABI}/libarcore_sdk_c.so)

target_link_libraries(football_ar
    arcore
    jnigraphics
    android
    log
    GLESv3
    EGL
)
