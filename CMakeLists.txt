cmake_minimum_required(VERSION 3.21)
project(mustard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Third-party dependencies via FetchContent
include(cmake/FetchDependencies.cmake)

# ---------------------------------------------------------------------------
# FFmpeg — system libraries (Ubuntu: libavcodec-dev etc.)
# .pc files may be empty on some Ubuntu images; use find_library directly.
# ---------------------------------------------------------------------------
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    PATHS /usr/include /usr/include/x86_64-linux-gnu
    REQUIRED)
find_library(AVCODEC_LIB  avcodec  REQUIRED)
find_library(AVFORMAT_LIB avformat REQUIRED)
find_library(AVUTIL_LIB   avutil   REQUIRED)
find_library(SWSCALE_LIB  swscale  REQUIRED)

# ---------------------------------------------------------------------------
# OpenEB / Metavision SDK — Prophesee RAW event decoding
# ---------------------------------------------------------------------------
find_package(MetavisionSDK 5.2.0 REQUIRED COMPONENTS stream)

# OpenEB is installed under /usr/local in the devcontainer.  Use a transitive
# build rpath so Metavision's indirect shared-library dependencies are found.
add_link_options(-Wl,--disable-new-dtags)
set(CMAKE_BUILD_RPATH "/usr/local/lib")

# ---------------------------------------------------------------------------
# glad — vendored pre-generated for OpenGL 3.3 core (no Python required)
# ---------------------------------------------------------------------------
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)
# glad.c needs to know the GL version at compile time
target_compile_definitions(glad PRIVATE GLAD_GL_VERSION_MAJOR=3 GLAD_GL_VERSION_MINOR=3)

# ---------------------------------------------------------------------------
# mustard core library (data + core, no OpenGL)
# ---------------------------------------------------------------------------
add_library(mustard STATIC
    src/core/TimeController.cpp
    src/data/events/IITDatalogLoader.cpp
    src/data/events/IITDatalogStream.cpp
    src/data/events/PropheseeRawLoader.cpp
    src/data/events/PropheseeRawStream.cpp
    src/annotation/BoundingBox.cpp
    src/annotation/EyeTracking.cpp
    src/annotation/AnnotationStore.cpp
)
target_include_directories(mustard PUBLIC include)
# imgui is needed here so that annotation renderers can find imgui.h at compile time.
# PRIVATE keeps it out of mustard's public interface so pure-data test targets
# that don't reference annotation types are unaffected.
target_link_libraries(mustard PRIVATE imgui MetavisionSDK::stream)

# ---------------------------------------------------------------------------
# mustard_app — viewer executable
# ---------------------------------------------------------------------------
add_executable(mustard_app
    src/app/main.cpp
    src/app/App.cpp
    src/ui/ViewerPanel.cpp
    src/ui/DVSViewerPanel.cpp
    src/ui/RGBVideoPanel.cpp
    src/ui/ImageListPanel.cpp
)
target_include_directories(mustard_app PRIVATE ${FFMPEG_INCLUDE_DIR} third_party/stb)
target_link_libraries(mustard_app PRIVATE mustard imgui glfw glad ImGuiFileDialog
    ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
target_compile_definitions(mustard_app PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD2)
# In devcontainer many optional FFmpeg codec plugin libs are empty stubs.
# Allow unresolved transitive shared-library symbols at link time so the
# binary still builds; the missing codecs simply won't be available at runtime.
target_link_options(mustard_app PRIVATE -Wl,--allow-shlib-undefined)

add_custom_target(bundle_mustard
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/bundle_mustard.sh
            $<TARGET_FILE:mustard_app>
            ${CMAKE_BINARY_DIR}/dist/mustard
    DEPENDS mustard_app
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Bundling mustard_app into ${CMAKE_BINARY_DIR}/dist/mustard"
    VERBATIM
)

add_custom_target(appimage
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/make_appimage.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Mustard AppImage"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
