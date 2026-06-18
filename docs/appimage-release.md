# AppImage release checklist

Use this checklist from a Linux build environment with the project dependencies
installed. For broad compatibility, build on the oldest distro version you want
to support; the current devcontainer is Ubuntu 24.04, so artifacts produced
there should be treated as Ubuntu 24.04-or-newer unless tested otherwise.

## 1. Pick the release version

The AppImage version defaults to the CMake project version in `CMakeLists.txt`.
Override it for a one-off build with:

```sh
VERSION=0.1.0 ./scripts/make_appimage.sh
```

## 2. Build, test, bundle, and package

```sh
./scripts/make_appimage.sh
```

The script configures `build-release` if needed, builds the project, runs the
CTest suite, bundles shared libraries and Metavision HAL plugins, downloads
`appimagetool` if needed, and writes the release artifact to:

```text
build-release/dist/Mustard-<version>-x86_64.AppImage
```

If only AppImage metadata changed and the tests have already passed, repack with:

```sh
SKIP_TESTS=1 ./scripts/make_appimage.sh
```

You can also invoke the packaging flow through CMake:

```sh
cmake --build build-release --target appimage
```

To build against an older Ubuntu userspace, use the release Docker images:

```sh
docker build -f packaging/docker/ubuntu22.Dockerfile -t mustard-appimage:ubuntu22 .
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu22 \
  mustard-appimage:ubuntu22 ./scripts/make_appimage.sh
```

Ubuntu 20.04 is also available as `packaging/docker/ubuntu20.Dockerfile`, but
OpenEB 5.x no longer officially supports Ubuntu 20.04, so treat focal builds as
best effort until they pass target-system testing.

## 3. Verify the artifact

Run these checks before publishing:

```sh
ctest --test-dir build-release --output-on-failure
file build-release/dist/Mustard-*-x86_64.AppImage
sha256sum build-release/dist/Mustard-*-x86_64.AppImage
```

On a Linux desktop, launch the AppImage and verify the expected workflows:

```sh
chmod +x build-release/dist/Mustard-*-x86_64.AppImage
./build-release/dist/Mustard-*-x86_64.AppImage
```

At minimum, open the IIT datalog sample, load an image sequence, and load a
Prophesee RAW file if that support is part of the release claim.

## 4. FUSE troubleshooting

If launching the AppImage prints `error loading libfuse.so.2`, the system is
missing the FUSE 2 compatibility library used by the AppImage runtime.

On Ubuntu 24.04 or newer:

```sh
sudo apt install libfuse2t64
```

On Ubuntu 22.04 or 23.x:

```sh
sudo apt install libfuse2
```

Do not install the `fuse` package on recent Ubuntu releases just to fix this;
that can remove FUSE 3 packages used by the desktop.

If installing FUSE is not possible, run the AppImage in extract-and-run mode:

```sh
./build-release/dist/Mustard-*-x86_64.AppImage --appimage-extract-and-run
```

For a persistent extracted copy:

```sh
./build-release/dist/Mustard-*-x86_64.AppImage --appimage-extract
./squashfs-root/AppRun
```

If launching prints `GLFW error 65542: GLX: Failed to load GLX`, first rebuild
with the current bundler. The AppImage must not carry `libGLX`, `libGLdispatch`,
`libEGL`, `libX11`, or `libxcb` libraries from the build container; those are
provided by the target machine's graphics stack.

On Ubuntu targets using Mesa, make sure the host runtime GLX packages are
installed:

```sh
sudo apt install libgl1 libglx0 libglx-mesa0 libgl1-mesa-dri mesa-utils
glxinfo -B
```

For a headless container smoke test, use Xvfb and extract-and-run mode:

```sh
timeout 8 xvfb-run -a -s "-screen 0 1280x720x24" \
  ./build-release/dist/Mustard-*-x86_64.AppImage --appimage-extract-and-run
```

A timeout with no GLFW error means the app launched and stayed open. To run
interactively inside Docker, pass through a real X11/Wayland display and, for
hardware rendering, the host GPU device.

## 5. Publish

Attach the AppImage and its SHA-256 checksum to the release. Include:

- the release version and commit SHA
- the Linux distro used to build it
- the minimum distro version you tested
- known requirements, especially GPU/OpenGL and display server expectations
