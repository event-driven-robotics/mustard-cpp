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

On Ubuntu, install the common build dependencies:

```sh
sudo apt update
sudo apt install \
  build-essential cmake ninja-build git git-lfs pkg-config \
  libgl1-mesa-dev libglu1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```

Install MetavisionSDK/OpenEB 5.2.0 separately, or use the provided devcontainer
or release Dockerfiles, which build and install OpenEB for you.

Then configure, build, and test:

```sh
git clone git@github.com:Iaxama/mustard-cpp.git
cd mustard-cpp
git lfs install
git lfs pull

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

## Build an AppImage

The release packaging script configures a release build, runs tests, bundles the
application, downloads `appimagetool` if needed, and writes the AppImage to
`build-release/dist/`.

```sh
./scripts/make_appimage.sh
```

The generated artifact is named like:

```text
build-release/dist/Mustard-<version>-<linux>-x86_64.AppImage
```

To build in an older Ubuntu userspace for wider compatibility, use the release
Docker images:

```sh
docker build -f packaging/docker/ubuntu22.Dockerfile -t mustard-appimage:ubuntu22 .
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu22 \
  mustard-appimage:ubuntu22 ./scripts/make_appimage.sh
```

See `docs/appimage-release.md` for the full release checklist and AppImage
troubleshooting notes.
