#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
if [[ $# -ne 3 ]]; then
    printf 'Usage: %s <Dolphin 26.08.2 checkout> <build directory> <staging directory>\n' "$0" >&2
    exit 2
fi
source_dir=$(realpath "$1")
build_dir=$(realpath -m "$2")
stage_dir=$(realpath -m "$3")
integration_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
expected_revision=f19f070e7dc140213a2c6dd5d11f26122c89e1a0
actual_revision=$(git -C "$source_dir" rev-parse HEAD)
if [[ "$actual_revision" != "$expected_revision" ]]; then
    printf 'Expected Dolphin 26.08.2 revision %s, found %s.\n' "$expected_revision" "$actual_revision" >&2
    exit 1
fi
patch_file="$integration_dir/0001-publish-open-window-locations.patch"
if git -C "$source_dir" apply --reverse --check "$patch_file" 2>/dev/null; then
    printf 'Dolphin location integration is already applied.\n'
else
    if [[ -n "$(git -C "$source_dir" status --porcelain --untracked-files=no)" ]]; then
        printf 'The source checkout has local changes; use a separate clean checkout.\n' >&2
        exit 1
    fi
    git -C "$source_dir" apply --check "$patch_file"
    git -C "$source_dir" apply "$patch_file"
fi
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
DESTDIR="$stage_dir" cmake --install "$build_dir"
printf 'Patched Dolphin is staged under %s/usr. No system package was replaced.\n' "$stage_dir"
