#!/bin/sh
# The guest's ddraw.dll (DirectDraw and Direct3D 1-7) for Windows XP,
# without a C runtime.
#
#   build.sh [out.dll]              the shipping DLL (default: ddraw.dll here)
#   build.sh --diagnostic [out.dll] with the trace file and exception log
#                                   (default: ddraw-diagnostic.dll here)
#   build.sh --tests [dir]          the XP smoke tests of ../sample, with the
#                                   shipping ddraw.dll beside them
#                                   (default: /private/tmp/ddraw-d3d7-smoke)
#
# CC, WINDRES and OBJDUMP select the mingw tools.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${CC:-i686-w64-mingw32-gcc}
windres=${WINDRES:-i686-w64-mingw32-windres}
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}

usage() {
    printf '%s\n' "usage: $0 [--diagnostic] [out.dll]" \
        "       $0 --tests [dir]"
}

mode=dll
case ${1-} in
    --diagnostic) mode=diagnostic; shift ;;
    --tests) mode=tests; shift ;;
    -h|--help) usage; exit 0 ;;
    -*) usage >&2; exit 2 ;;
esac
if [ $# -gt 1 ]; then
    usage >&2
    exit 2
fi

resource_dir=$(mktemp -d "${TMPDIR:-/tmp}/ddraw-resource.XXXXXX")
resource_object="$resource_dir/ddraw_version.o"
trap 'rm -rf "$resource_dir"' EXIT HUP INT TERM

# The guest has no C runtime to resolve such an import against.
check_imports() {
    imports=$("$objdump" -p "$1" | sed -n 's/^[[:space:]]*DLL Name: //p')
    if printf '%s\n' "$imports" | grep -Eiq \
            '(msvcrt|ucrt|libgcc|libstdc\+\+|api-ms-win-crt)'; then
        printf '%s\n' "Unexpected runtime import in $1:" >&2
        printf '%s\n' "$imports" >&2
        exit 1
    fi
}

# build_dll out.dll [compiler flags]
build_dll() {
    output=$1
    shift
    "$windres" --input-format=rc --output-format=coff \
        "$script_dir/ddraw_version.rc" "$resource_object"
    "$compiler" -shared -std=gnu99 -Os -s -nostdlib "$@" \
        -Wall -Wextra -Werror \
        -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -Wl,--kill-at \
        -o "$output" \
        "$script_dir/ddraw_proxy.c" "$script_dir/ddraw.def" "$resource_object" \
        -luser32 -lgdi32 -lkernel32
    check_imports "$output"
}

case $mode in
    dll)
        output=${1:-"$script_dir/ddraw.dll"}
        build_dll "$output"
        printf '%s\n' "Built $output" "$imports"
        ;;
    diagnostic)
        output=${1:-"$script_dir/ddraw-diagnostic.dll"}
        build_dll "$output" -DDDWG_DIAGNOSTIC_TRACE=1
        printf '%s\n' "Built diagnostic $output" "$imports"
        ;;
    tests)
        output_dir=${1:-/private/tmp/ddraw-d3d7-smoke}
        mkdir -p "$output_dir"
        build_dll "$output_dir/ddraw.dll"
        for test_name in \
            ddraw_d3d7_triangle \
            ddraw_d3d1_execute_buffer \
            ddraw_d3d7_advanced_texture \
            ddraw_surface_advanced
        do
            exe="$output_dir/${test_name}_test.exe"
            "$compiler" -mwindows -std=gnu99 -Os -s -nostdlib \
                -Wall -Wextra -Werror \
                -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
                -o "$exe" "$script_dir/../sample/${test_name}_test.c" \
                -lddraw -ldxguid -luser32 -lkernel32
            check_imports "$exe"
        done
        printf '%s\n' "Built the DirectDraw/Direct3D smoke tests and ddraw.dll in $output_dir"
        ;;
esac
