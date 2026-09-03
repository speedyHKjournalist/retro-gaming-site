#!/bin/sh
# The shipping opengl32.dll. Drop it beside the guest game's executable.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${1:-"$script_dir/opengl32.dll"}
compiler=${CC:-i686-w64-mingw32-gcc}

# -Wno-cast-function-type: wglGetProcAddress returns every entry point as PROC,
# which is exactly the cast the warning describes. It is the ABI, not a bug.
"$compiler" -shared -std=gnu99 -Os -s -nostdlib \
    -Wall -Wextra -Wno-cast-function-type -Werror \
    -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -Wl,--kill-at \
    -o "$output" \
    "$script_dir/opengl32_proxy.c" "$script_dir/opengl32.def" \
    -luser32 -lgdi32 -lkernel32

imports=$(
    i686-w64-mingw32-objdump -p "$output" \
        | sed -n 's/^[[:space:]]*DLL Name: //p'
)
if printf '%s\n' "$imports" | grep -Eiq '(msvcrt|ucrt|libgcc|libstdc\+\+)'; then
    printf '%s\n' "Unexpected runtime import in $output:" >&2
    printf '%s\n' "$imports" >&2
    exit 1
fi

export_count=$(
    i686-w64-mingw32-objdump -p "$output" \
        | sed -n '/^\[Ordinal\/Name Pointer\] Table/,/^$/p' \
        | grep -Ec '^[[:space:]]*\[[[:space:]]*[0-9]+'
)
if [ "$export_count" -lt 831 ]; then
    printf '%s\n' "Export ABI is incomplete: $export_count of 831 entries" >&2
    exit 1
fi

printf '%s\n' "Built $output"
printf '%s\n' "$imports"
printf '%s\n' "$export_count GL/WGL exports"
