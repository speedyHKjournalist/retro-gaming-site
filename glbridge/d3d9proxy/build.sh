#!/bin/sh
# The guest's d3d9.dll for Windows XP, without a C runtime.
#
#   build.sh [out.dll]              the shipping DLL (default: d3d9.dll here)
#   build.sh --diagnostic [out.dll] with the trace file and exception log
#                                   (default: d3d9-diagnostic.dll here)
#   build.sh --tests [dir]          the XP smoke tests of ../sample, with the
#                                   shipping d3d9.dll beside them
#                                   (default: /private/tmp/d3d9-smoke-test)
#
# CC and OBJDUMP select the mingw tools (D9WG_CC the tests' compiler).
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${CC:-i686-w64-mingw32-gcc}
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
    "$compiler" -shared -std=gnu99 -Os -s -nostdlib "$@" \
        -Wall -Wextra -Werror \
        -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -Wl,--kill-at \
        -o "$output" \
        "$script_dir/d3d9_proxy.c" "$script_dir/d3d9.def" \
        -luser32 -lgdi32 -lkernel32
    check_imports "$output"
}

case $mode in
    dll)
        output=${1:-"$script_dir/d3d9.dll"}
        build_dll "$output"
        printf '%s\n' "Built $output" "$imports"
        ;;
    diagnostic)
        output=${1:-"$script_dir/d3d9-diagnostic.dll"}
        build_dll "$output" -DD9WG_DIAGNOSTIC_TRACE=1
        printf '%s\n' "Built diagnostic $output" "$imports"
        ;;
    tests)
        output_dir=${1:-/private/tmp/d3d9-smoke-test}
        test_compiler=${D9WG_CC:-$compiler}
        mkdir -p "$output_dir"
        build_dll "$output_dir/d3d9.dll"
        for test_name in \
            clear \
            triangle \
            texture \
            world \
            reset \
            shader \
            caps \
            rt_viewport \
            swapchain \
            depth_texture \
            cube_texture \
            surface_lifetime \
            process_vertices \
            patch
        do
            exe="$output_dir/d3d9_${test_name}_test.exe"
            "$test_compiler" -mwindows -std=gnu99 -Os -s -nostdlib \
                -Wall -Wextra -Werror \
                -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
                -o "$exe" "$script_dir/../sample/d3d9_${test_name}_test.c" \
                -ld3d9 -lgdi32 -luser32 -lkernel32
            check_imports "$exe"
        done
        printf '%s\n' "Built the D3D9 smoke tests and d3d9.dll in $output_dir"
        ;;
esac
