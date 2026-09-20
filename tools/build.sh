#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
selected="$(python3 "$project_dir/tools/build_scope.py" "${1:-auto}")"
if [[ -z "$selected" ]]; then
  echo 'Both firmware images are current; nothing to build.'
  exit 0
fi
read -r -a roles <<< "$selected"
if ! command -v idf.py >/dev/null 2>&1; then
  echo 'Activate ESP-IDF v5.5.1 using its export.sh first.' >&2
  exit 1
fi
idf_version="$(idf.py --version)"
if [[ "$idf_version" != 'ESP-IDF v5.5.1' ]]; then
  echo "Expected ESP-IDF v5.5.1; found: $idf_version" >&2
  exit 1
fi
cd "$project_dir/firmware"
for role in "${roles[@]}"; do
  idf.py -B "$project_dir/build/$role" \
    -D "SDKCONFIG=$project_dir/build/$role/sdkconfig" \
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$role" build
done
python "$project_dir/tools/package.py" "${roles[@]}"
