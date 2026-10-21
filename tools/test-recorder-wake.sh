#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=$(mktemp /tmp/driveclub-recorder-wake.XXXXXX)
trap 'rm -f "$binary"' EXIT
"${CXX:-c++}" -std=c++20 -O2 -pthread -I"$root/src" "$root/tests/recorder_wake_smoke.cpp" -o "$binary"
timeout 30 "$binary"
echo 'Recorder wake handoff and shutdown races: OK'
