#!/bin/sh
set -eu
cd "$(dirname "$0")"
compiler=${CC:-i686-w64-mingw32-gcc}
# The virtio driver has passed XP boot/probe/game validation. The shipping
# filename is also the service's installed name; an explicit path is supported.
output=${1:-v86gl.sys}
"$compiler" -Os -Wall -Wextra -Werror -Wno-unused-parameter \
    -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000 \
    -I"${V86GL_DDK_INCLUDE:-$(dirname "$("$compiler" -print-file-name=libntoskrnl.a)")/../include/ddk}" \
    -nostdlib -shared -Wl,--subsystem,native:5.01 \
    -Wl,--entry,_DriverEntry@8 -Wl,--image-base,0x10000 \
    -Wl,--disable-auto-import -Wl,--no-insert-timestamp -o "$output" v86gl_driver.c \
    -lntoskrnl -lhal -lgcc
printf 'Built %s (XP virtio transport)\n' "$output"
