#!/bin/bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
lvgl_dir=${1:-${LVGL_DIR:-}}
if [[ -z "$lvgl_dir" || ! -d "$lvgl_dir/src" ]]; then
    echo "usage: $0 /path/to/pinned/lvgl" >&2
    exit 64
fi

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/rogue-radar-lvgl-tests.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
ulimit -c 0

awk '
    /#endif.*LV_CONF_H/ {
        print "#undef LV_MEM_SIZE"
        print "#define LV_MEM_SIZE (64 * 1024U)"
        print "#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>"
        print "#define LV_ASSERT_HANDLER abort();"
    }
    { print }
' "$repo_root/lv_conf.h" >"$tmp_dir/lv_conf.h"

mkdir -p "$tmp_dir/obj"
objects=()
while IFS= read -r -d '' source; do
    relative=${source#"$lvgl_dir/src/"}
    object="$tmp_dir/obj/${relative%.c}.o"
    mkdir -p "$(dirname "$object")"
    "${CC:-gcc}" -std=c11 -O0 -I"$tmp_dir" -I"$lvgl_dir" \
        -DLV_CONF_INCLUDE_SIMPLE -c "$source" -o "$object"
    objects+=("$object")
done < <(find "$lvgl_dir/src" -name '*.c' -print0)
"${AR:-ar}" rcs "$tmp_dir/liblvgl.a" "${objects[@]}"

"${CXX:-g++}" -std=c++11 -O0 -Wall -Wextra -Werror \
    -I"$tmp_dir" -I"$lvgl_dir" -DLV_CONF_INCLUDE_SIMPLE \
    "$repo_root/tests/lvgl_keyboard_lifecycle_test.cpp" \
    "$tmp_dir/liblvgl.a" -lm -o "$tmp_dir/lvgl_keyboard_lifecycle_test"

"$tmp_dir/lvgl_keyboard_lifecycle_test" helper
"$tmp_dir/lvgl_keyboard_lifecycle_test" fixed

set +e
"$tmp_dir/lvgl_keyboard_lifecycle_test" old
old_status=$?
set -e
if [[ $old_status -ne 134 ]]; then
    echo "FAIL: original cancel recreation returned $old_status; expected SIGABRT (134)" >&2
    exit 1
fi
echo "PASS original cancel recreation reproduced allocator failure (exit $old_status)"
