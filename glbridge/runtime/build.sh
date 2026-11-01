#!/bin/sh
# Build the private, CRT-free C mechanics shared by D3D9, D3D8 and DDraw.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${CC:-i686-w64-mingw32-gcc}
archiver=${AR:-i686-w64-mingw32-ar}
output=${1:?usage: build.sh output.a}
object_dir=$(mktemp -d "${TMPDIR:-/tmp}/d9wg-runtime.XXXXXX")
trap 'rm -rf "$object_dir"' EXIT HUP INT TERM
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_runtime.c" -o "$object_dir/d9wg_runtime.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_process.c" -o "$object_dir/d9wg_process.o"
"$archiver" rcs "$output" "$object_dir/d9wg_runtime.o" "$object_dir/d9wg_process.o"
