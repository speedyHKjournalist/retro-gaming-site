# v86gl XP driver

This WDM driver is the PCI-only transport used by `opengl32_proxy.c`.

It allocates a 16 MiB physically contiguous buffer below 4 GiB, maps that
buffer into the calling process, and submits the buffer's guest physical
address to the v86gl PCI I/O BAR at `0xF100`.

## Architecture

```text
┌────────────────────────────── v86 VM: Windows XP guest ──────────────────────────────┐
│                                                                                        │
│  ┌──────────────┐   OpenGL 1.x / WGL   ┌────────────────────────────────────────────┐ │
│  │ Game / app   │ ───────────────────▶ │ opengl32.dll (OpenGL proxy)                │ │
│  └──────────────┘                      │ • Encodes intercepted GL calls             │ │
│                                        │ • Writes descriptor and command records    │ │
│                                        └──────────────┬─────────────────────────────┘ │
│                                                       │ MAP_BUFFER / SUBMIT (IOCTL)    │
│                                                       ▼                                │
│  ┌────────────────────────────────────────────────────────────────────────────────┐  │
│  │ v86gl.sys (WDM kernel driver)                                                   │  │
│  │ • Allocates and maps 16 MiB contiguous RAM below 4 GiB                          │  │
│  │ • Validates the DMA descriptor                                                   │  │
│  │ • Writes DESC_LO / DESC_LEN / COMMAND to the PCI I/O BAR                        │  │
│  └─────────────┬──────────────────────────────────────────────────┬───────────────┘  │
│                │ mapped user address                              │ doorbell          │
│                ▼                                                  ▼                   │
│  ┌────────────────────────────────────┐          ┌────────────────────────────────┐ │
│  │ Shared guest RAM (16 MiB)           │ ◀─ DMA ─ │ v86gl PCI device (BAR 0xF100) │ │
│  │ [ V86GLDMADesc | GL records ... ]   │  read    │ • SUBMIT / FORCE_PRESENT        │ │
│  └────────────────────────────────────┘          │ • STATUS / ERROR / LAST_FRAME   │ │
│                                                   └───────────────┬────────────────┘ │
└───────────────────────────────────────────────────────────────────┼──────────────────┘
                                                                    │ v86gl-pci-frame
                                                                    ▼
┌──────────────────────────────────── Browser / host ───────────────────────────────────┐
│  ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│  │ v86_network_bridge.js                                                            │ │
│  │ • Receives and decodes the PCI command stream                                    │ │
│  │ • Dispatches to v86gl_gl* wrappers                                               │ │
│  └──────────────────────────────────────────┬──────────────────────────────────────┘ │
│                                             ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│  │ gl4es.js + gl4es.wasm: OpenGL 1.x fixed pipeline → OpenGL ES / WebGL             │ │
│  └──────────────────────────────────────────┬──────────────────────────────────────┘ │
│                                             ▼                                         │
│                              ┌──────────────────────────────────┐                    │
│                              │ WebGL / GLES backend              │                    │
│                              │ v86gl_canvas (overlay canvas)     │                    │
│                              └──────────────────────────────────┘                    │
└───────────────────────────────────────────────────────────────────────────────────────┘

  ◀── Status readback: PCI STATUS / ERROR → v86gl.sys → IOCTL return
  ◀── glReadPixels: host writes the synchronous result into shared guest RAM
```

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

Install the resulting `v86gl.sys` as a demand-start kernel service, then start
it before launching the guest OpenGL program:

```text
sc create v86gl type= kernel start= demand binPath= C:\v86gl\v86gl.sys
sc start v86gl
```

The driver exposes `\\.\v86gl`. Its `MAP_BUFFER` IOCTL maps the contiguous
guest RAM into `opengl32.dll`; `SUBMIT` writes descriptor address, byte length,
and doorbell through the PCI I/O BAR. The shared user/kernel declarations are
in `../winproxy/v86gl_ioctl.h`.

## Transport diagnostics

Every map, submit, descriptor validation failure, PCI doorbell, and completion
is written as a `[v86gl.sys]` `DbgPrint` record. Use DebugView in the guest
with **Capture Kernel** enabled. A successful submit includes the same
`frame`, command count, and command byte count recorded by `opengl32.dll`,
then reads `STATUS`, `LAST_FRAME`, `LAST_BYTES`, and `ERROR` back from the PCI
device. This makes a missing/failed transition visible without a kernel
debugger.
