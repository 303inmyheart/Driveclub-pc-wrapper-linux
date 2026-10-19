#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
paths=()
while read -r key path; do
    # KosmicKrisp and its Mesa source tree are only used on macOS.
    [[ "$path" == externals/mesa-kosmickrisp ]] || paths+=("$path")
done < <(git config --file .gitmodules --get-regexp '\.path$')
git submodule update --init --jobs "${BUILD_JOBS:-4}" -- "${paths[@]}"
# Only these nested modules are needed by the Linux application build. Do not fetch
# unrelated documentation themes, third-party test suites or the macOS driver.
git -C externals/sirit submodule update --init --recursive
git -C externals/zydis submodule update --init --recursive
git -C externals/freetype submodule update --init --recursive
