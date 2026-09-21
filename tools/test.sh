#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$project_dir/build/tests"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/components/bridge_core/include" \
  "$project_dir/firmware/components/bridge_core/bridge_core.cpp" \
  "$project_dir/tests/core_test.cpp" -o "$project_dir/build/tests/core_test"
"$project_dir/build/tests/core_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/components/bridge_core/include" -I "$project_dir/firmware/main" \
  "$project_dir/firmware/components/bridge_core/bridge_core.cpp" \
  "$project_dir/tests/call_test.cpp" -o "$project_dir/build/tests/call_test"
"$project_dir/build/tests/call_test"

python3 "$project_dir/tools/test_reconnect.py"
python3 "$project_dir/tools/test_reconnect_replay.py"
python3 "$project_dir/tools/test_sdp_trace.py"
python3 "$project_dir/tools/test_connection_trace.py"
