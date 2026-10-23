#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${BUILD_DIR:-"$root/build/linux-release"}
output_dir=${OUTPUT_DIR:-"$root/dist"}
tools_dir=${TOOLS_DIR:-"$root/.tools"}
appdir="$build_dir/AppDir"
jobs=${BUILD_JOBS:-4}
version=${VERSION:-"linux-$(git -C "$root" rev-parse --short HEAD)"}

if [[ $(uname -m) != x86_64 ]]; then
    echo 'This AppImage target requires an x86_64 host.' >&2
    exit 1
fi
if [[ ! "$version" =~ ^[A-Za-z0-9._+-]+$ ]]; then
    echo 'VERSION must contain only letters, digits, dots, underscores, plus or minus.' >&2
    exit 1
fi
mkdir -p "$tools_dir" "$output_dir"

fetch_tool() {
    local name=$1 url=$2 digest=$3
    if [[ ! -f "$tools_dir/$name" ]]; then
        curl --fail --location --retry 3 "$url" -o "$tools_dir/$name"
    fi
    printf '%s  %s\n' "$digest" "$tools_dir/$name" | sha256sum --check
    chmod +x "$tools_dir/$name"
}

# Hashes pin the tools even though upstream uses a moving release tag.
fetch_tool linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1
fetch_tool appimagetool-x86_64.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
    95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6

cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="${CC:-clang-19}" \
    -DCMAKE_CXX_COMPILER="${CXX:-clang++-19}" \
    -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
    -DENABLE_DISCORD_RPC=ON -DENABLE_UPDATER=OFF "$@"
cmake --build "$build_dir" --parallel "$jobs"

# Only create a fresh staging tree. Never package game data, firmware or user settings.
if [[ -e "$appdir" ]]; then
    echo "Staging directory already exists: $appdir. Move it aside before packaging again." >&2
    exit 1
fi
mkdir -p "$appdir/usr/share/doc/driveclub-pc" "$appdir/usr/share/metainfo" "$appdir/usr/lib"
cp "$root/packaging/linux/io.github.driveclub_pc.metainfo.xml" "$appdir/usr/share/metainfo/"
cp "$root/LICENSE" "$root/README.md" "$root/README-UPSTREAM.md" \
    "$root/README-GR2fork.md" "$root/CHANGELOG.md" "$appdir/usr/share/doc/driveclub-pc/"
cp -r "$root/LICENSES" "$appdir/usr/share/doc/driveclub-pc/"
cp -r "$root/driveclub-pc" "$appdir/usr/share/doc/driveclub-pc/"
cp -r "$root/packaging/discord" "$appdir/usr/share/doc/driveclub-pc/discord-artwork"
while IFS= read -r -d '' notice; do
    relative=${notice#"$root/"}
    destination="$appdir/usr/share/doc/driveclub-pc/third-party/$relative"
    mkdir -p "$(dirname -- "$destination")"
    cp "$notice" "$destination"
done < <(find "$root/externals" -type f \
    \( -iname 'license*' -o -iname 'copying*' -o -iname 'notice*' \) -print0)

export APPIMAGE_EXTRACT_AND_RUN=1
"$tools_dir/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --executable "$build_dir/Driveclub-PC" \
    --desktop-file "$root/packaging/linux/io.github.driveclub_pc.desktop" \
    --icon-file "$root/packaging/linux/driveclub-pc.svg" \
    --custom-apprun "$root/packaging/linux/AppRun"

# Bundle the C++ runtime used by this build. Graphics drivers and glibc remain host-provided.
cxx_runtime=$("${CXX:-clang++-19}" -print-file-name=libstdc++.so.6)
"$tools_dir/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --library "$cxx_runtime"

export ARCH=x86_64 VERSION="$version"
"$tools_dir/appimagetool-x86_64.AppImage" \
    --mksquashfs-opt -processors --mksquashfs-opt "$jobs" "$appdir" \
    "$output_dir/Driveclub-PC-$version-x86_64.AppImage"
(
    cd "$output_dir"
    sha256sum "Driveclub-PC-$version-x86_64.AppImage" > \
        "Driveclub-PC-$version-x86_64.AppImage.sha256"
)
