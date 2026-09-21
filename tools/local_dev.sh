#!/usr/bin/env bash
# Development only: test/build locally, never flash hardware or publish artifacts.
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
action="${1:-check}"
case "$action" in check|build) ;; *) echo 'Usage: tools/local_dev.sh check|build [SDK directory]' >&2; exit 2;; esac
sdk="${2:-${IDF_PATH:-$project_dir/build/local-sdk/esp-idf}}"
if [[ ! -f "$sdk/export.sh" ]]; then
  echo 'Provide the installed ESP-IDF directory as the second argument.' >&2
  exit 1
fi
sdk="$(cd "$sdk" && pwd)"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$(dirname "$sdk")/tools}"
# Reuse the installed virtual environment even when macOS's system Python
# precedes the Python version used during setup.
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" ]]; then
  shopt -s nullglob
  environments=("$IDF_TOOLS_PATH"/python_env/idf*_env)
  shopt -u nullglob
  if [[ ${#environments[@]} -eq 1 ]]; then export IDF_PYTHON_ENV_PATH="${environments[0]}"; fi
fi
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]]; then export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"; fi
# IDF's activation script requires Python on PATH and does not support nounset.
set +u
. "$sdk/export.sh" >/dev/null
set -u
cd "$project_dir"
if [[ "$action" == check ]]; then
  bash tools/test.sh
  python3 tools/test_build_scope.py
  python3 tools/test_sdp_attributes.py
  python3 tools/test_acl_recovery.py
  if [[ -f tools/test_pairing_confirmation.py ]]; then python3 tools/test_pairing_confirmation.py; fi
else
  # Reuse the same build directory, SDK and compiler cache on every invocation.
  # Build only Board B; tools/build.sh enforces the SDK version and packages it.
  if command -v ccache >/dev/null 2>&1; then
    export IDF_CCACHE_ENABLE=1
  else
    export IDF_CCACHE_ENABLE=0
  fi
  export CCACHE_DIR="$project_dir/build/ccache"
  bash tools/build.sh car
  DASHBRIDGE_BUILD_ROLE=car python3 tools/test_call_config.py
  DASHBRIDGE_BUILD_ROLE=car python3 tools/test_sdp.py
fi
