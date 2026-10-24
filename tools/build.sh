#!/usr/bin/env bash
# Full-source build: Linux host tests or Windows x64 cross using a user-supplied Zig compiler.
# No system settings changed. No source downloads unless HX_ALLOW_FETCH=1.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mode="${1:-linux}"
case "$mode" in
  linux)
    cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DHX_PREFER_SYSTEM_LIBS=ON -DHX_ALLOW_FETCH=${HX_ALLOW_FETCH:-OFF}
    cmake --build build/linux --parallel "${JOBS:-4}"
    ctest --test-dir build/linux --output-on-failure
    ;;
  windows)
    if [[ -z "${ZIG:-}" ]] || [[ ! -x "$ZIG" ]]; then
      echo "Set ZIG=/absolute/path/to/zig (0.17+) for Windows cross compilation" >&2
      exit 2
    fi
    export HX_ZIG="$ZIG"
    cmake -S . -B build/win-cross -G "Unix Makefiles" \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER="$PWD/tools/zig-cc.sh" \
      -DCMAKE_CXX_COMPILER="$PWD/tools/zig-cxx.sh" \
      -DCMAKE_BUILD_TYPE=Release \
      -DHX_PREFER_SYSTEM_LIBS=OFF -DHX_ALLOW_FETCH=${HX_ALLOW_FETCH:-OFF}
    cmake --build build/win-cross --parallel "${JOBS:-4}"
    file build/win-cross/hx.exe build/win-cross/hx-tune.exe
    echo "Windows EXEs generated; MUST run selftest on a real Windows machine."
    ;;
  *) echo "usage: tools/build.sh linux|windows";exit 2;;
esac
