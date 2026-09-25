#!/usr/bin/env bash
# Builds and runs the self-test with the Windhawk clang, then compiles the mod
# DLL with the same flags the Windhawk editor uses (taken from its extension.js).
#   tests/build.sh            selftest + mod compile check
#   tests/build.sh preview 20 also shows the pill for 20 seconds
set -euo pipefail

readonly WINDHAWK_DIR="/mnt/c/Program Files/Windhawk"
readonly CLANG="$WINDHAWK_DIR/Compiler/bin/clang++.exe"
readonly PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
readonly OUT_DIR="/mnt/c/Users/Public/windhawk-ai-usage-build"
readonly COMMON_FLAGS=(-std=c++23 -O2 -DUNICODE -D_UNICODE -DWINVER=0x0A00
  -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008
  -D__USE_MINGW_ANSI_STDIO=0 -target x86_64-w64-mingw32 -include windhawk_api.h)
readonly MOD_LIBS=(-lgdiplus -lgdi32 -lshell32 -lruntimeobject -lole32 -loleaut32)

win_path() { wslpath -w "$1"; }

build_test_exe() {
  "$CLANG" "${COMMON_FLAGS[@]}" -DWH_MOD -DWH_EDITING "$(win_path "$PROJECT_DIR/tests/pill_test.cpp")" \
    "${MOD_LIBS[@]}" -static -o "$(win_path "$OUT_DIR/pill_test.exe")"
}

build_mod_dll() {
  local lib_path
  lib_path="$(find "$WINDHAWK_DIR/Engine" -path '*/64/windhawk.lib' | head -1)"
  "$CLANG" "${COMMON_FLAGS[@]}" -shared -DWH_MOD '-DWH_MOD_ID=L"ai-usage-pill"' \
    '-DWH_MOD_VERSION=L"1.0.0"' "$(win_path "$lib_path")" \
    -x c++ "$(win_path "$PROJECT_DIR/ai-usage-pill.wh.cpp")" -Wl,--export-all-symbols \
    "${MOD_LIBS[@]}" -o "$(win_path "$OUT_DIR/ai-usage-pill.dll")"
}

"$PROJECT_DIR/tools/bundle.sh"
mkdir -p "$OUT_DIR"
cd "$WINDHAWK_DIR/Compiler"
build_test_exe
"$OUT_DIR/pill_test.exe" selftest "$(win_path "$PROJECT_DIR/tests/fixture-usage.json")"
build_mod_dll
echo "mod DLL compiled: $OUT_DIR/ai-usage-pill.dll"
if [[ "${1:-}" == "preview" ]]; then
  "$OUT_DIR/pill_test.exe" preview "${2:-15}"
fi
