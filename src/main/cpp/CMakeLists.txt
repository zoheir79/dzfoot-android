cmake_minimum_required(VERSION 3.22)
project(football_ar)

set(CMAKE_CXX_STANDARD 17)

# GameplayFootball as static lib (no SDL display in AR mode)
# add_subdirectory(GameplayFootball GF_build)

# ARCore NDK
find_package(arcore REQUIRED)

# ENet fallback
include(FetchContent)
FetchContent_Declare(enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG master)
FetchContent_MakeAvailable(enet)

# Main shared lib
add_library(football_ar SHARED
    jni_main.cpp
    ARManager.cpp
    ARRenderer.cpp
    NetworkClient.cpp
    GameBridge.cpp
    Shader.cpp
    Mesh.cpp
    AssetLoader.cpp
    AudioManager.cpp
    AnimationPlayer.cpp
    InputManager.cpp
)

target_link_libraries(football_ar
    # gameplayfootball_lib
    enet
    ${arcore_LIBRARIES}
    android log GLESv3 EGL z
)
