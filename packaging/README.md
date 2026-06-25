# Packaging

This directory contains packaging assets and scripts for Windows bundles and
Linux AppImage releases.

## Windows bundle

To ship Mustard to another Windows machine, generate a bundle that contains
`mustard_app.exe`, FFmpeg DLLs, Metavision DLLs, and Metavision HAL plugins.
Run the bundling script from `x64 Native Tools Command Prompt for VS 2022` or
Developer PowerShell so `dumpbin.exe` is available.

For a Ninja build:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\scripts\bundle_windows.ps1 -BuildDir build-win -Configuration Release -OutputDir build-win\dist\mustard-windows
```

For a Visual Studio generator build:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\scripts\bundle_windows.ps1 -BuildDir build-win-vs -Configuration Release -OutputDir build-win-vs\dist\mustard-windows
```

If FFmpeg or Metavision were installed in non-standard locations, pass extra
runtime DLL directories explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\scripts\bundle_windows.ps1 -BuildDir build-win -VcpkgRoot C:\vcpkg -MetavisionRoot "C:\Program Files\Prophesee" -ExtraBinDirs "C:\path\to\other\dlls"
```

The generated bundle can be zipped and copied to another Windows machine.

Launch `mustard_app.exe` directly. If Metavision HAL plugins are not discovered,
or if you want to force Mustard to use the bundled plugin directory, launch
`mustard_app.bat` instead.

The bundling script prints the exact bundle directory and `mustard_app.exe` path
when it completes. The generated bundle also includes a short `README.txt` with
the same launch guidance.

## AppImage release

Build AppImages from a Linux environment with the project dependencies
installed. For broad compatibility, build on the oldest distro version you want
to support; artifacts produced in the current Ubuntu 24.04 devcontainer should
be treated as Ubuntu 24.04-or-newer unless tested otherwise.

The AppImage version defaults to the CMake project version in `CMakeLists.txt`.
Override it for a one-off build with:

```sh
VERSION=0.1.0 ./packaging/scripts/make_appimage.sh
```

From the repository root, build, test, bundle, and package with:

```sh
./packaging/scripts/make_appimage.sh
```

The script configures `build-release` if needed, builds the project, runs the
CTest suite, bundles shared libraries and Metavision HAL plugins, downloads
`appimagetool` if needed, and writes an artifact named like:

```text
build-release/dist/Mustard-<version>-<linux>-x86_64.AppImage
```

If only AppImage metadata changed and the tests have already passed, repack
with:

```sh
SKIP_TESTS=1 ./packaging/scripts/make_appimage.sh
```

You can also invoke the packaging flow through CMake:

```sh
cmake --build build-release --target appimage
```

## Docker release images

The Docker images provide older Ubuntu build environments for AppImage releases.
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
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu22 \
  mustard-appimage:ubuntu22 ./packaging/scripts/make_appimage.sh
```

For Ubuntu 20.04, switch the image and output directory:

```sh
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" -w /workspace \
  -e BUILD_DIR=/workspace/build-release-ubuntu20 \
  mustard-appimage:ubuntu20 ./packaging/scripts/make_appimage.sh
```

Ubuntu 20.04 note: OpenEB 5.x dropped official Ubuntu 20.04 support. The focal
image pins a newer CMake and disables Python bindings, but treat that image as
best effort and verify the produced AppImage on target systems.

## Verify the AppImage

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

## AppImage troubleshooting

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

## Publish

Attach the AppImage and its SHA-256 checksum to the release. Include:

- the release version and commit SHA
- the Linux distro used to build it
- the minimum distro version you tested
- known requirements, especially GPU/OpenGL and display server expectations
