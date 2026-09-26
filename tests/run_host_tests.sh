#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/rogue-radar-host-tests.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
cxx=${CXX:-g++}

"$cxx" -std=c++11 -Wall -Wextra -Werror \
    "$repo_root/tests/signal_tracker_model_test.cpp" \
    -o "$tmp_dir/signal_tracker_model_test"
"$tmp_dir/signal_tracker_model_test"

"$cxx" -std=c++11 -Wall -Wextra -Werror \
    -I"$repo_root/tests/radio_stubs" \
    "$repo_root/tests/signal_tracker_radio_test.cpp" \
    -o "$tmp_dir/signal_tracker_radio_test"
"$tmp_dir/signal_tracker_radio_test"

"$cxx" -std=c++11 -Wall -Wextra -Werror \
    -I"$repo_root/tests/radio_stubs" \
    "$repo_root/tests/continuous_ble_scan_test.cpp" \
    -o "$tmp_dir/continuous_ble_scan_test"
"$tmp_dir/continuous_ble_scan_test"

"$cxx" -std=c++11 -Wall -Wextra -Werror \
    "$repo_root/tests/scan_session_model_test.cpp" \
    -o "$tmp_dir/scan_session_model_test"
"$tmp_dir/scan_session_model_test"

"$cxx" -std=c++11 -Wall -Wextra -Werror \
    "$repo_root/tests/cc1101_power_test.cpp" \
    -o "$tmp_dir/cc1101_power_test"
"$tmp_dir/cc1101_power_test"

for suite in known_device_store known_signal_model known_device_discovery; do
    "$cxx" -std=c++11 -Wall -Wextra -Werror \
        -I"$repo_root/tests/radio_stubs" \
        "$repo_root/tests/${suite}_test.cpp" -o "$tmp_dir/${suite}_test"
    "$tmp_dir/${suite}_test"
done
