cmake_minimum_required(VERSION 3.22)
project(football_ar)

set(CMAKE_CXX_STANDARD 17)

# Headers are now in src/main/cpp directly
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

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
