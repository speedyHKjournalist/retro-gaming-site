#!/bin/sh
# The guest's opengl32.dll for Windows XP, without a C runtime. Drop it
# beside the guest game's executable.
#
#   build.sh [out.dll]              the shipping DLL (default: opengl32.dll here)
#   build.sh --diagnostic [out.dll] the trace-enabled DLL
#                                   (default: opengl32-diagnostic.dll here)
#
# The diagnostic DLL is deployed as opengl32.dll too, so that the Windows
# OpenGL loader selects it. It writes opengl32_trace_<pid>.log beside the
# executable, then beside the loaded DLL, or finally in %TEMP%, with no guest
# environment variable set. Per-call detail is capped per frame; see the
# diagnostic section of README.md for the knobs.
#
# CC and OBJDUMP select the mingw tools.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${CC:-i686-w64-mingw32-gcc}
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}

usage() {
    printf '%s\n' "usage: $0 [--diagnostic] [out.dll]"
}

mode=dll
case ${1-} in
    --diagnostic) mode=diagnostic; shift ;;
    -h|--help) usage; exit 0 ;;
    -*) usage >&2; exit 2 ;;
esac
if [ $# -gt 1 ]; then
    usage >&2
    exit 2
fi

# build_dll out.dll [compiler flags]
build_dll() {
    output=$1
    shift
    # -Wno-cast-function-type: wglGetProcAddress returns every entry point as
    # PROC, which is exactly the cast the warning describes. It is the ABI,
    # not a bug.
    "$compiler" -shared -std=gnu99 -Os -s -nostdlib "$@" \
        -Wall -Wextra -Wno-cast-function-type -Werror \
        -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -Wl,--kill-at \
        -o "$output" \
        "$script_dir/opengl32_proxy.c" "$script_dir/opengl32.def" \
        -luser32 -lgdi32 -lkernel32

    # The guest has no C runtime to resolve such an import against.
    imports=$("$objdump" -p "$output" | sed -n 's/^[[:space:]]*DLL Name: //p')
    if printf '%s\n' "$imports" | grep -Eiq \
            '(msvcrt|ucrt|libgcc|libstdc\+\+|api-ms-win-crt)'; then
        printf '%s\n' "Unexpected runtime import in $output:" >&2
        printf '%s\n' "$imports" >&2
        exit 1
    fi

    export_count=$(
        "$objdump" -p "$output" \
            | sed -n '/^\[Ordinal\/Name Pointer\] Table/,/^$/p' \
            | grep -Ec '^[[:space:]]*\[[[:space:]]*[0-9]+'
    )
    if [ "$export_count" -lt 831 ]; then
        printf '%s\n' "Export ABI is incomplete: $export_count of 831 entries" >&2
        exit 1
    fi
}

case $mode in
    dll)
        output=${1:-"$script_dir/opengl32.dll"}
        build_dll "$output"
        printf '%s\n' "Built $output" "$imports" "$export_count GL/WGL exports"
        ;;
    diagnostic)
        output=${1:-"$script_dir/opengl32-diagnostic.dll"}
        python3 "$script_dir/../tools/gen_gl_opcode_names.py" >/dev/null
        build_dll "$output" -DV86GL_DIAGNOSTIC_TRACE=1
        printf '%s\n' "Built diagnostic $output" "$imports" \
            "$export_count GL/WGL exports" \
            "Deploy as opengl32.dll beside the actual game executable (not beside its launcher)."
        ;;
esac
