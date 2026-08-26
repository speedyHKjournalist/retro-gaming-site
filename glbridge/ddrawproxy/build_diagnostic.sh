#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${1:-"$script_dir/ddraw-diagnostic.dll"}
compiler=${CC:-i686-w64-mingw32-gcc}

"$compiler" -shared -std=gnu99 -Os -s -nostdlib \
    -DDDWG_DIAGNOSTIC_TRACE=1 \
    -Wall -Wextra -Werror \
    -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -Wl,--kill-at \
    -o "$output" \
    "$script_dir/ddraw_proxy.c" "$script_dir/ddraw.def" \
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

printf '%s\n' "Built diagnostic $output"
printf '%s\n' "$imports"
