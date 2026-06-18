#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <mustard_app_executable> <output_directory>" >&2
    exit 2
fi

source_exe="$1"
dest_dir="$2"

if [[ ! -x "$source_exe" ]]; then
    echo "Executable not found or not executable: $source_exe" >&2
    exit 1
fi

source_exe="$(readlink -f "$source_exe")"

rm -rf "$dest_dir"
mkdir -p "$dest_dir/lib/metavision/hal/plugins"
dest_dir="$(cd "$dest_dir" && pwd)"

cp -L "$source_exe" "$dest_dir/mustard_app.bin"
chmod 755 "$dest_dir/mustard_app.bin"

declare -A copied_libs=()
declare -a scan_queue=("$dest_dir/mustard_app.bin")

ldd_paths() {
    local file="$1"
    local output
    output="$(LD_LIBRARY_PATH="$dest_dir/lib:/usr/local/lib:${LD_LIBRARY_PATH:-}" ldd "$file")"
    if grep -q "not found" <<<"$output"; then
        echo "Unresolved shared libraries while scanning $file:" >&2
        grep "not found" <<<"$output" >&2
        return 1
    fi

    awk '
        /=> \// { print $3; next }
        /^\// { print $1; next }
    ' <<<"$output" | while read -r path; do
        [[ -n "$path" && -f "$path" ]] && printf '%s\n' "$path"
    done
}

copy_lib() {
    local src="$1"
    local name
    name="$(basename "$src")"

    case "$name" in
        ld-linux*.so*|linux-vdso*.so*) return 0 ;;
        libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*) return 0 ;;
        libresolv.so.*|libanl.so.*|libutil.so.*|libnsl.so.*|libcrypt.so.*) return 0 ;;
        libBrokenLocale.so.*|libSegFault.so|libthread_db.so.*) return 0 ;;
        libGL.so.*|libGLX.so.*|libGLdispatch.so.*|libOpenGL.so.*) return 0 ;;
        libEGL.so.*|libGLESv1_CM.so.*|libGLESv2.so.*|libglapi.so.*|libgbm.so.*) return 0 ;;
        libdrm.so.*|libdrm_*.so.*|libvulkan.so.*) return 0 ;;
        libX11.so.*|libX11-xcb.so.*|libXau.so.*|libXdmcp.so.*) return 0 ;;
        libXext.so.*|libXfixes.so.*|libXrender.so.*|libXrandr.so.*) return 0 ;;
        libXi.so.*|libXcursor.so.*|libXinerama.so.*|libXss.so.*) return 0 ;;
        libxcb*.so.*|libwayland-*.so.*|libxkbcommon.so.*) return 0 ;;
    esac

    if [[ -n "${copied_libs[$name]:-}" ]]; then
        return 0
    fi

    cp -L "$src" "$dest_dir/lib/$name"
    chmod 644 "$dest_dir/lib/$name"
    copied_libs[$name]=1
    scan_queue+=("$dest_dir/lib/$name")
}

copy_plugin() {
    local src="$1"
    local name
    name="$(basename "$src")"
    cp -L "$src" "$dest_dir/lib/metavision/hal/plugins/$name"
    chmod 644 "$dest_dir/lib/metavision/hal/plugins/$name"
    scan_queue+=("$dest_dir/lib/metavision/hal/plugins/$name")
}

while ((${#scan_queue[@]} > 0)); do
    current="${scan_queue[0]}"
    scan_queue=("${scan_queue[@]:1}")
    while read -r dep; do
        copy_lib "$dep"
    done < <(ldd_paths "$current")
done

if [[ -d /usr/local/lib/metavision/hal/plugins ]]; then
    while IFS= read -r plugin; do
        copy_plugin "$plugin"
    done < <(find /usr/local/lib/metavision/hal/plugins -maxdepth 1 -type f -name '*.so' | sort)
fi

while ((${#scan_queue[@]} > 0)); do
    current="${scan_queue[0]}"
    scan_queue=("${scan_queue[@]:1}")
    while read -r dep; do
        copy_lib "$dep"
    done < <(ldd_paths "$current")
done

cat > "$dest_dir/mustard_app" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$app_dir/lib:$app_dir/lib/metavision/hal/plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MV_HAL_PLUGIN_PATH="$app_dir/lib/metavision/hal/plugins${MV_HAL_PLUGIN_PATH:+:$MV_HAL_PLUGIN_PATH}"
export MV_HAL_PLUGIN_SEARCH_MODE="${MV_HAL_PLUGIN_SEARCH_MODE:-PLUGIN_PATH_ONLY}"

exec "$app_dir/mustard_app.bin" "$@"
EOF
chmod 755 "$dest_dir/mustard_app"

cat > "$dest_dir/README.txt" <<'EOF'
Run Mustard from this folder with:

  ./mustard_app

The launcher sets LD_LIBRARY_PATH and Metavision HAL plugin paths so the
bundled OpenEB/Metavision libraries can load Prophesee RAW files.
EOF

lib_count="$(find "$dest_dir/lib" -type f -name '*.so*' | wc -l)"
plugin_count="$(find "$dest_dir/lib/metavision/hal/plugins" -type f -name '*.so' 2>/dev/null | wc -l)"

echo "Bundled Mustard app:"
echo "  $dest_dir/mustard_app"
echo "  libraries: $lib_count"
echo "  Metavision plugins: $plugin_count"
