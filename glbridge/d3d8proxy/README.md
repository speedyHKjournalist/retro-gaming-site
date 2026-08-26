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
- every texture format the host decodes that D3D8 also names: the
  `A8R8G8B8`/`X8R8G8B8`/`R5G6B5`/`X1R5G5B5`/`A1R5G5B5`/`A4R4G4B4`/`X4R4G4B4`
  core set, `R8G8B8`, `A8R3G3B2`, `R3G3B2`, `L8`/`A8`/`A8L8`/`A4L4`,
  `A2B10G10R10`, `G16R16`, `P8`/`A8P8` with palettes, `DXT1`-`DXT5`, and the
  signed bump formats `V8U8`, `L6V5U5`, `X8L8V8U8`, `Q8W8V8U8`, `V16U16`,
  `A2W10V10U10`;
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
  instructions in the shader translator. Both were previously refused;
- **`ProcessVertices`**: a real software fixed-function transform (world x view
  x projection, viewport mapping to `XYZRHW`) with directional, point and spot
  lighting over the shadowed material and light state;
- **4x multisampling** on the device, additional swap chains, render targets
  and depth surfaces;
- vertex shader 1.1 and pixel shader 1.1-1.4: declaration parsing and
  conversion, bytecode validation, constant banks including `D3DVSD_CONST`,
  state-block capture and `Reset` reconstruction;
- render targets, depth surfaces, image surfaces, `CopyRects`, state blocks,
  and a 64-bit per-process session namespace. `CopyRects` and the state-block
  machinery are guest-side and carried over unchanged; the session namespace is
  a D9WG feature the D3D8 path now shares.

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
- **the ps_1_x depth-replacement instructions** `texdepth` and `texm3x2depth`,
  and the ps_1_4 `bem`. The first two write the fragment's depth from a
  texture-addressing result, which the host's pipeline has no `frag_depth` path
  for.

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
