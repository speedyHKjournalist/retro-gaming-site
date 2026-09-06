#!/bin/sh
set -eu
cd "$(dirname "$0")"
compiler=${CC:-i686-w64-mingw32-gcc}
# Keep this cross-build separate from the installed/checked-in DDK binary until
# it has been boot-tested. Override the output path explicitly to deploy it.
output=${1:-v86gl-virtio.sys}
"$compiler" -Os -Wall -Wextra -Werror -Wno-unused-parameter \
    -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000 \
    -I"${V86GL_DDK_INCLUDE:-$(dirname "$("$compiler" -print-file-name=libntoskrnl.a)")/../include/ddk}" \
    -nostdlib -shared -Wl,--subsystem,native:5.01 \
    -Wl,--entry,_DriverEntry@8 -Wl,--image-base,0x10000 \
    -Wl,--disable-auto-import -o "$output" v86gl_driver.c \
    -lntoskrnl -lhal -lgcc
printf 'Built %s (XP boot validation still required)\n' "$output"
