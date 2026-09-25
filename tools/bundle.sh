#!/usr/bin/env bash
# Windhawk compiles a single .wh.cpp, but the project keeps one responsibility
# per file under src/. This joins them, in dependency order, into the file you
# paste into the Windhawk editor. Local includes and #pragma once are dropped
# because everything ends up in one translation unit.
set -euo pipefail
cd "$(dirname "$0")/.."

readonly OUTPUT=ai-usage-pill.wh.cpp
readonly SOURCES=(src/mod_metadata.cpp src/common.h src/hidden_process_runner.h
  src/usage_report_parser.h src/usage_poller.h src/brand_logo_paths.h src/brand_logo.h
  src/pill_painter.h src/pill_window.h
  src/pill_app.h src/mod_entry.cpp)

for source in "${SOURCES[@]}"; do
  [[ "$source" == src/mod_metadata.cpp ]] || printf '\n// ---- %s ----\n' "$source"
  grep -v -E '^#pragma once|^#include "' "$source"
done > "$OUTPUT"
echo "bundled $OUTPUT ($(wc -l < "$OUTPUT") lines)"
