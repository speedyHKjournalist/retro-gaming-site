# Direct D3D8 to WebGPU guest frontend

This app-local `d3d8.dll` bypasses WineD3D, `opengl32.dll`, gl4es and WebGL.
It keeps Direct3D 8 COM objects, shadow state, `Lock`/`Unlock` memory and
batching inside Windows XP, and emits high-level commands into the existing
`v86gl.sys` 16 MiB DMA arena.

## Architecture: D3D8 is translated to D3D9, not backed separately

The DLL emits **D9WG** commands on `V86GL_CTRL_D3D9_BATCH` (`0xFFE1`), decoded
by `../d3d9-webgpu/d3d9_executor.js`. It does not define a protocol of its own.

Direct3D 8 is very nearly a semantic subset of Direct3D 9: the two share render
state numbering, texture-stage-state numbering, FVF bit layout, primitive
types, formats, and -- for shader model 1.x -- bytecode token encoding. The
places they genuinely differ are few and local, and every one of them is
handled guest-side so the host sees a single D3D9-shaped command stream no
matter which API produced it. `d3d8_protocol.h` is that translation layer and
enumerates the differences.

This is the architecture DXVK uses, and for the same reason. A second backend
would have to re-implement cube and volume textures, clip planes, point
sprites, vertex blending, multi-stream layouts, palettes, multisampling,
readback and the whole shader translator -- all of which `../d3d9-webgpu/`
already implements and 3DMark06 already exercises.

`../d3d8-webgpu/d3d8_executor.js` is the retired D8WG backend. It is no longer
on the path for this DLL; it and its tests are kept only as a reference for the
MapleStory-class 2D behaviour they validated.

### What the translation layer actually does

| D3D8 | D3D9 / D9WG | Where |
| --- | --- | --- |
| `SetTextureStageState` for `ADDRESS*`, `*FILTER`, `MIPMAPLODBIAS`, `MAXMIPLEVEL`, `MAXANISOTROPY`, `BORDERCOLOR` | `SetSamplerState` with D3D9's numbering | `d3d8_stage_state_to_sampler_state` |
| `D3DRS_ZBIAS` (integer 0..16, towards viewer) | `D3DRS_DEPTHBIAS` (float, away from viewer) | `emit_render_state` |
| `D3DRS_LINEPATTERN`, `ZVISIBLE`, `EDGEANTIALIAS`, `SOFTWAREVERTEXPROCESSING`, `PATCHSEGMENTS`, `POSITIONORDER`, `NORMALORDER` | deleted in D3D9; shadowed for `GetRenderState`, not sent | `emit_render_state` |
| `CreateVertexShader(declaration, function)` | a vertex declaration object plus a shader object | `declaration_to_elements`, `emit_create_vertex_shader` |
| `D3DVSD_*` token stream binding stream data to `v0..v15` | `D3DVERTEXELEMENT9` binding stream data to usages | `declaration_to_elements` |
| `SetVertexShader(FVF token)` | `SetFVF` carrying the expanded element array | `fvf_to_elements`, `emit_set_fvf` |
| `SetIndices(buffer, baseVertexIndex)` | `SetIndices(buffer)`; base vertex is per-draw | `device_set_indices`, `device_draw_indexed` |
| `SetRenderTarget(colour, depth)` | `SetRenderTarget(0, colour)` + `SetDepthStencilSurface(depth)` | `device_set_render_target` |
| window moved with no `Present` | `D9WG_OP_WINDOW_STATE` | `emit_surface_update_and_flush` |

`d3d8_protocol.h` states the reasoning for each; the ZBIAS scale factor in
particular is a driver-quality choice rather than a specification, and matches
DXVK's so that content tuned against it lands the same way here.

### Vertex registers vs usages

This is the one place the two APIs disagree about meaning rather than spelling.
A D3D8 declaration says "stream N's next `type` bytes load into vertex register
`v`r``"; a D3D9 declaration says "stream N at byte offset X carries a usage",
and the shader's own `dcl_` statements connect a usage to a register.

`vs_1_1` bytecode has no `dcl_` statements at all, so the only meaning `v`r``
ever had is the one D3D8's fixed register semantics assign it -- `v0` is
position, `v3` normal, `v7..v14` texture coordinates, and so on. The guest
synthesises the usage from the register number on that basis
(`d3d8_vsd_register_usage`), which is what a D3D8 declaration means.

The host applies the *same* table to the shader it translates
(`VS1_FIXED_INPUT_SEMANTICS` in `../d3d9-webgpu/d3d9_shader_pipeline.js`),
because `d3d9_executor.js` pairs a declaration element to a shader input by
`(usage, usageIndex)` and binds nothing when the two disagree. Until that
table existed the translator reflected *no* inputs for any real vs_1_x -- only
shaders carrying a `dcl_`, which this model cannot contain, looked correct --
so every `v#` read a zeroed private variable and every vertex-shader draw was
dropped for having no attribute to bind. `d3d8_protocol_consistency_test.js`
compares the two tables by running both, register by register.

## Implemented

- `IDirect3D8`/`IDirect3DDevice8` COM lifecycle, adapter enumeration, caps and
  `CreateDevice`; `Reset`, `Present`, `Clear`, `BeginScene`, `EndScene`;
- vertex/index buffer create, `Lock`/`Unlock` with dirty-range upload,
  `D3DLOCK_DISCARD` orphaning and `D3DLOCK_NOOVERWRITE`;
- 2D textures with `LockRect`/`UnlockRect` subrect upload, `GetSurfaceLevel`
  and `UpdateTexture`;
- **cube textures** (`IDirect3DCubeTexture8`): six mip chains behind one host
  resource, `GetCubeMapSurface`, per-face `LockRect`/`UnlockRect`, and
  `UpdateTexture`. A cube-face surface locks and describes itself through the
  same `IDirect3DSurface8` every other surface uses;
- **volume textures** (`IDirect3DVolumeTexture8`/`IDirect3DVolume8`):
  `GetVolumeLevel`, `LockBox`/`UnlockBox` with a real `D3DBOX` sub-range, and
  `UpdateTexture`. Backed by a genuine 3D WebGPU texture, one slice per upload;
- **every D3DFORMAT D3D8 defines**, in the role D3D8 gives it. What follows is
  the complete enumeration, and
  `glbridge/tests/d3d8_protocol_consistency_test.js` fails if either side of
  the boundary loses one:
  - **textures**, listed below;
  - **render targets and back buffers**: `A8R8G8B8`, `X8R8G8B8`, `R5G6B5`,
    `X1R5G5B5`, `A1R5G5B5`, `A4R4G4B4` -- a 16-bit display mode is only usable
    when its own format is a legal render target, so the older set of two
    32-bit formats made every 16-bit mode fail the check an app runs before it
    picks one;
  - **display modes**: 640x480, 800x600 and 1024x768, each in `X1R5G5B5`,
    `R5G6B5` and `X8R8G8B8`;
  - **depth-stencil**: `D16`, `D24S8`, `D32`, `D15S1`, `D24X8`, `D24X4S4`
    (see Approximations for what each really gets, and Not implemented for
    `D16_LOCKABLE`);
- every texture format the host decodes that D3D8 also names: the
  `A8R8G8B8`/`X8R8G8B8`/`R5G6B5`/`X1R5G5B5`/`A1R5G5B5`/`A4R4G4B4`/`X4R4G4B4`
  core set, `R8G8B8`, `A8R3G3B2`, `R3G3B2`, `L8`/`A8`/`A8L8`/`A4L4`,
  `A2B10G10R10`, `G16R16`, `P8`/`A8P8` with palettes, `DXT1`-`DXT5`, and the
  signed bump formats `V8U8`, `L6V5U5`, `X8L8V8U8`, `Q8W8V8U8`, `V16U16`,
  `W11V11U10`, `A2W10V10U10`; packed-video `UYVY`/`YUY2` is converted from BT.601 4:2:2,
  while legacy `INDEX16`/`INDEX32` texture probes are backed by RGBA8 with the
  little-endian index bytes preserved in channel order;
- eight texture blend stages and eight simultaneous textures, the full
  `SetTextureStageState` cascade, and per-stage sampler state;
- sixteen vertex streams;
- `SetTransform`/`SetViewport`/`SetMaterial`/`SetLight`/`LightEnable`,
  `SetRenderState`, `SetTexture`;
- `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP` and
  `DrawIndexedPrimitiveUP`;
- six user clip planes (`SetClipPlane`/`GetClipPlane`);
- palettes (`SetPaletteEntries`, `GetPaletteEntries`,
  `Set/GetCurrentTexturePalette`);
- fixed-function vertex blending, including `D3DFVF_XYZB1`-`XYZB5` and indexed
  blending against the world-matrix palette;
- point sprites, with the host expanding points into camera-facing quads;
- **environment bump mapping**: `D3DTOP_BUMPENVMAP` and
  `D3DTOP_BUMPENVMAPLUMINANCE` in the fixed-function cascade, driven by
  `D3DTSS_BUMPENVMAT00..11`/`BUMPENVLSCALE`/`BUMPENVLOFFSET`, plus the ps_1_x
  `texbem`/`texbeml`/`texdp3`/`texdp3tex`/`texm3x2*`/`texm3x3*` addressing
  instructions in the shader translator, and the ps_1_4 arithmetic form `bem`
  (3DMark 2001's Advanced Pixel Shader test is built on it). All were
  previously refused;
- **`ProcessVertices`**: a real software fixed-function transform (world x view
  x projection, viewport mapping to `XYZRHW`) with directional, point and spot
  lighting over the shadowed material and light state;
- **4x multisampling** on the device, additional swap chains, render targets
  and depth surfaces;
- vertex shader 1.0-1.1 and pixel shader 1.0-1.4: declaration parsing and
  conversion, bytecode validation, constant banks including `D3DVSD_CONST`,
  state-block capture and `Reset` reconstruction. The guest validator accepts
  exactly the instructions the host translator implements -- including the
  `m4x4`/`m4x3`/`m3x4`/`m3x3`/`m3x2` macros every vs_1_x transform is written
  with, and the ps_1_x `texbem`/`texm3x*`/`texreg2*`/`texdp3*`/`bem` bump and
  addressing family -- and `d3d8_protocol_consistency_test.js` compares the two
  tables instruction by instruction, operand count included;
- render targets, image surfaces, `CopyRects`, state blocks, and a 64-bit
  per-process session namespace. `CopyRects` and the state-block machinery are
  guest-side and carried over unchanged; the session namespace is a D9WG
  feature the D3D8 path now shares;
- **depth-stencil surfaces with a resource of their own**:
  `CreateDepthStencilSurface` allocates a host depth texture at the size asked
  for, rather than aliasing every depth surface onto the device's implicit
  buffer. Render-to-texture depends on this -- an app renders a reflection into
  an offscreen target and creates depth to match -- and the alias left the host
  depth-testing such a pass against the buffer negotiated at `CreateDevice`,
  which it answers by dropping depth testing for the pass entirely. The surface
  `GetDepthStencilSurface()` returns for the implicit buffer still names the
  auto depth-stencil, which is what it is.

## Not implemented

These return `D3DERR_INVALIDCALL` rather than pretending to succeed, and their
caps bits are correspondingly absent:

- **higher-order patches beyond `D3DORDER_LINEAR`.** `DrawRectPatch` and
  `DrawTriPatch` tessellate a linear-order patch exactly -- a linear patch *is*
  its control polygon, so subdividing it reproduces what a hardware
  tessellator would produce. `D3DORDER_CUBIC` and `D3DORDER_QUINTIC` evaluate a
  Bezier or B-spline basis over 4x4/6x6 control points and are refused: drawing
  the control hull instead would render a visibly flatter surface. Patch
  handles are not cached, so `DeletePatch` succeeds as a no-op, and
  `D3DDEVCAPS_RTPATCHES` stays off because the general case is not supported.
- **`D3DVSD_TOKEN_TESSELLATOR`** in a vertex shader declaration, for the same
  reason.
- **multisample types other than 4x.** WebGPU defines exactly two sample counts,
  1 and 4, so `D3DMULTISAMPLE_2/8/16_SAMPLES` have no representation. Rounding
  one of them to 4 would hand an app a different image than it asked for, so
  `CheckDeviceMultiSampleType` reports them unavailable.
- **`ProcessVertices` with a programmable vertex shader bound.** The call would
  have to run the app's own vs_1_1 on the CPU; running the fixed-function
  pipeline instead would silently produce different geometry.
- **`D3DFMT_D16_LOCKABLE`.** Every other D3D8 depth format is offered; this
  one exists purely so `LockRect` works on the depth surface, and WebGPU
  cannot copy a `depth24plus` texture to a buffer at all. A lockable surface
  whose lock fails is worse than a format an app never selects, and plenty of
  real DX8 cards refused it as well. Making it real means giving the depth
  target a copyable format (`depth16unorm`/`depth32float`) and adding a depth
  readback path to `../d3d9-webgpu/`.
- **the ps_1_x depth-replacement instructions** `texdepth` and `texm3x2depth`,
  plus `texm3x3`. The first two write the fragment's depth from a
  texture-addressing result, which the host's pipeline has no `frag_depth` path
  for; the third is the Radeon-era 3x3 form that writes a result without
  sampling, and nothing observed emits it.

## Regression from the D8WG backend

Moving to the shared D3D9 backend gave up one capability the retired
`d3d8_executor.js` had: **v86 save/load state serialization**.

That executor implemented `serializeState`/`restoreState`, which let a v86
snapshot capture a canonical D3D8 device/resource/state checkpoint and rebuild
the exact saved epoch before guest work resumed. `d3d9_executor.js` implements
neither, so `v86_network_bridge.js` serializes an empty buffer for it -- the
guard there is a `typeof` check, so this fails silently rather than erroring.
Saving a running D3D8 title and restoring it now loses host-side GPU state.

This is a pre-existing limitation of the D3D9 path that D3D8 has inherited, not
a new bug, and it applies equally to every D3D9 title. Closing it means
implementing checkpointing in `d3d9_executor.js`, which benefits both paths.

## Approximations

Where WebGPU has no exact equivalent, the closest correct behaviour is
implemented and the deviation recorded here rather than the feature being
refused:

- **`D3DRS_ZBIAS`** is mapped onto D3D9's float `DEPTHBIAS` with a fixed scale
  (see above). The ordering it produces is correct; the exact depth offset is
  a driver-quality choice.
- **`D3DTADDRESS_BORDER`** is implemented by clamping the physical sample and
  substituting `BORDERCOLOR` for coordinates outside the unit domain.
  `D3DTADDRESS_MIRRORONCE` has no emulation and is deliberately *not*
  advertised, rather than silently falling back to clamp.
- **Point size** is emulated by expanding points to camera-facing quads, since
  WebGPU points are always one pixel.
- **`ProcessVertices` lighting** computes ambient and diffuse per D3D8's model
  but not the specular term, and does not clip. Every title that uses this call
  does so to project geometry, not to shade it.
- **ps_1_x 1D texture lookups** (`texdp3tex`) sample a 2D texture at `v = 0`,
  because WGSL has no 1D texture type. Every desktop driver did the same.
- **depth-stencil bit layouts.** The host backs all six accepted depth formats
  with one `depth24plus-stencil8` target, so `D3DFMT_D32` gets 24 bits of
  depth rather than 32, `D3DFMT_D15S1` and `D3DFMT_D24X4S4` get 8 bits of
  stencil rather than 1 and 4, and `D3DFMT_D24X8` gets a stencil buffer it did
  not ask for. Each is more precision than the app requested -- the direction
  that cannot turn a correct scene into a wrong one -- and the guest can never
  read a depth surface back, so the layout is unobservable. An app that relies
  on 4-bit stencil *wrapping* at 16 is the one case this changes.
- **16-bit render targets** are stored as `rgba8unorm` and re-packed to their
  own bit layout only when the app reads one back, so rendering into an
  `R5G6B5` target keeps 8 bits per channel instead of 5/6/5. Banding an app
  expected from a 16-bit target will be absent.

## Building

An XP-compatible DLL with no C runtime dependency:

```sh
./glbridge/d3d8proxy/build.sh /private/tmp/d3d8.dll
```

For an XP diagnostic build that records proxy failures and first-chance
exceptions:

```sh
./glbridge/d3d8proxy/build_diagnostic.sh
```

This produces `d3d8-diagnostic.dll`. Deploy it under the loader-visible name
`d3d8.dll` beside the program. It writes `d3d8_trace_<pid>.log` beside the DLL,
or to `%TEMP%` when that directory is read-only. The trace includes the loaded
EXE/proxy paths, guest memory at attach/detach, every explicit failing HRESULT
with function and source line, `v86gl.sys` open/map/submit Win32 errors, and x86
registers for first-chance exceptions. Start the guest driver first (`sc start
v86gl`); an `OPEN_FAIL` line otherwise records the exact reason that
`Direct3DCreate8` returned `NULL`.

The guest DLL and the host executor are one unit: `d3d8.dll` must be deployed
with a `d3d9_executor.js` of the same D9WG protocol version. The executor
rejects a different protocol minor version rather than silently skipping newer
commands.

Install the DLL beside the target executable. Use a game deployment profile
that does not also contain the `opengl32.dll` proxy: both frontends share one
mapped DMA arena and cannot produce batches concurrently. A guest process loads
either `d3d8.dll` or `d3d9.dll`, never both.

`GetDeviceCaps` advertises vs_1_1/ps_1_4, and the earlier 1.x models are
subsets of those, so they are accepted rather than refused: a D3D8 device that
advertises 1.1 runs a `vs.1.0` shader, and 3DMark 2001 assembles most of its
shaders that way, never checks the `CreateVertexShader` HRESULT, and
dereferences the handle it did not get.

## Guest-side blockers that are not the proxy

**3DMark2001 SE build 300 refuses to start on Windows XP SP3**, with
"3DMark2001 SE needs DirectX 8.1 and proper drivers installed in order to run"
and `DirectX8 not installed.` in `error.log`, however complete the installed
`d3d8.dll` is. The app reads `HKLM\SOFTWARE\Microsoft\DirectX\Version` and
takes `Mid(value, 5, 6)` as the DirectX minor number: correct for DirectX 8.1's
`4.08.01.0881`, but XP SP3 ships DirectX 9.0c as `4.09.00.0904`, whose
substring is `00.090` -> minor 0 -> "DirectX 8.0" -> below its 8.1 floor. The
system-info dump it writes alongside the refusal shows the proxy enumerated
correctly (adapter, modes, formats, full caps), so the trace log is a false
lead here. Futuremark fixed this in build 330; for a build 300 image, patch the
routine at VA `0x005e9d20` -- which takes no stack arguments and has exactly
one caller -- so that it returns 1. Six bytes at file offset `0x1e9d20` of
`3DMark2001SE.exe` (build 300, 4,403,200 bytes):

```
6a ff 68 19 b9 74 00 ...   ->   b8 01 00 00 00 c3   ; mov eax,1 / ret
```

Searching a raw disk image for the routine's opening bytes patches it in place
without mounting the image.

## Tests

```sh
node glbridge/tests/d3d8_protocol_consistency_test.js
node glbridge/tests/legacy_proxy_diagnostic_trace_test.js
```

Guards the translation boundary itself, which is where this architecture's
characteristic bug lives -- a guest that emits an opcode the host ignores, or
drops a render state the host would have honoured, produces a wrong picture and
no error anywhere. It checks that every opcode the guest emits is decoded by
`d3d9_executor.js`, that every payload struct it fills exists in
`d3d9_protocol.h`, that the sampler-state and `DEPTHBIAS` mappings agree with
the executor's own D3D9 constants, and that each deliberately-dropped render
state really is one the executor has no code for.

The D3D9 host suites cover the rest of the path, since it is now the same path:

```sh
node --test glbridge/tests/d3d9_webgpu_executor_test.js
node glbridge/tests/d3d9_shader_pipeline_test.js
node glbridge/tests/d3d9_shader_corpus_test.js
```

The XP acceptance-test builders (`build_stage3_tests.sh` through
`build_stage6_tests.sh`) still build against the D3D8 API surface and remain
the way to exercise the DLL in the guest.
