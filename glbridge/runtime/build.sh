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
    -c "$script_dir/d9wg_fvf.c" -o "$object_dir/d9wg_fvf.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_fpu.c" -o "$object_dir/d9wg_fpu.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_math.c" -o "$object_dir/d9wg_math.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_tnl_math.c" -o "$object_dir/d9wg_tnl_math.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_lighting.c" -o "$object_dir/d9wg_lighting.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_blend.c" -o "$object_dir/d9wg_blend.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_clip.c" -o "$object_dir/d9wg_clip.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_clip_draw.c" -o "$object_dir/d9wg_clip_draw.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_vertex.c" -o "$object_dir/d9wg_vertex.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_process.c" -o "$object_dir/d9wg_process.o"
"$compiler" -std=gnu99 -Os -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_transport.c" -o "$object_dir/d9wg_transport.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_window_state.c" -o "$object_dir/d9wg_window_state.o"
"$compiler" -std=gnu99 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_window_geometry.c" -o "$object_dir/d9wg_window_geometry.o"
"$compiler" -std=gnu99 -Os -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_window_probe.c" -o "$object_dir/d9wg_window_probe.o"
"$compiler" -std=gnu99 -Os -Wall -Wextra -Werror \
    -c "$script_dir/d9wg_window_watch.c" -o "$object_dir/d9wg_window_watch.o"
"$archiver" rcs "$output" "$object_dir/d9wg_runtime.o" "$object_dir/d9wg_fvf.o" "$object_dir/d9wg_fpu.o" "$object_dir/d9wg_math.o" "$object_dir/d9wg_tnl_math.o" "$object_dir/d9wg_lighting.o" "$object_dir/d9wg_vertex.o" "$object_dir/d9wg_blend.o" "$object_dir/d9wg_clip.o" "$object_dir/d9wg_clip_draw.o" "$object_dir/d9wg_process.o" "$object_dir/d9wg_transport.o" "$object_dir/d9wg_window_state.o" "$object_dir/d9wg_window_geometry.o" "$object_dir/d9wg_window_probe.o" "$object_dir/d9wg_window_watch.o"
