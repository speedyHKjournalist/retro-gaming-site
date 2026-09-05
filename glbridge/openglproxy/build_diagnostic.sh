#!/bin/sh
# Build the trace-enabled proxy. Deploy the result as opengl32.dll in the
# guest; it writes opengl32_trace_<pid>.log beside the executable, then beside
# the loaded DLL, or finally in %TEMP%. The result still must be renamed to
# opengl32.dll when deployed so the Windows OpenGL loader selects it.
#
# It traces with no guest environment variable set. Per-call detail is capped
# per frame; see the diagnostic section of README.md for the knobs.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${1:-"$script_dir/opengl32-diagnostic.dll"}
compiler=${CC:-i686-w64-mingw32-gcc}

python3 "$script_dir/../tools/gen_gl_opcode_names.py" >/dev/null

"$compiler" -shared -std=gnu99 -Os -s -nostdlib \
    -Wall -Wextra -Wno-cast-function-type -Werror \
    -DV86GL_DIAGNOSTIC_TRACE=1 \
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

printf '%s\n' "Built diagnostic $output"
printf '%s\n' "$imports"
printf '%s\n' "$export_count GL/WGL exports"
printf '%s\n' "Deploy as opengl32.dll beside the actual game executable (not beside its launcher)."
