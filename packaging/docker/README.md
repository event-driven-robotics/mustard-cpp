# Release Docker images

These images provide older Ubuntu build environments for AppImage releases.
They install the native C++ toolchain, FFmpeg development libraries, OpenGL and
GLFW dependencies, AppImage validation helpers, and OpenEB/Metavision SDK.

Build the Ubuntu 22.04 image:

```sh
docker build -f packaging/docker/ubuntu22.Dockerfile -t mustard-appimage:ubuntu22 .
```

Build the Ubuntu 20.04 image:

```sh
docker build -f packaging/docker/ubuntu20.Dockerfile -t mustard-appimage:ubuntu20 .
```

Run a release build from your current checkout:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu22 \
  mustard-appimage:ubuntu22 \
  ./scripts/make_appimage.sh
```

For Ubuntu 20.04, switch the image and output directory:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu20 \
  mustard-appimage:ubuntu20 \
  ./scripts/make_appimage.sh
```


Ubuntu 20.04 note: OpenEB 5.x dropped official Ubuntu 20.04 support. The focal
image pins a newer CMake and disables Python bindings, but treat that image as
best effort and verify the produced AppImage on target systems.
