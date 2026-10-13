#!/usr/bin/env bash
# Reads changed paths from stdin and prints the sources clang-tidy has to check:
# the changed sources and the sources that include a changed header, or every
# source if the lint or build configuration changed.
set -euo pipefail

sources() { find libs apps tests -name '*.cpp'; }

changed=$(cat)
if grep -qE '(^|/)(\.clang-tidy|CMakeLists\.txt|CMakePresets\.json|ci\.yml)$|^cmake/|^scripts/lint-scope\.sh$' <<< "$changed"; then
  sources
  exit
fi

headers=$(grep -E '^(libs|apps|tests)/.*\.hpp$' <<< "$changed" | xargs -r -n1 basename || true)
{
  grep -E '^(libs|apps|tests)/.*\.cpp$' <<< "$changed" || true
  for header in $headers; do
    sources | xargs grep -lF "\"$header\"" || true
  done
} | sort -u
