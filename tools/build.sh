#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v idf.py >/dev/null 2>&1; then
  echo 'Activate ESP-IDF v5.5.1 using its export.sh first.' >&2
  exit 1
fi
idf_version="$(idf.py --version)"
if [[ "$idf_version" != 'ESP-IDF v5.5.1' ]]; then
  echo "Expected ESP-IDF v5.5.1; found: $idf_version" >&2
  exit 1
fi
roles=(phone car)
if [[ $# -gt 0 ]]; then
  case "$1" in phone|car) roles=("$1");; *) echo 'Usage: build.sh [phone|car]' >&2; exit 2;; esac
fi
cd "$project_dir/firmware"
for role in "${roles[@]}"; do
  idf.py -B "$project_dir/build/$role" \
    -D "SDKCONFIG=$project_dir/build/$role/sdkconfig" \
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$role" build
done
python "$project_dir/tools/package.py" "${roles[@]}"
