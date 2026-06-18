#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"

build_dir="${BUILD_DIR:-$repo_dir/build-release}"
dist_dir="$build_dir/dist"
bundle_dir="$dist_dir/mustard"
appdir="$dist_dir/Mustard.AppDir"
tool_dir="$dist_dir/tools"

version="${VERSION:-}"
if [[ -z "$version" ]]; then
    version="$(sed -n 's/^project(mustard VERSION \([^ )]*\).*/\1/p' "$repo_dir/CMakeLists.txt" | head -n 1)"
fi
version="${version:-0.0.0}"

linux_version="${LINUX_VERSION:-}"
if [[ -z "$linux_version" && -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    linux_id="${ID:-linux}"
    linux_version_id="${VERSION_ID:-}"
    if [[ -n "$linux_version_id" ]]; then
        linux_version="$linux_id-$linux_version_id"
    else
        linux_version="$linux_id"
    fi
fi
linux_version="${linux_version:-linux-$(uname -r)}"
linux_version="$(printf '%s' "$linux_version" | tr '[:upper:]' '[:lower:]' | tr -c '[:alnum:]._' '-')"
linux_version="${linux_version%-}"

machine="$(uname -m)"
case "$machine" in
    x86_64|amd64) arch="x86_64" ;;
    *)
        echo "Unsupported AppImage architecture for this script: $machine" >&2
        echo "Build on x86_64, or extend scripts/make_appimage.sh for this architecture." >&2
        exit 1
        ;;
esac

jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
fi

cmake --build "$build_dir" -j "$jobs"

if [[ "${SKIP_TESTS:-0}" != "1" ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi

cmake --build "$build_dir" --target bundle_mustard -j "$jobs"

rm -rf "$appdir"
mkdir -p "$appdir"
cp -a "$bundle_dir/." "$appdir/"
cp "$repo_dir/packaging/appimage/mustard.desktop" "$appdir/mustard.desktop"
cp "$repo_dir/packaging/appimage/mustard.png" "$appdir/mustard.png"

mapfile -t bundled_graphics_libs < <(
    find "$appdir/lib" -maxdepth 1 -type f \
        \( -name 'libGL*.so*' \
        -o -name 'libEGL*.so*' \
        -o -name 'libOpenGL*.so*' \
        -o -name 'libGLES*.so*' \
        -o -name 'libglapi*.so*' \
        -o -name 'libgbm*.so*' \
        -o -name 'libdrm*.so*' \
        -o -name 'libvulkan*.so*' \
        -o -name 'libX11*.so*' \
        -o -name 'libXau*.so*' \
        -o -name 'libXdmcp*.so*' \
        -o -name 'libXext*.so*' \
        -o -name 'libXfixes*.so*' \
        -o -name 'libXrender*.so*' \
        -o -name 'libXrandr*.so*' \
        -o -name 'libXi*.so*' \
        -o -name 'libXcursor*.so*' \
        -o -name 'libXinerama*.so*' \
        -o -name 'libXss*.so*' \
        -o -name 'libxcb*.so*' \
        -o -name 'libwayland-*.so*' \
        -o -name 'libxkbcommon*.so*' \) \
        -printf '%f\n' | sort
)
if ((${#bundled_graphics_libs[@]} > 0)); then
    echo "Refusing to package AppImage with host graphics stack libraries bundled:" >&2
    printf '  %s\n' "${bundled_graphics_libs[@]}" >&2
    echo "These must be provided by the target machine so GLX/EGL matches the display driver." >&2
    exit 1
fi

cat > "$appdir/AppRun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$app_dir/mustard_app" "$@"
EOF
chmod 755 "$appdir/AppRun"

mkdir -p "$tool_dir"
appimagetool="$tool_dir/appimagetool-$arch.AppImage"
if [[ ! -x "$appimagetool" ]]; then
    curl -fL \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$arch.AppImage" \
        -o "$appimagetool"
    chmod 755 "$appimagetool"
fi

output="$dist_dir/Mustard-$version-$linux_version-$arch.AppImage"
rm -f "$output"

ARCH="$arch" VERSION="$version" APPIMAGE_EXTRACT_AND_RUN=1 "$appimagetool" "$appdir" "$output"
chmod 755 "$output"

echo "Created AppImage:"
echo "  $output"
