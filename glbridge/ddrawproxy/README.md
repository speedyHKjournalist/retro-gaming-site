# DirectDraw and Direct3D 1-7 to WebGPU guest frontend

This app-local `ddraw.dll` replaces Microsoft's software DirectDraw for one
game directory. It keeps the DirectDraw COM objects, surface memory and
`Lock`/`Unlock` bits inside Windows XP and emits high-level commands into the
existing `v86gl.sys` 16 MiB DMA arena, where the browser turns them into WebGPU
work.

## Architecture: DirectDraw is translated to D3D9, not backed separately

The DLL emits **D9WG** commands on `V86GL_CTRL_D3D9_BATCH` (`0xFFE1`), decoded
by `../d3d9-webgpu/d3d9_executor.js` and its `ddraw_ops.js` mixin. It defines
no protocol of its own, exactly as `../d3d8proxy` does not.

Two facts make that the right shape. First, `d3d.h`'s interfaces are only
reachable through `ddraw.dll` -- `IDirect3D7` comes from
`IDirectDraw7::QueryInterface`, a device renders into an `IDirectDrawSurface7`,
the Z buffer is an attached surface, every texture is a surface -- so a
DirectDraw surface and a Direct3D render target are one object and belong in
one resource table. Second, the header numbers say Direct3D 7 is a *cleaner*
subset of Direct3D 9 than Direct3D 8 was:

| Constant family | `d3dtypes.h` against `d3d9types.h` |
| --- | --- |
| Render states | 53 shared names, every one at the same number |
| Texture stage states | 19 shared names, identical values; the 10 legacy-only ones are exactly the sampler states |
| `D3DTOP_*`, `D3DTA_*`, `D3DPT_*` | identical without exception |
| `D3DTRANSFORMSTATE_*` | 11 identical; only `WORLD`/`WORLD1..3` move |
| `D3DLIGHT7`, `D3DMATERIAL7`, `D3DVIEWPORT7` | byte-for-byte their D3D9 counterparts |

And Direct3D 7 has no programmable shaders at all, so the hardest part of the
D3D9 path has no counterpart here. `ddraw_protocol.h` is the translation layer
and enumerates every place the legacy APIs genuinely differ.

## What is implemented

The current frontend covers the DirectDraw 2D path through every retail
interface version and the Direct3D 1-7 fixed-function object models:

| Area | State |
| --- | --- |
| `DirectDrawCreateEx` with `IID_IDirectDraw7` | done |
| `SetCooperativeLevel`, `SetDisplayMode`, `RestoreDisplayMode` | done, including a real `ChangeDisplaySettings` attempt |
| `CreateSurface`: primary, flip chains, offscreen plain, system memory | done |
| `Lock`/`Unlock` over a system-memory shadow, dirty-rect upload | done |
| `Blt`, `BltFast`: copy, stretch, source/destination colour keys and overrides, mirroring, colour/depth fill, `SRCCOPY`/`BLACKNESS` | done; destination-key and self-blits use detached target snapshots |
| `Flip`, including storage rotation around a chain | done |
| `IDirectDrawPalette`, palettised surfaces kept as GPU-side indices | done |
| `IDirectDrawClipper`, resolved guest-side into per-rectangle blits | done |
| `GetDC`/`ReleaseDC` over a DIB section | done |
| Overlay show/hide, position/stretch, dirty refresh, source/destination keys and overrides, mirroring, enumeration and all z-order operations | done as a non-destructive present-time composite |
| `DuplicateSurface` | done; distinct COM/overlay identity over refcounted shared GPU storage and CPU pixels |
| `SetSurfaceDesc` | done for explicit system-memory surfaces, including client-owned memory and size/format reallocation |
| `EnumDisplayModes`, `GetCaps`, `GetDisplayMode`, `GetDeviceIdentifier` | done |
| `WaitForVerticalBlank`, `GetScanLine` | approximated from the clock |
| Readback of a GPU-written surface on `Lock` | done for true-colour, P8 and BC/DXT subresources |
| Pre-v7 interfaces: `IDirectDraw`/`2`/`3`/`4` and `IDirectDrawSurface`/`2`/`3`/`4` | done, as thunks over the v7 implementation |
| `DirectDrawCreate` (the v1 entry point every retail title uses) | done |
| `IDirect3D7`, HAL/T&L HAL/RGB enumeration and device creation | done |
| `IDirect3DDevice7`: render target/depth binding, clear, transforms, viewport, material, lights, render/TSS/sampler state, textures and all draw entry points | done |
| D3D7 state blocks and `IDirect3DVertexBuffer7`, including software `ProcessVertices` | done |
| D3D7 mip chains, six-face cube maps, DXT1-5 upload/readback and texture colour-key rendering | done |
| D3D7 `ComputeSphereVisibility`, including enabled user clip planes | exact CPU frustum classification |
| Direct3D 1-6 factory/device/texture/VB/viewport/material/light views | done as adapters over the D3D7 core |
| D3D1 matrices, execute buffers, all 14 opcodes and triangle `Pick` records | done |
| DirectDraw video ports and overlay alpha/bob capture modes | not advertised; refused |

Every refusal names itself through `D9WG_OP_GUEST_LOG`, so it appears in the
browser console rather than only as an `HRESULT` nobody can see.

### How the interface versions fit together

A versioned DirectDraw interface is a different *view* of one object with one
reference count, not a different object, so each object carries every vtable it
answers to and `QueryInterface` hands back a pointer into the same allocation.
Which version a call arrived through is recovered from the vtable the caller's
pointer names (`surface_from_any`), which is also how a method that returns an
interface knows which version to hand back -- a version 2 app calling
`GetAttachedSurface` gets an `IDirectDrawSurface2`, because that is what it
will call through.

The version 7 methods are the implementation; the older versions are generated
thunks that convert their arguments and forward. They differ in exactly three
mechanical ways: the interface pointer type, `DDSURFACEDESC` against
`DDSURFACEDESC2` (with `DDSCAPS` against `DDSCAPS2` inside it), and `Unlock`
taking the locked pointer in versions 1-3 and a rectangle in version 4.

A vtable is positional and the compiler cannot check it -- every entry has the
same shape -- so `../tests/ddraw_vtable_layout_test.js` checks all 355 slots
across the twelve interfaces against the order `ddraw.h` declares. A swapped
pair there is a crash with no explanation anywhere near the mistake, inside a
VM. `ddraw_d3d_legacy_vtable_test.js` performs the same check for the 241 slots
in the seventeen Direct3D 1-6 vtables.

### Not yet run against a real game

Everything above is verified statically, by the JS suites, by real `naga`
compilation and by clean `-Werror`, no-CRT builds. The XP-targeted four smoke
programs are written and build: the D3D7 triangle, the advanced
mip/cube/DXT/colour-key/sphere test, a D3D1 execute-buffer test that also draws
through Device2 and Device3, and the advanced DirectDraw surface/overlay test.
None has executed inside the XP guest yet.
Guest execution is therefore still the next acceptance gate; static and host
executor coverage is not presented as real-game compatibility.

## Documented deviations

Each of these is a place where the API says one thing and this DLL does
another, on purpose.

| Deviation | What happens instead | Why |
| --- | --- | --- |
| `DDFLIP_NOVSYNC`, `DDFLIP_INTERVALn` | ignored | the browser's presentation cadence is not ours to choose |
| `WaitForVerticalBlank`, `GetScanLine` | derived from `GetTickCount` at ~60 Hz | there is no raster to read; a raster that never moves turns a wait loop into a hang |
| Exclusive fullscreen when `ChangeDisplaySettings` fails | the overlay covers the game window at 1:1, the desktop keeps its resolution | a stretched overlay with untouched input coordinates looks right and points wrong |
| Clip lists over 64 rectangles | the surplus rectangles are dropped | bounded work per blit; the count is reported through the host's counters |
| Hardware overlay scanout | a disposable composite is built immediately before present | keeps overlay pixels out of retained primary/readback, but cannot refresh independently of browser presentation or participate in guest GDI composition |
| Overlay alpha, bob/interleaved capture and video ports | refused and their caps are not advertised | these require a video-capture/scanout model rather than a DirectDraw texture composite |
| `DDBLTFX` rotation and `DDBLTFX_ARITHSTRETCHY` | ignored, mirroring is honoured | no GPU equivalent; the caps bits for them are not set either |
| Raster operations other than `SRCCOPY` and `BLACKNESS` | refused with `DDERR_NORASTEROPHW` | the only two DirectDraw ever required of a driver |
| GDI drawn straight onto the primary while DirectDraw owns it | does not appear | the primary is a host texture, not the screen; the same boundary d7vk draws |
| `CoCreateInstance(CLSID_DirectDraw)` | reaches `system32\ddraw.dll`, not this DLL | the registry, not the loader, resolves it; app-local replacement cannot intercept it |
| Surface loss | never reported; `Restore` returns `DD_OK` | nothing here is lost to a mode change or a task switch |
| Filtered true-colour texture colour keys | compared after the sampled RGB value is quantised | exact at texel centres; filtered edges may differ from individual 1990s drivers |
| `D3DLIGHT_PARALLELPOINT` / `D3DLIGHT_GLSPOT` | translated to directional / spot | D3D9/WebGPU has no distinct form for either legacy light type |
| D3D1 `D3DOP_SPAN` | submitted as a point list over the named transformed vertices | WebGPU has no span primitive; the opcode remains executable instead of aborting the buffer |
| D3D1 triangle `Pick` at exact shared edges | top-left raster rule in guest floating point | historical drivers differed by subpixel rounding at boundaries |
| D3D7 `GetInfo` | returns `S_FALSE` | permitted by the D3D7 contract for unsupported driver-private queries |

## Colour keys, and the one rule both sides must share

A `DDCOLORKEY` is a value in the *surface's own* format -- five bits of red in
RGB565, a palette index in P8 -- and the GPU compares eight-bit texels the host
produced when it uploaded the surface. Both sides therefore have to widen a
narrow channel by the identical rule, and they do:

```c
    return (value * 255u) / max;    /* ddraw_expand_channel */
```

which is what `expandRowToGPU` in `../d3d9-webgpu/d3d9_executor.js` already does
(`(v * 255 / max) | 0`). The DirectDraw-era hardware rule was high-bit
replication instead, and the two differ by one at ordinary values -- 24 of 31
widens to 197 by scaling and 198 by replication -- so a key widened by the
other rule misses on exactly those colours and the sprite blits as a solid
rectangle. `../tests/ddraw_channel_expansion_test.js` compiles this function
and compares it against the executor's, value by value, for every channel
width, so the two cannot drift apart quietly.

A palettised surface compares the index itself, before any palette lookup,
which is what the API says it compares.

## Palettised surfaces are indexed on the GPU

A P8 surface is created with `D9WG_USAGE_DDRAW_INDEXED` and kept as an
`r8uint` index texture, resolved through a 256-entry palette buffer at sample
and present time -- not expanded to RGBA on the CPU the way the D3D9 path
expands a P8 texture.

The reason is correctness rather than speed. A 2D title blits P8 into P8 all
frame long, and a surface holds *indices*, so a later palette change has to
change the colour of pixels that were blitted earlier. An RGBA copy cannot be
re-indexed, so a CPU-expanded destination would freeze at whatever palette was
current when the sprite landed. Palette animation -- the water in Age of
Empires II, the fades in the Infinity Engine -- then costs a 1 KiB buffer write
instead of a full-surface repaint.

## Building and deploying

```sh
./build.sh                 # produces ddraw.dll next to this README
./build_diagnostic.sh      # produces ddraw-diagnostic.dll with full tracing
./build_smoke_test.sh /private/tmp/ddraw-d3d7-smoke
                           # produces the DLL + four XP-compatible EXEs
```

Deploy `ddraw-diagnostic.dll` under the loader-visible name `ddraw.dll`. For a
game that normally means beside its EXE; for `dxdiag.exe` it means the system
copy it actually loads, after preserving the original. The diagnostic DLL
writes `ddraw_trace_<pid>.log` beside itself, or to `%TEMP%` if that directory
is read-only. Delete an old trace before starting a new capture: DLL reloads in
the same process append new `PROCESS_ATTACH` sections so an early dxdiag pass
cannot be overwritten by a later one. It records loaded module paths, memory
state, every explicit failing DirectDraw or Direct3D 1-7 HRESULT, successful
returns from the critical DirectDraw display path, surface parameters and
lifetime, uploads, presents, batch submissions, `v86gl.sys` transport state,
and x86 exception registers. Repeated status polling is sampled at powers of
two, and `OutputDebugString` exceptions are omitted as tracing noise. Start
`v86gl.sys` before the test (`sc start v86gl`). The smaller
`ddraw-webgpu.log` remains the operational log for transport retries and
host-visible refusals.

The build fails if the DLL picks up a C runtime import: it links `-nostdlib`
and must import nothing beyond `kernel32`, `user32` and `gdi32`.

Deployment stays mutually exclusive with the sibling proxies -- a game
directory holds exactly one of `ddraw.dll`, `d3d8.dll`, `d3d9.dll` or
`opengl32.dll`. One DMA arena, one overlay canvas, one owner.

The transport is `v86gl.sys`, a WDM driver, so the guest is Windows XP. Several
of these titles also ran on Windows 98, but those images have no transport
driver.

## Tests

| Test | What it pins |
| --- | --- |
| `../tests/ddraw_protocol_consistency_test.js` | the wire structs, opcode numbers, flag bits and export list agree across the header, the DLL and the host module |
| `../tests/legacy_proxy_diagnostic_trace_test.js` | every explicit error return is routed through the diagnostic trace and both diagnostic build scripts remain wired |
| `../tests/ddraw_channel_expansion_test.js` | the guest and the executor widen colour channels identically |
| `../tests/ddraw_webgpu_executor_test.js` | which pipeline variant, which bindings, what lands in the uniform block, and which blits are refused |
| `../tests/ddraw_blit_wgsl_validation_test.js` | every generated blit shader compiles under real `naga` |
| `../tests/ddraw_d3d7_vtable_test.js` | all 66 `IDirect3D7`/device/VB slots match the SDK order and the DirectDraw QI route is present |
| `../tests/ddraw_d3d_legacy_vtable_test.js` | all 241 Direct3D 1-6 COM slots, factory/surface QI routes and all execute-buffer opcodes |
| `../sample/ddraw_surface_advanced_test.c` | XP-side COM calls for destination keys, overlay state/z-order, shared `DuplicateSurface` pixels and client-owned `SetSurfaceDesc` memory |

See `../../docs/ddraw-d3d7-webgpu-architecture.md` and
`../../docs/ddraw-d3d7-webgpu-implementation-plan.zh-CN.md` for the milestone
schedule and the protocol increment.
