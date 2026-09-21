#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"
sdk="${1:-${IDF_PATH:-}}"
if [[ -z "$sdk" || ! -f "$sdk/export.sh" ]]; then
    echo 'Usage: ./build-local.sh /absolute/path/to/esp-idf' >&2
    exit 1
fi
sdk="$(cd "$sdk" && pwd)"
if [[ "$(git -C "$sdk" rev-parse HEAD)" != b774170ff46c393eeb5e495ea37936038d3f4f4f ]]; then
    echo 'Expected ESP-IDF v5.5.5 commit b774170ff46c393eeb5e495ea37936038d3f4f4f.' >&2
    exit 1
fi

export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$(dirname "$sdk")/tools}"
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" ]]; then
    shopt -s nullglob
    environments=("$IDF_TOOLS_PATH"/python_env/idf*_env)
    shopt -u nullglob
    if [[ ${#environments[@]} -eq 1 ]]; then
        export IDF_PYTHON_ENV_PATH="${environments[0]}"
    fi
fi
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]]; then
    export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
fi
set +u
. "$sdk/export.sh" >/dev/null
set -u

idf.py -C "$project_dir" -B "$project_dir/build" \
    -D "SDKCONFIG=$project_dir/build/sdkconfig" \
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.repro" \
    build
