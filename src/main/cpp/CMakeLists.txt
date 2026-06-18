cmake_minimum_required(VERSION 3.22)
project(football_ar)

set(CMAKE_CXX_STANDARD 17)

# Enable 16 KB page size alignment for Android 15+ compatibility
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

# Fetch bgfx and its dependencies using FetchContent
include(FetchContent)

# Avoid building bgfx tools/examples/install targets to speed up compilation
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "Disable bgfx tools" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Disable bgfx examples" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "Disable bgfx installation" FORCE)

FetchContent_Declare(
    bx
    GIT_REPOSITORY https://github.com/bkaradzic/bx.git
    GIT_TAG        master
)
FetchContent_Declare(
    bimg
    GIT_REPOSITORY https://github.com/bkaradzic/bimg.git
    GIT_TAG        master
)
FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.git
    GIT_TAG        master
)
FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        master
)

FetchContent_GetProperties(bx)
if(NOT bx_POPULATED)
    FetchContent_Populate(bx)
endif()

FetchContent_GetProperties(bimg)
if(NOT bimg_POPULATED)
    FetchContent_Populate(bimg)
endif()

FetchContent_GetProperties(bgfx)
if(NOT bgfx_POPULATED)
    FetchContent_Populate(bgfx)
endif()

# Point bgfx.cmake to the fetched directories
set(BX_DIR "${bx_SOURCE_DIR}" CACHE PATH "Location of bx" FORCE)
set(BIMG_DIR "${bimg_SOURCE_DIR}" CACHE PATH "Location of bimg" FORCE)
set(BGFX_DIR "${bgfx_SOURCE_DIR}" CACHE PATH "Location of bgfx" FORCE)

# Make bgfx_cmake available to build bgfx, bx, and bimg targets
FetchContent_MakeAvailable(bgfx_cmake)

# Headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/protocol)
include_directories(${ARCORE_INCLUDE})
include_directories(${bx_SOURCE_DIR}/include)
include_directories(${bimg_SOURCE_DIR}/include)
include_directories(${bgfx_SOURCE_DIR}/include)

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
    TouchController.cpp
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
    bgfx
    arcore
    jnigraphics
    android
    log
    GLESv3
    EGL
)
