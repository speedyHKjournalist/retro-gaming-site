# v86gl XP driver: custom virtio transport

The updated source uses v86's modern virtio PCI capabilities and split queue.
It discovers the assigned I/O BARs, negotiates VERSION_1 and SHARED_ARENA,
then registers the existing 16 MiB command/response arena. The `\\.\v86gl`
name and proxy DLL IOCTL ABI are unchanged.

Game → existing proxy DLL → MAP_BUFFER / SUBMIT IOCTL → virtqueue →
registered shared RAM → browser graphics adapter → WebGPU.

**The checked-in `v86gl.sys` is the old PCI driver.** The new cross-build is
named `v86gl-virtio.sys` to make deployment explicit. Do not use the old binary
with the new host device. The new driver has been cross-compiled and the host
protocol tested, but XP boot/service/game validation remains required.

## Cross-build

With the i686 mingw-w64 toolchain installed, from the repo root:

```sh
sh glbridge/v86gl_driver/build.sh
```

Output: `glbridge/v86gl_driver/v86gl-virtio.sys`. The script accepts an output
path as its first argument; `CC` and `V86GL_DDK_INCLUDE` override tool locations.
It targets PE32, native subsystem 5.1 and only imports kernel/HAL routines.

## XP DDK build

Copy `virtio_transport.h` with `v86gl_driver.c`, and preserve the relative
`../openglproxy/v86gl_ioctl.h` include path.

Build with the Windows Server 2003 SP1 DDK (`3790.1830`) or another
Windows XP-compatible WDK. In this legacy DDK, x86 is the default target;
do not pass `x86` to `setenv.bat`. `64` and `AMD64` are the only architecture
switches.

Place the driver folder at a short path without spaces, such as
`C:\v86gl_driver`. The legacy `build.exe` can lose the source list when the
project path contains spaces, including the default `Documents and Settings`
path on Windows XP.

```text
cd /d C:\WINDDK\3790.1830
call bin\setenv.bat C:\WINDDK\3790.1830 fre WXP
cd /d C:\v86gl_driver
build -cZ
```

This produces an x86 free-build driver, normally at
`objfre_wxp_x86\i386\v86gl.sys`. For a checked build, use
`call bin\setenv.bat C:\WINDDK\3790.1830 chk WXP`; its output directory is normally
`objchk_wxp_x86\i386`. Use the same `C:\v86gl_driver` location and `build -cZ`
for checked builds.

`makefile.def` belongs to the DDK, not this project. Before running `build`,
verify that the DDK environment points at it:

```text
echo %NTMAKEENV%
dir "%NTMAKEENV%\makefile.def"
```

For a complete DDK 3790.1830 installation, `setenv.bat` normally selects
`C:\WINDDK\3790.1830\bin` as `NTMAKEENV`, and that directory must contain
`makefile.def`. If the `dir` command reports it missing, do not add a copy to
this project: repair or reinstall the complete DDK build environment, then
run `setenv.bat` again.

## Install and test

Close all proxy applications, stop the old service, and replace its installed
binary with the new build. Keep the old driver with the old emulator build.
For a new installation, after copying the new binary to `C:\v86gl\v86gl.sys`:

```bat
sc create v86gl type= kernel start= demand binPath= C:\v86gl\v86gl.sys
sc start v86gl
```

Use a cold boot instead of resuming an old PCI-device memory snapshot. Select
**Enable graphics proxy (WebGPU)** in index.html/debug.html. Test the D3D9 clear
and triangle samples before the game. The service now fails startup if the
custom virtio device/capabilities/features cannot be found and initialized.
DebugView's kernel capture shows discovery, negotiation failures and failed
virtqueue requests under `[v86gl.sys]`.

The driver serializes map, submit and unmap operations. Unregister/reset
revokes host arena access before memory is released. Queue completion acknowledges
batch acceptance; GPU query/readback completion still follows the existing
GLWG/D9WG response flags. No new GPU-fence semantics are promised.

Full protocol, provisional device identity, snapshot compatibility and reproducible
host build instructions: [v86/docs/v86gl-virtio.md](../../../v86/docs/v86gl-virtio.md).

## Submission fast path

The optimized driver uses FAST_MUTEX in a critical region instead of KMUTEX,
and completes IRPs after releasing the lock. The IOCTL/virtio wire ABI is unchanged.
The log identifies this build as `virtio perf1`. Before replacing a driver, run
`sc qc v86gl` and use its actual `BINARY_PATH_NAME`; copying a new binary elsewhere
does not update the service. Optimized-driver XP performance remains to be tested.

See [the host/driver optimization report](../../../v86/docs/v86gl-virtio-perf.md).
