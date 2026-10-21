#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=$(mktemp /tmp/driveclub-topology-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
"${CXX:-c++}" -std=c++20 -O2 -I"$root/src" "$root/tests/linux_cpu_topology_smoke.cpp" -o "$binary"
"$binary"
