#!/bin/sh
# The guest's d3d8.dll for Windows XP, without a C runtime.
#
#   build.sh [out.dll]              the shipping DLL (default: d3d8.dll here)
#   build.sh --diagnostic [out.dll] with the trace file and exception log
#                                   (default: d3d8-diagnostic.dll here)
#   build.sh --tests [dir]          the XP acceptance tests of ../sample
#                                   (stages 3 to 6), with the shipping
#                                   d3d8.dll beside them
#                                   (default: /private/tmp/d3d8-tests)
#
# CC and OBJDUMP select the mingw tools (D8WG_CC the tests' compiler).
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
        "$script_dir/d3d8_proxy.c" "$script_dir/d3d8.def" \
        -luser32 -lgdi32 -lkernel32
    check_imports "$output"
}

case $mode in
    dll)
        output=${1:-"$script_dir/d3d8.dll"}
        build_dll "$output"
        printf '%s\n' "Built $output" "$imports"
        ;;
    diagnostic)
        output=${1:-"$script_dir/d3d8-diagnostic.dll"}
        build_dll "$output" -DD8WG_DIAGNOSTIC_TRACE=1
        printf '%s\n' "Built diagnostic $output" "$imports"
        ;;
    tests)
        output_dir=${1:-/private/tmp/d3d8-tests}
        test_compiler=${D8WG_CC:-$compiler}
        mkdir -p "$output_dir"
        build_dll "$output_dir/d3d8.dll"
        # Stage 3: geometry to maple_gr2d; stage 4: transform_depth to
        # textured_cube; stage 5: stateblock to caps_audit. caps_audit is
        # stage 6's acceptance test as well: audit_caps() requires
        # VertexShaderVersion/PixelShaderVersion to be honestly advertised as
        # vs_1_1/ps_1_4, audit_shader_pipeline() creates and draws with a
        # real VS1.1 + PS1.1 pair, and
        # audit_unsupported_shader_bytecode_rejected() confirms a
        # wrong-version or unsupported-opcode shader is still rejected rather
        # than silently mistranslated. Per-instruction numeric coverage for
        # the VS1.1/PS1.1-1.4 translator lives in the host executor test
        # suite (glbridge/tests/d3d8_webgpu_executor_test.js), where each
        # instruction can be checked without an XP/WebGPU environment.
        for test_name in \
            geometry \
            texture \
            texture_formats \
            texture_stage_ops \
            multitexture \
            dynamic_resources \
            mipmap_filter \
            maple_gr2d \
            transform_depth \
            raster_stencil \
            lighting \
            fog \
            textured_cube \
            stateblock \
            reset_lifecycle \
            lifecycle_compat \
            caps_audit
        do
            exe="$output_dir/d3d8_${test_name}_test.exe"
            "$test_compiler" -mwindows -std=gnu99 -Os -s -nostdlib \
                -Wall -Wextra -Werror \
                -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
                -o "$exe" "$script_dir/../sample/d3d8_${test_name}_test.c" \
                -ld3d8 -lgdi32 -luser32 -lkernel32
            check_imports "$exe"
        done
        printf '%s\n' "Built the D3D8 acceptance tests and d3d8.dll in $output_dir"
        ;;
esac
