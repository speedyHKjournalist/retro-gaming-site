#!/bin/sh
set -eu
cd "$(dirname "$0")"
compiler=${CC:-i686-w64-mingw32-gcc}
# The virtio driver has passed XP boot/probe/game validation. The shipping
# filename is also the service's installed name; an explicit path is supported.
output=${1:-v86gl.sys}
seh_object=$(mktemp "${TMPDIR:-/tmp}/v86gl-map-user.XXXXXX")
trap 'rm -f "$seh_object"' EXIT HUP INT TERM
"${V86GL_SEH_CC:-clang}" --target=i686-pc-windows-msvc -fms-extensions \
    -Os -Wall -Wextra -Werror -c "${V86GL_MAP_USER_SOURCE:-map_user.c}" -o "$seh_object"
# GNU PE ld does not consume MSVC's link-time SafeSEH symbol-index section.
# XP kernel SEH uses the linked handler/scope table in .text/.xdata.
"${V86GL_OBJCOPY:-i686-w64-mingw32-objcopy}" --remove-section=.sxdata "$seh_object"
"$compiler" -Os -Wall -Wextra -Werror -Wno-unused-parameter \
    -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000 \
    -I"${V86GL_DDK_INCLUDE:-$(dirname "$("$compiler" -print-file-name=libntoskrnl.a)")/../include/ddk}" \
    -nostdlib -shared -Wl,--subsystem,native:5.01 \
    -Wl,--entry,_DriverEntry@8 -Wl,--image-base,0x10000 \
    -Wl,--disable-auto-import -Wl,--no-insert-timestamp -o "$output" v86gl_driver.c "$seh_object" \
    -lntoskrnl -lhal -lgcc
printf 'Built %s (XP virtio transport)\n' "$output"
