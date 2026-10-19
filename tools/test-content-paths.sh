#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/content-path-tests"
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror -I "$root/src" \
    "$root/tests/content_paths_smoke.cpp" -o "$root/build/content-path-tests/content-paths"
"$root/build/content-path-tests/content-paths"
