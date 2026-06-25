# Mustard

Mustard is a GUI for Multi Stream Annotation of Raw Data. It is designed for
reviewing synchronized raw sensor streams, visualizing event-camera data in
multiple forms, and creating annotations on top of events, images, and video.

The application can open individual files or scan folders for supported streams.
Loaded streams are shown as synchronized viewer panes with a shared playback
timeline, seek control, and recent-file menu.

## Supported data

- IIT datalog event streams (`.log`)
- Prophesee/OpenEB raw event streams (`.raw`)
- Image sequences from folders of `.png`, `.jpg`, or `.jpeg` files
- Video files decoded through FFmpeg, including `.mp4`, `.mkv`, and `.avi`

## Optional sample data

Sample files under `data_samples/` are stored with Git LFS. They are not needed
to configure, build, test, or run Mustard. If Git LFS is available on your
machine, download only the samples after cloning with:

```sh
git lfs pull --include="data_samples/**" --exclude=""
```

## GUI features

Event streams can be displayed with three representations:

- Histogram: accumulated ON/OFF activity in the selected time window
- Time Surface: the latest event per pixel, encoded by recency and polarity
- Ternary Image: positive, negative, and inactive pixels in a compact image view

The event viewer includes an accumulation-window slider so the same stream can
be inspected at different temporal scales.

Mustard also supports annotation overlays:

- Bounding boxes: drag directly on a viewer to create boxes
- Eye tracking: place and edit an eye model with gaze orientation, center, and
  radius
- Per-panel annotation export through the `Save Annotations` control

For eye tracking annotations, drag to set gaze orientation, hold `Shift` while
dragging to resize the radius, and hold `Ctrl` while dragging to move the eye
center.

## Download the AppImage

Linux users can download the prebuilt AppImage from the latest GitHub release:

https://github.com/Iaxama/mustard-cpp/releases/latest

Download the `Mustard-<version>-<linux>-x86_64.AppImage` asset, make it
executable, and launch it:

```sh
chmod +x Mustard-*-x86_64.AppImage
./Mustard-*-x86_64.AppImage
```

If your system reports a missing FUSE 2 library, install the compatibility
package for your distribution. On Ubuntu 24.04 or newer:

```sh
sudo apt install libfuse2t64
```

On Ubuntu 22.04 or 23.x:

```sh
sudo apt install libfuse2
```

If installing FUSE is not possible, run the AppImage in extract-and-run mode:

```sh
./Mustard-*-x86_64.AppImage --appimage-extract-and-run
```

## Build from source

Mustard is a C++17 CMake project. It uses FetchContent for several third-party
libraries, including Dear ImGui, GLFW, ImGuiFileDialog, and GoogleTest. System
dependencies include FFmpeg development libraries, OpenGL/windowing libraries,
and MetavisionSDK/OpenEB 5.2.0 with the `stream` component for Prophesee RAW
support.

Containerized environments are available: `.devcontainer/` provides a
development container, and `packaging/docker/` contains release Dockerfiles for
AppImage builds. These images build and install OpenEB for you.

### Linux

On Ubuntu, install the common build dependencies:

```sh
sudo apt update
sudo apt install \
  build-essential cmake ninja-build git pkg-config \
  libgl1-mesa-dev libglu1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```

Install MetavisionSDK/OpenEB 5.2.0 separately if you are building directly on
your host system.

Then configure, build, and test:

```sh
git clone git@github.com:Iaxama/mustard-cpp.git
cd mustard-cpp

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the GUI:

```sh
./build/mustard_app
```

Use `File > Open File...` to load a single supported file, or `File > Open
Folder...` to scan a dataset directory for multiple streams.

### Windows

The Windows build uses the same CMake project, but the native dependencies must
be discoverable by CMake. The recommended setup is Visual Studio 2022 with the
MSVC compiler, Ninja, vcpkg for FFmpeg, and a Windows installation of
MetavisionSDK/OpenEB 5.2.0 with the `stream` component.

Install the required tools:

- Visual Studio 2022 or Build Tools for Visual Studio 2022 with the `Desktop
  development with C++` workload
- CMake 3.21 or newer
- Ninja
- Git for Windows
- vcpkg
- MetavisionSDK/OpenEB 5.2.0 for Windows, including the `stream` component

If you build OpenEB from source, compiling `ALL_BUILD` is not enough to create
the default SDK install directory. You must either build the `INSTALL` target or
configure Mustard against the generated OpenEB build tree.

To install OpenEB into the default SDK location, open an Administrator command
prompt in the OpenEB build directory and run:

```bat
cmake --build . --config Release --target install
```

By default, OpenEB installs to:

```text
C:\Program Files\Prophesee
```

If you do not want to install into `Program Files`, reconfigure OpenEB with a
custom install prefix, then build the install target:

```bat
cmake .. -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\openeb\cmake\toolchains\vcpkg.cmake ^
  -DVCPKG_DIRECTORY=C:\vcpkg ^
  -DCMAKE_INSTALL_PREFIX=C:\openeb-install ^
  -DBUILD_TESTING=OFF

cmake --build . --config Release --parallel 4
cmake --build . --config Release --target install
```

The directory passed to Mustard as `MetavisionSDK_DIR` must be the directory
that contains `MetavisionSDKConfig.cmake`. To find it, run this in PowerShell:

```powershell
Get-ChildItem -Path "C:\Program Files\Prophesee", "C:\openeb-install", "C:\path\to\openeb\build" `
  -Filter MetavisionSDKConfig.cmake -Recurse -ErrorAction SilentlyContinue |
  Select-Object -ExpandProperty DirectoryName
```

Install FFmpeg through vcpkg. In a regular PowerShell or Command Prompt:

```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe install ffmpeg:x64-windows
```

Open `x64 Native Tools Command Prompt for VS 2022`. Using this prompt is
important because it puts the MSVC compiler and Windows SDK on `PATH`.

Clone the repository:

```bat
git clone git@github.com:Iaxama/mustard-cpp.git
cd mustard-cpp
```

Configure the Mustard build. Replace the `MetavisionSDK_DIR` value with the
directory that contains `MetavisionSDKConfig.cmake` on your machine:

```bat
cmake -S . -B build-win ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DMetavisionSDK_DIR="C:\Program Files\Prophesee\lib\cmake\MetavisionSDK"
```

If CMake cannot find MetavisionSDK, search your Metavision/OpenEB installation
for `MetavisionSDKConfig.cmake` and pass the folder containing that file as
`MetavisionSDK_DIR`.

Build and run the tests:

```bat
cmake --build build-win -j
ctest --test-dir build-win --output-on-failure
```

Run the GUI:

```bat
build-win\mustard_app.exe
```

If the executable starts but cannot find FFmpeg or Metavision DLLs, add the
corresponding `bin` directories to `PATH` before launching it. For example:

```bat
set PATH=C:\vcpkg\installed\x64-windows\bin;%PATH%
set PATH=C:\Program Files\Prophesee\bin;%PATH%
build-win\mustard_app.exe
```

If Windows reports that `metavision_sdk_stream.dll` is missing, first locate the
DLL:

```powershell
Get-ChildItem -Path "C:\Program Files\Prophesee", "C:\openeb-install", "C:\path\to\openeb\build" `
  -Filter metavision_sdk_stream.dll -Recurse -ErrorAction SilentlyContinue |
  Select-Object -ExpandProperty FullName
```

Then add the folder containing `metavision_sdk_stream.dll` to `PATH`, or copy
the required Metavision DLLs next to `mustard_app.exe`. When using an OpenEB
build tree instead of an installed SDK, the DLLs are usually under a
configuration-specific build output directory such as `bin\Release`; make sure
that directory matches the configuration used to build Mustard.

You can also configure with the Visual Studio generator instead of Ninja:

```bat
cmake -S . -B build-win-vs ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DMetavisionSDK_DIR="C:\Program Files\Prophesee\lib\cmake\MetavisionSDK"

cmake --build build-win-vs --config Release
ctest --test-dir build-win-vs -C Release --output-on-failure
build-win-vs\Release\mustard_app.exe
```
