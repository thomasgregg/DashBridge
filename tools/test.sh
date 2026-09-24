#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$project_dir/tools/check_architecture.py"
mkdir -p "$project_dir/build/tests"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/connections.cpp" \
  "$project_dir/tests/connections_test.cpp" -o "$project_dir/build/tests/connections_test"
"$project_dir/build/tests/connections_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  -I "$project_dir/firmware/adapters/phone/esp_ancs_adapter/include" \
  -I "$project_dir/firmware/protocols/dashlink_v2/include" \
  -I "$project_dir/firmware/protocols/obex/include" \
  -I "$project_dir/firmware/adapters/car/map_adapter/include" \
  -I "$project_dir/firmware/adapters/car/pbap_adapter/include" \
  -I "$project_dir/firmware/transport/dashlink_transport/include" \
  -I "$project_dir/firmware/platform/esp_runtime/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/contacts.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/messages.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/music.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/setup.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/text.cpp" \
  "$project_dir/firmware/protocols/obex/obex.cpp" \
  "$project_dir/firmware/adapters/car/map_adapter/map_adapter.cpp" \
  "$project_dir/firmware/adapters/car/pbap_adapter/pbap_adapter.cpp" \
  "$project_dir/firmware/adapters/phone/esp_ancs_adapter/ancs_codec.cpp" \
  "$project_dir/firmware/protocols/dashlink_v2/dashlink_v2.cpp" \
  "$project_dir/firmware/transport/dashlink_transport/dashlink_transport.cpp" \
  "$project_dir/tests/core_test.cpp" -o "$project_dir/build/tests/core_test"
"$project_dir/build/tests/core_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  -I "$project_dir/firmware/protocols/obex/include" \
  -I "$project_dir/firmware/protocols/calls_v3/include" \
  -I "$project_dir/firmware/protocols/contacts_v1/include" \
  -I "$project_dir/firmware/protocols/music_v1/include" \
  -I "$project_dir/firmware/protocols/dashlink_v2/include" \
  -I "$project_dir/firmware/adapters/dashbridge_adapter_api/include" \
  -I "$project_dir/firmware/adapters/calls/esp_hfp_adapter/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/calls.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/messages.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/music.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/setup.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/text.cpp" \
  "$project_dir/firmware/protocols/obex/obex.cpp" \
  "$project_dir/firmware/protocols/dashlink_v2/dashlink_v2.cpp" \
  "$project_dir/tests/call_test.cpp" -o "$project_dir/build/tests/call_test"
"$project_dir/build/tests/call_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  -I "$project_dir/firmware/protocols/setup_gatt_v1/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/setup.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/text.cpp" \
  "$project_dir/firmware/protocols/setup_gatt_v1/setup_gatt_v1.cpp" \
  "$project_dir/tests/setup_test.cpp" -o "$project_dir/build/tests/setup_test"
"$project_dir/build/tests/setup_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/messages.cpp" \
  "$project_dir/firmware/core/dashbridge_domain_core/text.cpp" \
  "$project_dir/tests/messages_test.cpp" -o "$project_dir/build/tests/messages_test"
"$project_dir/build/tests/messages_test"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$project_dir/firmware/core/dashbridge_domain_core/include" \
  "$project_dir/firmware/core/dashbridge_domain_core/music.cpp" \
  "$project_dir/tests/music_test.cpp" -o "$project_dir/build/tests/music_test"
"$project_dir/build/tests/music_test"
python3 "$project_dir/tools/test_audio_replay.py"
python3 "$project_dir/tools/test_audio_control.py"

python3 "$project_dir/tools/test_reconnect.py"
python3 "$project_dir/tools/test_reconnect_replay.py"
python3 "$project_dir/tools/test_sdp_trace.py"
python3 "$project_dir/tools/test_connection_trace.py"
