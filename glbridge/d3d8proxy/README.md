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
| Windowed swap effect and fullscreen presentation interval | Effective D3D9 interval, with a shared virtual deadline clock | `d9wg_present_interval8`, `d9wg_present_pace` |
| window moved with no `Present` | `D9WG_OP_WINDOW_STATE` | `emit_surface_update_and_flush` |

`d3d8_protocol.h` describes the protocol conversions; the ZBIAS scale factor in
particular is a driver-quality choice rather than a specification, and matches
DXVK's so that content tuned against it lands the same way here.

Device `Present` shares its clock and deadline logic with D3D9. D3D8 windowed
DISCARD/FLIP/COPY chains present immediately; COPY_VSYNC uses one refresh.
Fullscreen COPY_VSYNC also converts an IMMEDIATE request to one refresh.
The common clock keeps fractional tick debt, resamples early timer wakes and
falls back to a millisecond timebase if the performance counter fails or stalls.
`D9WG_PRESENT_NO_THROTTLE=1` disables device Present pacing in both frontends.
These are virtual deadlines; physical retrace, additional-chain pacing, full
presentation-parameter validation and Reset boundaries have separate review steps.

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
- **`ProcessVertices`**: shared software transforms for FVF and fixed-function
  declarations created with a NULL shader function. Declared inputs support
  multiple and constant streams, weighted matrix blending and CPU Tween,
  ambient/diffuse/specular lighting, vertex fog, generated/transformed texture
  coordinates, user-plane ClipStatus and clipping records for later FVF draws.
  Every input aliasing the output uses the same pre-call snapshot. Ordinary
  declaration draws validate every referenced stream and share Tween/user-plane
  ClipStatus; UP declarations are limited to stream zero. The Tween capability
  flag is advertised after paired raw/CPU guest pixel replay acceptance on
  SwiftShader and Metal;
- texture block geometry and checked mip row/byte calculations generated from
  the host format table in `../runtime/d9wg_format_layouts.h`; frontend layout
  ranges stay version-specific and do not imply GPU capability support;
- version-specific material and zero clip-plane defaults generated from the
  shared host table: D3D8 diffuse RGB is white with zero alpha; D3D9 material
  is entirely zero. Reset clears material, planes, and low/sparse light state;
  API8 material differences are sent after creation and Reset;
- texture-stage and sampler defaults generated from the same host table,
  including all pixel/vertex sampler slots, stage-dependent routing and
  D3D8's legacy sampling-state mapping; first DIFFUSE=0 operand changes
  are emitted from their CURRENT=1 defaults;
- render-state defaults generated from the shared host table, including
  creation-time depth, the reported point-size maximum and D3D8's zero point
  minimum; explicit version/capability differences are synchronized after
  creation and Reset;
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
./glbridge/d3d8proxy/build.sh --diagnostic
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

The XP acceptance tests of stages 3 to 6 (`build.sh --tests [dir]`, which
puts the shipping `d3d8.dll` beside them) still build against the D3D8 API
surface and remain the way to exercise the DLL in the guest.

The shared process runtime now includes bounded host-capability discovery.
Deploy D3D8, D3D9 and DirectDraw DLLs from the same build together; mixed
process-layout versions are rejected before accessing shared resource state.

Window-report region encoding and duplicate suppression now use the shared
`../runtime/d9wg_window_state.c` core. The full transmitted report participates
in the comparison, including geometry and foreground HWND; inactive region
tail bytes do not. Failed queueing/submission leaves the previous cache so an
idempotent state report can retry. Actual XP move/resize, foreground, hide/show
and no-surface observations pass, with the original command stream also
replayed on SwiftShader and Metal. Win32 capture/event code and per-Present
probe reuse continue in the following refactor step.

Adapter identity, current display mode and software-device registration now use
an API-specific factory profile in the private D3D9 core. D3D8 retains its
legacy identifier and WHQL policy; its 1068-byte SDK layout is converted from
the 1100-byte D3D9 identifier by skipping DeviceName. Normal D3D9 identities
remain byte-identical across all four build configurations. Old accepted DLLs
are the XP byte-comparison baseline, with native delegation/error/copy guards
and independent SwiftShader/Metal replay. See review stage 4K4 and deployment
`../deployments/d3d-webgpu-review-4K4.json`. Adapter enumeration/format/PP and
D3D8 device/resource wrapping continue in following phases.

Stage 4K5 also moves the D3D8 adapter mode count and nine-entry list into the
private API8-profile core. Its resolution-major X1R5G5B5/R5G6B5/X8R8G8B8
ordering stays intact; ordinary D3D9 still enumerates real driver modes by
format. Previous-C native comparisons, old-DLL XP comparisons and both GPU
backends cover the move. See `../deployments/d3d-webgpu-review-4K5.json`.

Stage 4K6 delegates D3D8 device-type, multisample and depth-match queries to
the private core factory. Its legacy format restrictions filter common
predicates, while ordinary D3D9 query and quality results stay unchanged.
Native current/prior-C matrix checks and old-DLL XP comparisons cover both
APIs. See `../deployments/d3d-webgpu-review-4K6.json`. The legacy D3D8
creation predicates remain until device/resource objects are wrapped.

Stage 4K7 delegates D3D8 CheckDeviceFormat through the API8-profile core.
It retains API8 shared-layout filtering and legacy resource/usage rules;
ordinary D3D9 query/autogen/host-capability behavior stays unchanged.
Native prior-C comparisons and old-DLL XP comparisons cover the move. XP
uses an explicit simulated capability response0; real pixels are separately
replayed on SwiftShader/Metal. See `../deployments/d3d-webgpu-review-4K7.json`.

Stage 4K8 routes D3D8 CreateDevice effective parameter storage through a
checked52-byte PP8 to56-byte PP9 layout bridge and the common SDK9 helper.
Quality is zero;legacy8 swap/interval/flag values and caller publication stay
intact. Native alias/guard/provider tests and real SDK120/220 zero-parameter
constructors cover the path. Reset/additional-chain normalization remains for
the next step. See `../deployments/d3d-webgpu-review-4K8.json`.

Stage 4K9 completes effective parameter routing for D3D8 Reset and
additional swap chains through the same checked PP8/PP9 core bridge.
SDK120/220 actually resets each device and creates/releases a transient
texture/swap-chain pair,verifies count-only caller publication and8x8
backbuffers,then replays five main images on both GPU backends with all
resources retired before SessionEnd. Actual device/resource wrapping follows.
See `../deployments/d3d-webgpu-review-4K9.json`.

Stage 4K10 adds a private API8-profile actual core9 device constructor.
It preserves immutable PP8 input,legacy constructor validation and initial
API8 states while ordinary9 stays byte-compatible with its prior defaults.
A test-only SDK8 consumer verifies seven actual devices and two private
module lifetimes in XP;both GPUs replay five images/320 pixels with all
resources retired before SessionEnd. Public8 device/resource wrapping and
API8 Reset/additional/shader adapters remain open.
See `../deployments/d3d-webgpu-review-4K10.json`.

Stage 4K11 extends the private API8 core device to Reset validation,
legacy blocker errors,cooperative pending/recovery,count-only publication,
current-format UNKNOWN fallback andprofile-specific defaults/replay patches.
SDK8 consumers verify real held-backbuffer failures andsuccessful recovery;
ordinary9 Reset preserves9 publication anddefaults. Three actual Reset
commands replay five images/320 pixels on both GPUs. Public8 devices remain
legacy;additional/shader adapters anddevice/resource wrapping follow.
See `../deployments/d3d-webgpu-review-4K11.json`.

Stage 4K12 adds private API8 core additional-chain construction with
immutable PP8 input,legacy validation/current-format fallback,count-only
publication andNULL/empty-client window geometry. XP verifies six real
RT texture/chain lifetimes,16-bit NULL-window chains,MSAA4/flag1 metadata,
container identity andcomplete retirement beforedrawing. Both GPUs replay
five images/320 pixels with allmaps empty beforefallback cleanup. Public8
wrappers,surface LockRect andadditional Present remain separate adapters.
See `../deployments/d3d-webgpu-review-4K12.json`.

Stage 4K13 shares the existing legacy8 shader body/opcode/register policy in
this static core. Public8 delegates body validation; API8 core devices use
legacy version/bounds rules, including VS1.0/1.1 without dcl and PS1.0–1.4.
Ordinary9 retains its prior scanner. SDK8 shifted register enums are normalized
to SDK9 ordinal enums. The existing shader hash/wire encoding is preserved.
XP compares the accepted 4K12 public8 DLL with the current DLL and exercises
core8 shader creation, byte queries, zero-public-reference bindings and two
programmable draws. SwiftShader and Metal each verify five frames/320 pixels;
all 22 shader resources retire before fallback cleanup. Public8 device/resource/
handle wrapping and numeric shader differential testing remain open.
See `../deployments/d3d-webgpu-review-4K13.json`.

Stage 4K14 preserves all D3DVSD_CONST blocks as shader-local DEF instructions
in private wire code. Literal data cannot terminate the declaration scan;
original public function/declaration queries remain unchanged. Binding no
longer uploads declaration values into device constants, so local precedence
survives API writes, shader switches, retained state blocks and Reset. Fixed
NULL-function declarations keep their layout without a shader/local upload.
Native checks cover bounds, holes, deterministic last-block overlap handling,
bit patterns, byte/hash contracts and API-bank isolation. Native HAL overlap
precedence and NaN arithmetic are not established by these checks.
XP SDK120/220 compares accepted 4K13 and current DLLs. Both GPU backends verify
14 frames/896 pixels, including the old lost-block regression and current
red/green/gap/overwrite/state-block/Reset results. Direct public8 backbuffer
LockRect remains open; the probe uses existing CopyRects to CPU surfaces.
See `../deployments/d3d-webgpu-review-4K14.json`.

Stage 4K15 accepts documented CND in ps_1_4 through the shared API8 policy.
Vertex rejection, other opcode rules and the ordinary9 scanner stay intact.
Historical native oracles include one explicit CND1.4 contract correction;
17 runtime mutants and an actual prior-policy control distinguish the fix.
XP SDK120/220 reproduces the old1.4 refusal and validates current creation,
original function bytes and expired handles. SwiftShader/Metal each compare
26 frames/1664 pixels: scalar alpha in1.1 versus per-component1.4 at0.5 and
adjacent float32 values,including alpha. Coissue,NaN arithmetic and the full
numeric differential corpus remain separate work. The shipped ordinary9 DLL
inherits its accepted version; only the accepted current8 DLL is published.
See `../deployments/d3d-webgpu-review-4K15.json`.

Stage 4K16 implements explicit lockable implicit backbuffers in public8 and
core9, including private API8-profile core devices. A common pure planner
checks geometry/pitch/range and a common upload bridge sends only the locked
region. Successful locks refresh from GPU before publishing state; READONLY
unlock emits no upload. Non-lockable/MSAA/repeated/invalid locks still fail.
Core9 lazily owns CPU lock storage and releases it on successful Reset so
cached surfaces safely pick up new geometry. Other surface dispatch is retained.
XP compares accepted 4K15 baseline8/9 refusals with public8 SDK120/220,
ordinary9 and privatecore8. Both GPUs independently compare 27 images/1536
pixels, including partial/full writes and 4x4 resize after Reset. XP CPU replies
explicitly inject analytic expected images; only GPU replay establishes pixels.
Additional-chain/depth/other target locks and other GPU formats remain open.
See `../deployments/d3d-webgpu-review-4K16.json`.

Stage 4K17 extends core9 renderable locks to explicitly lockable standalone RTs
and additional backbuffers. The common upload bridge addresses their texture
storage; writable unlock invalidates cached Clear/ColorFill mirrors. Texture
level lock tracking also blocks Reset for a locked bound target with zero
public references. Copy readback rejects locked source/destination storage.
Existing public8 target locks remain the old/current control.
XP validates five modes, ten targets, four Reset recoveries and 46 readbacks.
Both GPUs independently verify 2944 pixels; XP CPU replies explicitly inject
analytic images. Additional Present is excluded: replay has no window link
and refuses five presentation chains while exercising real target textures.
See `../deployments/d3d-webgpu-review-4K17.json`.

Stage 4K18 adds the private SDK8-callable standalone render-target constructor.
It preserves public8's4096 limit, six formats, sample/lockability and output
failure ordering, then uses actual core9 storage and lock/lifetime handling.
The SDKs remain in separate translation units; the private stdcall ABI is28bytes.
Native validation compares the frozen public8 method across421635 cases.
XP exercises13 guarded failures and the new constructor with the retained
46-image/2944-pixel contract, independently verified by both GPU backends.
CPU replies are analytic injection; additional Present and full public8 device
or resource wrapping are excluded. See `../deployments/d3d-webgpu-review-4K18.json`.

Stage 4K19 adds the private SDK8-callable depth-surface constructor with the
legacy six-format/sample and early-output rules. It reuses core8192 storage
bounds and forwards zero quality, discard=false and no shared handle.
A separate SDK8 consumer verifies the24-byte private ABI. Native comparison
covers70275 cases,14 strict runtime mutants andASAN. XP tests old/current8,
ordinary9 andcore8SDK120/220:depth retention after rebind,depth-only clear,
and a half-screen stencil mask,plusmetadata/identity/lock refusal andReset.
Both GPUs independently match20images/1280pixels; CPU replies are analytic
injection. MSAA4 depth metadata/storage is tested,not physical coverage or
precision. Public8wrapping remains pending. See `../deployments/d3d-webgpu-review-4K19.json`.

Stage 4K20 adds private API8 CPU-image and32-byte surface-descriptor adapters.
Shared CPU allocation preserves ordinary9 behavior; image8 selects its linear
formats and8192 limit. CPU locks use the device API,including8-only format65.
The descriptor stages SDK8 Size/samples words instead of copying9 samples/quality.
XP verifies local CPU65 bits/pitch/Size inold/current8/core8SDK120/220,ordinary9
refusal,andatomic descriptor byte guards. Both GPUs independently retain the
20-image/1280-pixel depth/stencil contract; no GPU-format65 claim is made.
Depth Size currently mirrors the old proxy's0/fixed4 metadata. Pinned Wine
shows this is a resource-size issue to correct next; it is not native compliance.
Public8 wrapping stays pending. See `../deployments/d3d-webgpu-review-4K20.json`.

Stage 4K21 corrects API8 depth resource Size through one shared format layout.
Six depth formats use 2/4 bytes per pixel and four-byte row alignment, without
multiplying the logical size by MSAA. Public8 standalone/implicit descriptors
and Reset caches now agree with the private core8 SDK descriptor. Pinned Wine
sources support this rule; native Windows and physical depth precision remain
unverified. Color/CPU-image pitch retains its prior behavior.
XP reproduces the accurate4K20 baseline bug and checks56 metadata records,
including odd dimensions and Reset. Both GPUs retain20images/1280pixels with
zero errors; CPU replies are explicit analytic injection. Native checks include
103680 layout cases,192 public descriptor cases,47203 SDK descriptor cases,
31 strict runtime mutations,four actual old-function controls andfive ASAN
families. Public8 wrapping remains pending. Evidence:
`../deployments/d3d-webgpu-review-4K21.json`.

Stage 4K22 adds the private32-byte stdcall API8 2D texture creation boundary.
A shared constructor retains8's4096 bound/format/pool/mip/output policy and
ordinary9's8192/autogen policy. Ordinary CPU texture locks and uploads select
the owning device API,including8-only65; ordinary9 continues to refuse65.
Actual frozen8/9 constructors and create emitters cover2945283 cases,with
41760 lock/upload cases,23 strict runtime mutants,three historical controls
andtwo ASAN families. XP old/current8,ordinary9,core8SDK120/220 validateCPU65,
four-level21 storage/surface views,partial uploads andBC1. Both GPUs match
20images/1280pixels from43uploads and20sample draws. CPU GPUreadback replies
are explicit analytic injection. Renderable texture readback,fullpublic8
wrapping andnative alignment/format conformance remain pending. Evidence:
`../deployments/d3d-webgpu-review-4K22.json`.

Stage 4K23 adds private API8 cube creation through the28-byte stdcall boundary.
Shared storage preserves8's4096/legacy policy and9's8192/autogen policy,with
six face-major mip chains. Ordinary cube CPU locks/uploads use the device API,
including8-only65 and8's BC-subrectangle alignment rules. Native comparison
covers2548803 constructors and467712 lock/upload cases,26 strict runtime
mutants,three actual historical controls andtwo ASAN families. XP old/current8,
ordinary9,core8SDK120/220 validateface/mip storage,metadata/identity,partial
surface writes andBC1. Both GPUs independently match45images/2880pixels from
173uploads,six face colors andunchanged adjacent face afterediting+Z.
CPU GPUreadback replies are explicit analytic injection. Renderable cube
readback,fullpublic8wrapping andnative alignment/fullformat conformance remain
pending. Evidence: `../deployments/d3d-webgpu-review-4K23.json`.

Stage 4K24 adds private API8 volume creation@36 andSDK8 descriptor@12.
Shared storage retains2048/legacy8 policy andordinary9 creation behavior,
including eager9/lazy8 level views andallocation-failure order. Ordinary CPU
box locking andslice uploads use the device API,including8-only65 and8s
sub-box alignment. SDK8 descriptors insertSize into32bytes instead ofcopying
SDK9s28bytes. Existing Usage passthrough is retained,not a native conformance
claim. Native coverage:2945283 constructors,169128 box/upload cases,26433
SDK descriptors,320 view cases,33 strict mutants,three historical controls,
four ASAN families. XP validatesfour slices/threemips/child-view box writes;
both GPUs match30images/1920pixels from56uploads. CPU GPUreadback replies
are explicit analytic injection. Native alignment/fullflags/formats/filtering
andfullpublic8wrapping remain open. Evidence:
`../deployments/d3d-webgpu-review-4K24.json`.

Stage 4K25 adds private API8 VB/IB constructors,both24-byte stdcall entries.
They validate the API8 device profile andforward toactual9 storage/locks/
descriptors withshared=NULL. Frozen8/9 comparison covers52806cases,realSDK
24/20-byte descriptions,initial state/zero bytes/OOM/transport/list/pins and
24 strict runtime mutants. Legacy lock coverage is reused;27mutants now
requirestrict compilation,23C assertions or4 controlled protected-page faults.
XP validatesVB/index16/index32,readonly/DISCARD/NOOVERWRITE andpartial writes;
the final rerun uses matching SDK FVF=68 afterpreserving the original probe
capture withdecimal44 metadata. Both GPUs match20images/1280pixels through
15indexed and5nonindexed draws. CPU GPUreadback replies are explicit analytic
injection. This preserves existing proxy validation,including knownnative
pool/FVF-length gaps;fullpublic8wrapping remains open. Evidence:
`../deployments/d3d-webgpu-review-4K25.json`.

Stage 4K26 shares API8 render/TSS translation through
`runtime/d9wg_state_bridge8.h`. Public8 emitters and private core8 state
setters use the same seven-state drop policy, ZBIAS conversion and ten
sampler relocations. Private setters have 12/16-byte stdcall ABIs and reuse
actual core9 filtering and state-block recording. The embedding frontend
must retain raw API8 getters, deleted states and unclamped ZBIAS metadata;
these entries do not implement a complete public8 device wrapper.

Native C comparisons cover 740360 cases and 1533458 checks, including
frozen old8 emitters/setters, ordinary9 setters, actual translated core9
recording, failure order and SDK enum values. All 35 effective mutations,
two historical setter controls and ASAN pass. The XP probe covers five
profiles, 400 sampler configurations, state-block Apply/Capture and Reset.
It waits for asynchronous window workers to unload all four DLLs before
process exit. The frozen trace checks every state packet against an
independent operation plan. This is state/API evidence with no new pixel
claim; ZBIAS precision remains a local compatibility approximation. See
`../deployments/d3d-webgpu-review-4K26.json` for results and preserved probe
corrections. Full public8 wrapping and later review stages remain open.

Stage 4K27 fixes UpdateTexture pool validation in public8 and shares the
SYSTEMMEM -> DEFAULT predicate with allthree core9 texture paths. Wine's
Windows test_update_texture_pool covers the2D/volume rule; cube uses the
same base-texture contract and existing9 rule. Private core8 UpdateTexture
has a12-byte stdcall ABI and calls actual9 mip/face/volume copy machinery.
Full public8 object wrapping remains separate.

Native checks cover221186 cases/663614 assertions, with17 strict runtime
mutations,three actual old8 wrong-pool controls andtwo ASAN families.
Existing copy tests retain42 cases perAPI and25 mutations with explicit
compiler-failure rejection. First XP run passes240 pool cases/640 local
DWORD checks acrossfive modes,480textures andallcube faces. The actual
old8 DLL reproduces45 wrong-pool successes;four current profiles each
reject45 invalid pairs without changing destination bytes. The frozen
wire model verifies1440 uploads and6205 corruptions. This probe has no
GPU readbacks or injected CPU responses. Retained copy replay separately
covers84 frames/252 sampled color probes onboth GPUs, rather than claiming
full-frame pixel coverage. Evidence:
`../deployments/d3d-webgpu-review-4K27.json`.

Stage 4K28 moves API8 GetFrontBuffer into a shared SDK-bound algorithm and
adds private core8 front readback (@8). Larger targets preserve their outer
pixels, X8 and texture/cube views retain existing8 behavior, and actual9
storage/readback/upload adapters handle core objects. Six ordinary9 methods
remain byte-identical; ordinary9 front validation keeps its stricter rules.
The shared algorithm exactly matches the mechanically rebound old8 body.

Native tests cover28835 cases/133300 checks,25 mutations,two ASAN families,
the retained61 API8 surface cases/29 strictly classified mutations and43
ordinary9 readback cases. An old strict9-body control demonstrates why it
cannot replace8 policy. The SDK8/9 probe passes35 cases acrossfive modes,
including4095 local DWORD checks andReset8x8->5x3. CPU readback replies are
explicit analytic injection; A8 source alpha stays0 before API8 makes the
copied image opaque. Both GPUs independently match49images/2940pixels,
including2D/cube copied centers and retained borders, with maps empty before
SessionEnd cleanup and owner release. Native desktop/window offsets/Present
timing and complete public8 wrapping remain open. Evidence:
`../deployments/d3d-webgpu-review-4K28.json`.


### Review 4K29: shared API8 CopyRects

API8 CopyRects now uses one shared legacy algorithm for public8 and private core8
(`d9wg_d3d9_core_copy_rects8`, stdcall24). The adapters preserve ordered partial
success, zero-count pointer behavior, optional points, CPU overlap snapshots,
GPU mip/cube-face identity, padding and shadow validity. Ordinary9 surface APIs
retain their existing contracts. Public8 device/resource wrapping remains open.

Actual frozen8/native comparison covers 77,766 cases / 316,230 checks, 22 strict
semantic mutants, two sanitizer families and retained61/29 surface checks. The
four-mode XP probe checks1,148 local DWORDs and fourReset operations. SwiftShader
and Metal each verify20 full images /1,084 pixels with zero error,16uploads/528bytes
and20blits; noDraw/Present. CPU replies in XP are explicit analytic injection.
This phase publishes exact accepted public8 and same-source built9 retail DLLs;
it does not claim a new ordinary9 SDK execution. Evidence:
`../deployments/d3d-webgpu-review-4K29.json`.


### Review 4K30: shared API8 declarations

Legacy8 declaration fields/register semantics and bounded parsing now live in
`d9wg_declaration8_policy.h` / `d9wg_declaration8.h`. Public8 delegates to them;
private `d9wg_d3d9_core_vertex_declaration8` (stdcall16) creates a real core9
layout, including the8 declaration-only primary NORMAL rule. Raw declaration,
local constant metadata and DWORD handle ownership remain in the8 frontend.
Byte-based word access avoids Win32 DWORD/uint32_t C type alias assumptions.

Frozen actual8 comparison covers1,105,927 cases /4,013,650 checks,25strict C
semantic mutants andtwo sanitizer families. The five-mode XP probe checksraw8
queries/coreCOM identity,16invalid declarations,API constant isolation andfive
Reset operations retaining managedbuffers/layouts. Both GPUs verifyten complete
images /640pixels, two offset input streams, red-to-green afterReset, withzero
error. XP CPU replies are explicitly analytic injection. Complete shader/handle
wrapping and the8-vs9 NULL-stream metadata difference remain adapter work.
Evidence: `../deployments/d3d-webgpu-review-4K30.json`.


### Review 4K31: shared local DEFs and core8 shader objects

The8 emitter and private core8 now share local DEF byte construction.
`d9wg_d3d9_core_vertex_shader8` (stdcall20) returns real9 declaration/VS pairs;
NULL function creates only a declaration. `d9wg_d3d9_core_pixel_shader8`
(stdcall12) creates real9 PS objects. Original8 version/body/8192 validation
precedes trusted DEF expansion. Ordinary9 validation and the one extracted
allocation/publication body preserve the old factory behavior. Pair failures
clear outputs and release temporary storage, declaration and parent pins.

Public8 queries retain original bytes; private9 GetFunction exposes expanded
DEFs plus END. The8 frontend still owns raw-query/handle semantics. Native pair
coverage is478cases/1,012checks,22strict C mutants andtwo sanitizers;retained
constant andordinary9 comparison suites pass. XP runsold/current8,ordinary9,
coreSDK120/220 withfiveReset,35shaders/15declarations/60handles. BothGPUs verify
20complete images/1,280pixels:localred survivesAPIoverride,localgreen survives
Reset. PS1.0-1.4 arecreated/queried/released without numerical drawing claims.
XP CPU replies are explicit analytic injection. Evidence:
`../deployments/d3d-webgpu-review-4K31.json`.


### Review 4K32: core8 FVF binding and declaration cache

`d9wg_d3d9_core_set_fvf8` (stdcall8) decodes the8 dialect, clears the VS,
and clears both VS/layout on zero. Actual9 user declarations carry a private
legacy8 cache key and fvfassociation0, so state blocks preserve elements
without re-decoding8 values as9. Cache references are private, preserve
public-zero object lifetime without an extra parent pin, and can be regained
after cache teardown. Ordinary9 methods and zero no-op behavior remain intact.
The8 frontend retains its raw DWORD FVF/handle Get shadow and error policy;
internal9 GetFVF=0 deliberately describes the real converted layout.

Native verification covers40,970cases/622,117checks,19strict semantic mutants
and two ASAN families. XP compares accurate4K31old/current8,ordinary9 reference
and coreSDK120/220:bit14 raw0x4042 versus normalized9 0x42,VS-to-FFP switch,
zero draw refusal,state-block restore and retained cache identity through
five Reset. Both GPUs verify20complete images/1,280pixels with zero error,
5programmable+15FFP draws and empty maps before SessionEnd/owner cleanup.
XP CPU replies are explicit analytic injection. Full public8 wrappers,
raw handle policy and ProcessVertices destination8 FVF remain open.
Evidence: `../deployments/d3d-webgpu-review-4K32.json`.


### Review 4K33: core8 program/declaration pair binding

`d9wg_d3d9_core_set_vertex_shader8` (stdcall12) binds a resolved8 shader handle
as real9 declaration/VS objects. Both types and device owners are validated
before either binding changes. NULL VS permits declaration-only; NULL/NULL
uses8 zero clearing. A live duplicate is a no-op while a recorded duplicate
still captures both slots. Actual9 setter publication order is retained on
transport failure; invalid-object prevalidation changes neither slot.

Native coverage is1,036cases/33,440checks,19strict semantic mutants andtwo
ASAN families. Independent declaration-only/program-only changes,public-zero
revival,record/apply/clear anddevice-first/last-view/last-block cleanup pass.
XP compares accurate4K32old/current8,ordinary9 andcoreSDK120/220:duplicate
binding recording,red-to-green switch andrecorded red restore,Capture ofgreen
andReset restore,localDEF/APIbank isolation andfinalrefs0. BothGPUs verify
20full images/1,280pixels withzeroerror. PS1.0-1.4 metadata remainseparate from
numerical drawing. VMCPUreplies areexplicit analytic injection;fullraw8
handle/query/stateblock wrappers andProcessVertices destination8FVF remainopen.
Evidence: `../deployments/d3d-webgpu-review-4K33.json`.


### Review 4K34: raw8 BaseVertexIndex to signed core9 indexed draws

`d9wg_d3d9_core_draw_indexed8` (stdcall28) takes the raw UINT saved by8
SetIndices and copies its bits into the9 signed per-draw base. It forwards
to actual9 DrawIndexedPrimitive without changing index binding state.
The opaque header uses UINT primitive type and requires no Direct3D SDK.
Frontend8 retains raw GetIndices/base and state-block metadata ownership.

Native verification covers50,004cases/115,727checks,independently enumerated
physical stream addresses,16/32bit IB bounds,signed extrema,zero calls and
submission failure;16strict semantic mutants andtwoASAN families pass.
Final-source XP compares accurate4K33old/current8,ordinary9,coreSDK120/220:
+1/-1/0 selectfour interior vertices fromsix sentinel-padded vertices,
managedVB/IB survivefiveReset,raw8 queries/zero/two refusals andfinalrefs0 pass.
BothGPUs verify20complete indexed images/1,280pixels withzeroerror. Extreme
bases are native metadata/range tests;GPU images use valid+1/-1/0 geometry.
VMCPUreplies areexplicit analytic injection. Evidence:
`../deployments/d3d-webgpu-review-4K34.json`. Fullraw8 state-block/wrappers
andProcessVertices destination8FVF remainopen.


### Review 4K35: real9 blocks from frontend8 snapshots

`d9wg_d3d9_core_record_state_block8` (stdcall16) runs a synchronous frontend
recorder through actual9 Begin/End. The frontend selects its raw8 snapshot
and uses translated recordable setters. Failure cancels the new recording;
failed Begin preserves an existing recording. Published blocks own values
and real object references without retaining the callback/context. Frontend8
keeps raw scopes,DWORD/Get metadata andCapture replacement policy.

Native coverage is1,037cases/19,488checks,15strict semantic mutants andtwo
ASAN families:detached/rebuilt snapshots,public-zero/device-first/last-view/
last-block cleanup,allocation/callback errors andcoldFVF cache ownership.
XP compares accurate4K34old/current8,normalized9 reference andcoreSDK120/220:
red remainslive whilegreen isrecorded,abort preserveslive,Apply turns green,
rebuiltCapture survivesReset;raw8 ZBIAS111/POSITIONORDER versusmapped9 bias
andADDRESSU sampler queries pass. Old8/reference abandonment explicitly uses
End/Delete orEnd/Release;private8 callbackfailure usesactualcancel. BothGPUs
verify20complete images/1,280pixels zeroerror. CPU replies areexplicit analytic
injection;fullpublic8 DWORD/predefined-scope wrappers remainopen. Evidence:
`../deployments/d3d-webgpu-review-4K35.json`.


### Review 4K36: ProcessVertices8 through the shared software pipeline

The private `d9wg_d3d9_core_process_vertices8` (stdcall24) selects the existing8
FVF output subset, then executes the factored actual9 processing body without
a temporary output declaration. Ordinary9 keeps its validation and output
layout policy. Reconstructed old8 decoder and whole9 ProcessVertices match
exactly;390 other8 /638 other9 functions and eight shared runtime files match.

Native coverage:392,170checks/364,469cases,22strict semantic mutants andtwo
ASAN families. Frozen9/new9/private8 compare full output bytes,aliasing,
matrices,attributes,padding,DONOTCOPYDATA,ClipStatus anduploads. Numerical
cases areunlit;zeroW isexisting localpolicy. Upload failure retains already
processed CPU data,matching old9. XP compares accurate4K35old/current8,
ordinary9 normalizedFVF andcoreSDK120/220:5devices/5Reset/10managedVB,
800actual CPU vertex DWORD checks,raw8bit14 layouts and20bounddraws.
Independentwire verifies45updates/5,760bytes;8,084corruptions caught.
BothGPUs verify20complete images/1,280pixels zeroerror. Framebuffer CPU
replies areexplicit analytic injection,separate from actualCPUvertex results
andGPU images. Evidence:`../deployments/d3d-webgpu-review-4K36.json`.
Fullpublic8 DWORD state-block ownership/predefined scopes andwrappers remain.


### Review 4K37: device-owned8 DWORD state-block tokens

Private create/capture/apply/delete entries (stdcall16/16/8/8) map DWORD tokens
to real9 state blocks built by the synchronous frontend recorder. Tokens hold
block storage without a device parent pin, and final device release drains
them. Capture replaces a prepared snapshot behind the same DWORD; failure
preserves the original. Operation pins protect reentrant deletion, and a token
retired during Capture cannot be resurrected. Resource finalizers run after
detachment; token operations are refused during final device cleanup.

Native:19,161checks/1,117cases,25strict semantic mutants andtwoASAN families.
Eight resource families,ordinary9 coexistence,public-zero/device-first cleanup,
cross-device/profile/stale tokens,collision skip,actual texture private-data
callbacks,Apply-time deletion anddevice revival are covered. Source audit
preserves640 existing9 functions;owned cleanup adds only the drain call and
state-block storage adds one zero-initialized DWORD. Wholepublic8 unchanged.
XP:accurate4K36old/current8/ordinary9/coreSDK120/220,5devices/5Reset,token
Capture failure/stable replacement/deletion andunreleased empty-token cleanup.
Independentwire catches11,753corruptions. DualGPU:20fullimages/1,280pixels
zeroerror. CPUframebuffer replies areexplicit analytic injection. Public8
wrappers/raw scopes/Get metadata still need frontend integration. Evidence:
`../deployments/d3d-webgpu-review-4K37.json`.


### Review 4K38: incremental8 Begin/End to core DWORD tokens

Private Begin8 (stdcall4) uses actual9 Begin;End8 (stdcall8) uses actual9 End
and the same token publisher as snapshot creation. Invalid End preserves the
recording and clears the caller output. Tokens remain device-owned without
public device parent cycles. Frontend8 still owns raw scopes/Get metadata.

Native:79,400checks/1,038cases;1,024 all-eight-resource/pool/cleanup groups,
live isolation,repeated recorded bindings,invalid End/OOM/profile/teardown,
ordinary9 interoperability andunfinished-recording cleanup.15new plus25retained
strict semantic mutants pass. CombinedASAN also covers retained token and9
state-block lifetime tests.647 existing9 functions match;reconstructedCreate8
and moved publisher match exactly;block storage/wholepublic8/policy unchanged.
XP compares accurate4K37old/current8/ordinary9/coreSDK120/220:private profiles
exercise Begin/nestedBegin/invalidEnd/End-token/Apply andReset restoration.
Independentwire catches11,756corruptions;bothGPUs verify20fullimages/1,280pixels
zeroerror. CPUframebuffer replies areexplicit analytic injection. Fullpublic8
wrappers/predefined scopes remainopen. Evidence:
`../deployments/d3d-webgpu-review-4K38.json`.


### Review 4K39: weak identity cache for frontend8 object views

Private get/attach-view8 entries (stdcall8/12,retire callback4) associate one
frontend metadata object with actual9 resource storage. They add no public
resource/device references;cached identity survives public zero while core
bindings retain storage. Eight destruction paths retire metadata once after
private-data finalizers. A linked tombstone refuses same-object get/reattach
while callbacks run. The registry finds its link again after nested retirement.
Callbacks release frontend metadata,not the dying core object.

Native:112,181checks/1,038cases,35strict semantic mutants andcombinedASAN.
Five resource parents,three cached level views,two implicit surfaces,standalone
RT,private-data ordering,public-zero revival,nested removal/OOM/profile guards
are covered. Additional-chain native coverage istype recognition;real SDK/XP
covers the actual new destruction callback.643 existing9 functions andten
storage structs match;destructors differ only byretirement calls/braces.

XP:five retainedrenderer profiles plusSDK120/220 lifecycle devices,7devices/
7Reset/80protocol handles/28callbacks,32actualCPUshadow DWORD checks. Wire
model catches16,339corruptions. BothGPUs verify20fullimages/1,280pixels zeroerror,
including real additional canvas creation (2created/0refused/2destroyed) and
empty mapsbeforeSessionEnd. Framebuffer CPU replies remainanalytic injection.
These entries provide cache metadata;fullpublic8 COMwrappers still need
integration. Evidence:`../deployments/d3d-webgpu-review-4K39.json`.


### Review 4K40: actual SDK8 Surface8 adapter on core9 storage

`d3d8_core_surface.c` implements the real11-method IDirect3DSurface8 vtable.
It forwards descriptor/private-data/lock operations through a typed Windows-only
core9 boundary anduses the weak registry for identity. Texture/cube levels use
the frontend container count;other surfaces coalesce public refs into one core
surface reference andone frontend device pin. Getter-owned input references
are consumed on every path. Cached construction needs no allocation andracing
publication cleans its candidate before using the winner. Release uses saved
locals because core retirement can immediately free the wrapper.

Native:54,571checks/1,037cases,34strict semantic mutants andASAN. The backend
is anexplicit COM/metadata shim. RealSDK8 XP covers14Surface8 interfaces over
seven surface kinds,descriptor32bytes/privateData/locks/own-coupled counts/
publiczero/Reset identity,128actualCPUshadow DWORDs. Frontend device/container
parents areexplicit IUnknown-only reference fixtures,not full parent adapters.
The old public factory isnotyet switched. All656 oldcore9 functions/tenstorage
structs/wholeold8source match;the shipping8 build adds the new source only.

Surface8 texture Lock/Unlock uploadsred/blue into real9 fixed-function draws:
4new images plus20retainedprogrammable images,24fullimages/1,536pixels zeroerror
onbothGPUs. Wire catches17,108corruptions,including sixuploads/384bytes. Cube
green isCPU/upload coverage,not sampled here. CPUframebuffer replies remain
analytic injection,separate fromactualCPUshadow checks andGPU images.
Evidence:`../deployments/d3d-webgpu-review-4K40.json`.


### Review 4K41: real SDK8 Texture8 and Surface8 container integration

`d3d8_core_texture.c` implements all19 Texture8 methods andreturns actual
Surface8 children whose container is the real Texture8. Both interfaces share
core lock state, coupled refs andweak identity caching. Common GUID/own-ref
helpers are shared withSurface8. A typed15-method core9 boundary forwards the
operations;surface/texture descriptions reuse the existing eight-DWORD pack.
657oldcore9 functions andten storage structs match;the factored surface desc
reconstructs exactly. Wholeold8/runtime policies remain unchanged. The public
factory is notyet switched;Device8/Cube8 parents remain IUnknown fixtures.

Native:54,048checks/1,036cases,43strict mutants andASAN;retainedSurface8
54,571checks/34mutants/ASAN. A surviving wrongmip mutant led to anindependent
child-surface dimension check. Legacy4K22 names were restored exactly andthe
new capture uses frontend_texture. SDK8/XP:106s/36batches,7devices/7Reset,
2realTexture8/16Surface8views,fourmips,cross-interface locks,private/priority/
LOD/dirty/QI/publiczero/Reset/final0,352actualCPUshadow DWORDs.

24fullimages/1,536pixels zeroerror onbothGPUs;8uploads/1,280bytes and18,046
wire corruptions. CPUframebuffer replies areanalytic;actualCPUshadow andGPU
checks areseparate. 376guest/87core regressions,32EXEs/fourstrictDLLs/site2/
scopedlint0. ExactXPaccepted retail DLLs andsame-source diagnostics published.
Evidence:`../deployments/d3d-webgpu-review-4K41.json`.


### Review 4K42: real SDK8 CubeTexture8 and Surface8 face containers

The real19-method CubeTexture8 adapter forwards through a typed15-method core9
boundary andreturns actual Surface8 face/mip views,with real CubeTexture8
containers. Both interfaces share core lock state,coupled refs andweak identity.
All663oldcore9 functions,tenstorage structs,wholeold8/runtime/Surface8/Texture8/
commonview match. The public8 build adds one source;defaultfactory remainsopen.

Native:61,952checks/1,164cases,47strict mutants andASAN,including face-loss
routing failures. The backend is anexplicit COM/metadata shim. RetainedTexture8
andSurface8 checks/mutations pass. ActualSDK8 XP:128s/52batches,nine devices/
nineReset,all24face-mip views pernewdevice,shared/independent locks,QI/private/
priority/LOD/desc/publiczero/Reset/final0. NewDevice8 parent is anIUnknown fixture.

Sixfacecolors pluspostResetwhite add14GPU images:38fullimages/2,432pixels
zeroerror onbothGPUs. 24uploads/4,992bytes,27,582wire corruptions,1,280actual
CPUshadow DWORDs acrossallprobes. CPUframebuffer replies areanalytic,separate
fromactualCPUshadow andGPUchecks. CubeAddDirtyRect retains the current core9
no-op policy. 378guest/88core regressions,32EXEs/fourstrictDLLs/site2/scopedlint0.
ExactXPaccepted retail DLLs andsame-source diagnostics published.
Evidence:`../deployments/d3d-webgpu-review-4K42.json`.


### Review 4K43: real SDK8 VolumeTexture8 and Volume8 containers

The 19-method VolumeTexture8 and 11-method Volume8 adapters share real core9
storage, locks, private data and weak identity caching. Volume8 references belong
to its real frontend VolumeTexture8 container. SDK-neutral typed 15/6-method
tables keep box and output casts inside core9. The shared descriptor pack
reconstructs the old volume_desc8 exactly. All 669 other core9 functions, ten
storage structs and nine complete old frontend/runtime files match the baseline.

Native validation: 66,667 checks / 790 cases, 70 strict semantic mutants and
ASAN. The backend is an explicit COM/metadata shim. Retained volume descriptor
and core mutation suites pass. A stale-output mutant exposed an ineffective
NULL input in the test; a non-NULL sentinel now detects it. A standalone consumer
also caught a Direct3D SDK dependency in the header, corrected before acceptance.

The third XP run is the accepted capture: 136 seconds / 64 batches, eleven
devices and Resets, four 3D mips, real parent/child identity, shared and independent
locks, row/slice pitch, private data, public-zero revival and final cleanup.
New Device8 parents remain IUnknown fixtures; default factory integration is open.
48 full images / 3,072 pixels match on both GPUs; 44 uploads / 7,168 bytes,
35317 wire corruptions and 1,824 actual CPU shadow DWORDs are checked.
Analytic framebuffer replies are separate from CPU shadow and actual GPU checks.
381 guest / 89 core regressions, 32 EXEs, four strict DLLs, two site tests and
scoped lint pass. Exact accepted XP retail and same-source diagnostics published.
Evidence: `../deployments/d3d-webgpu-review-4K43.json`.


### Review 4K44: shared real SDK8 VB8/IB8 adapters

`d3d8_core_buffer.c` supplies both real 14-method SDK8 interfaces with one shared
constructor, identity, cache and reference policy. Typed thunks forward through a
SDK-neutral ten-method core9 table. Existing VB/IB operations own private data,
priority, locks and 24/20-byte descriptors. All 678 existing core9 functions, ten
storage structs and eleven complete frontend/runtime files match the baseline.
The public8 build adds one source; default factory integration remains open.

Native validation: 24,373 checks / 536 cases, 36 strict semantic mutants and
ASAN. The backend is an explicit COM/metadata shim. First XP run: 125 seconds /
74 batches, thirteen devices and Resets, two actual VB8 and four IB8 interfaces,
16/32-bit indices, private data, descriptors, shared refs, public-zero revival,
Reset and final cleanup. New Device8 parents remain IUnknown fixtures.

SDK8 buffer writes feed SDK9 reference draws: 56 full images / 3,584 pixels match
on both GPUs. Eight buffer updates / 552 bytes and 40,748 wire corruptions check
the complete vertex/index payloads. CPU shadow checks total 1,962 DWORDs;
analytic framebuffer replies are separate. Host drawCalls=50 counts nonindexed
draws; indexedDrawCalls=6 is separate. 383 guest / 90 core regressions, 32 EXEs,
four strict DLLs, two site tests and scoped lint pass. Exact accepted XP retail
DLLs and same-source diagnostics are published.
Evidence: `../deployments/d3d-webgpu-review-4K44.json`.


### Review 4K45: real SDK8 SwapChain8 and Surface8 backbuffers

The five-method SwapChain8 adapter uses a typed two-method core9 boundary.
Backbuffers are real Surface8 views with independent references and frontend
Device8 containers. Invalid getter indices preserve caller output. Weak chain
metadata remains valid while backbuffer storage is retained. The entire old
core9 source/header reconstructs exactly after removing the new bridge blocks;
twelve other frontend/runtime files match. The build adds one adapter source.

Native: 8,482 checks / 269 cases, 28 strict mutants and ASAN. The backend is an
explicit COM/metadata shim. Accepted second XP run: 133 seconds / 92 batches,
fifteen devices and Resets, real chain/backbuffer identity, independent counts,
error output, public-zero revival, release-before-Reset and final cleanup.
Device8 parents remain IUnknown fixtures; the public factory remains open.

Renderable locks read GPU storage, including before writes. The first probe
omitted these replies and failed; the corrected reference includes pre-write,
readonly and post-Present images. 68 full images / 3,776 pixels match on both
GPUs. Four additional Presents execute without drops; separate canvas output
pixels are not read. 64 new CPU checks happen before upload; readonly checks
consume analytic GPU replies and are reported separately. Existing crop/dirty
region policy and native HAL differences remain outside this adapter change.
45686 wire corruptions, 385 guest / 91 core regressions, 32 EXEs,
four strict DLLs, two site tests and scoped lint pass. Exact accepted XP retail
DLLs and same-source diagnostics published.
Evidence: `../deployments/d3d-webgpu-review-4K45.json`.


### Review 4K46: weak frontend device metadata

The existing core8 weak-view registry now accepts profile8 Device9 objects.
Device metadata retires after both implicit surface metadata callbacks and
before device storage is freed. The parent factory stays alive throughout
all callbacks. A source review caught its previous early release; the exact
old ordering is now an assertion mutant. Final XP capture supersedes the first
156-second capture. Existing private-data finalizers run first,
under the core temporary reference, and may retain or transiently revive the
device. Retirement callbacks only tear down metadata. No extra public pins.
Only two production functions change; removing those edits reconstructs the
entire previous core source exactly, including macros and storage layouts.
680 of 682 extracted functions and fifteen frontend/runtime/build files match;
the core header adds comments only. The public factory remains pending.

Native: 7,560 checks / 772 cases, ten strictly compiled assertion mutants and
ASAN; retained weak resource views: 112,181 checks / 35 mutants. Actual XP:
192 seconds / 96 batches, nineteen devices and Resets, 124 handles. Four new
SDK120/220 metadata lifecycles exercise logical-zero/backend-held identity,
Reset, transient/retained finalizer revival and twelve ordered callbacks.
The frontend device is explicitly an IUnknown-only fixture; implicit surface
contexts are metadata only. Full Device8 integration is still pending.

The existing resource workload retains 68 full images / 3,776 pixels, matching
on both GPUs; four additional Presents complete without drops. Separate canvas
pixels are not read. New metadata cases add no draw/readback. Analytic XP GPU
replies and actual independent GPU replay remain distinct evidence.
54257 semantic corruptions. The first parallel core run timed out
at 500 seconds for this fixture; cached opcode lookups and a fresh complete
run with an 1800-second deadline replace that attempt. 387 guest / 92 core regressions, 32 EXEs,
four strict DLLs, two site tests and scoped lint pass. Exact accepted XP retail
DLLs and same-source diagnostics published.
Evidence: `../deployments/d3d-webgpu-review-4K46.json`.


### Review 4K47: private shader storage for frontend8 handle metadata

Two private SDK-neutral stdcall helpers retain/release actual declaration, VS
and PS storage on profile8 devices. They borrow inputs and use existing binding
references; public COM/device references do not change. The frontend drops
constructor COM references separately after a successful hold, and releases
its balanced holds in the device metadata retirement callback while the core
device and factory remain valid. The complete previous core source/header
reconstruct exactly after removing the bridge: 682 existing functions, macros
and storage layouts unchanged; seventeen frontend/runtime/build files match.

Native: 10,008 checks / 525 cases, 17 strictly compiled assertion mutants and
ASAN. Actual first XP: 130 seconds / 97 batches, nineteen devices and Resets,
136 handles, 27 declarations and 43 shaders. Twelve new real core objects
survive public-zero cloning/getter revival, Reset, byte-exact metadata queries
and transient/retained device revival by private-data finalizers. All twelve
resource destroys follow their DeviceDestroy and precede SessionEnd; GPU
resource tables are empty before fallback cleanup. Frontend devices and raw8
handle metadata remain explicit fixtures; full Device8/handle tables are open.

Both GPUs match 68 images / 3,776 pixels with 43 shader translations. No new
draws or CPU shadow writes; all previous resource rendering remains covered.
The first full guest run exposed a scheduling-dependent old GUID-collision
mutation test. Sixteen deterministic GUID coexistence cases now cover every
byte; 30 private-data cases / 8,000 concurrent iterations, 19 strictly compiled
C assertion mutants and ASAN pass before a fresh full guest run.
9,125 corruption cases cover new shader/Reset fields and payload words, with
retained-prefix sampling plus complete old fixtures in the regression suite.
389 guest / 93 core regressions, 32 EXEs, four strict DLLs, two site tests and
scoped lint pass. Exact XP-accepted retail DLLs and same-source diagnostics
published. First SDK compilation caught incompatible vtable pointer types;
using opaque table addresses corrected it without relaxing strict flags.
Evidence: `../deployments/d3d-webgpu-review-4K47.json`.


### Review 4K48: frontend device-owned cleanup observer

An optional weak device observer runs before native recording, owned blocks,
live bindings and FVF cache cleanup, outside its transport lock scope under
the core temporary device pin. The private explicit cleanup entry reuses that
same implementation while public children remain alive. Nested cleanup is
rejected. Callbacks can remove every other pin; dropping the temporary pin
then uses normal final Release, so repeated notifications require idempotent
frontend metadata. The entry never accesses the device after that Release.
The generic weak attach ABI keeps its signature and supplies no observer.
Only weak registry metadata gains a callback field; device/resource layouts
remain unchanged. 683 of 685 old source functions and seventeen full files
match; complete old source/header reconstruct exactly from the bridge edits.

Native: 8,468 checks / 521 cases, 19 strict assertion mutants and ASAN. Retained
35 resource-view and ten device-view mutants still pass after locating the
shared attachment implementation. First actual XP: 117 seconds / 97 batches,
nineteen devices and Resets, 140 handles, 27 declarations and 43 shaders.
Four explicit cleanup calls while real VB children are publicly held cancel
recording, invalidate native tokens and clear bindings. Frontend raw snapshot
fixtures detach once; ten total explicit/final notifications are idempotent.
Reset does not notify. Transient/retained finalizer device revival and late
shader storage retirement remain covered. The frontend is an IUnknown device
and CPU snapshot fixture; full Device8/raw tables/public factory remain open.

Both GPUs match 68 full images / 3,776 pixels with empty maps before fallback.
Four VB creates and destroys add eight commands (1,221 total); no new draw,
upload or CPU shadow write. 9,205 corruption cases cover selected new fields,
payload words and SDK summaries plus prefix sampling; full old fixtures run
separately. 391 guest / 94 core regressions, 32 EXEs, four strict DLLs, private
stdcall16/4 and isolated SDK8 ABI, two site tests and scoped lint pass. Exact
XP-accepted retail DLLs and same-source diagnostics published.
Evidence: `../deployments/d3d-webgpu-review-4K48.json`.


### Review 4K49: typed Device8 backend dispatch and constant bytes

The private SDK-neutral Device8 backend table has 57 strictly typed methods.
It delegates actual core9 operations, fixes extra swap/target indices and stream
offsets to zero, and accepts genuine profile8 devices only. Legacy8 memory,
constant bounds, empty calls and readable PURE-hint getters remain distinct
from ordinary9 policy. The common float4 setter now compares and copies object
bytes, preserving signed zero, signaling/quiet NaN payloads, infinities and
subnormals while suppressing identical updates. This is the typed backend
boundary; the full 97-method public Device8 facade and default factory remain
pending.

Native: 56,505 checks / 5,686 cases, 25 strictly compiled assertion mutants and
ASAN, plus retained scalar recording checks. Actual SDK8 clients exercise all
57 slots in four normal/PURE contexts; two ordinary9 devices guard existing9
behavior. The accepted third XP run took 131 seconds / 106 batches, with 21
devices and Resets, 152 handles, 27 declarations and 43 shaders. Earlier strict
indentation failure and two probe-expectation failures (unbound depth status,
zero stride) were corrected and retained as failed attempts. Reset preserves
and replays constant bytes and gamma. Old source/header reconstruct exactly:
687 of 689 existing functions unchanged, 59 new thin/selector/byte-helper
functions; seventeen whole old files and all storage layouts unchanged.

SwiftShader and Metal match 72 full images / 4,032 pixels, with four primary and
four additional Presents, zero drops/errors and empty maps before fallback.
1,401 commands; 56 draws, 48 texture uploads / 7,424 bytes, and 2,026 prior CPU
shadow words remain covered. Canvas outputs are not separately read back.
23,849 selected semantic corruptions plus full retained fixtures, 393 guest /
95 core suites, 32 EXEs/four DLLs, 21-member static core/isolated SDK8 layout,
two site tests and scoped lint pass. Exact accepted retail and same-source
diagnostic binaries published. See review evidence stage4k49-validation.json.


### Review 4K50: paired targets and borrowed surface ownership

The private API8 pair setter preflights both surfaces, ownership, usage, size
and sample type before mutation. It publishes both CPU bindings before the
first transport attempt, then color/depth/live viewport in legacy8 order.
NULL color preserves the target and viewport; NULL depth unbinds depth.
Matching viewport updates are suppressed; target side effects remain live
while explicit state recording is active. Actual9 setters share the private
slot replacement and live publication helpers with unchanged9 policy.

Borrowed surface ownership reports texture/cube children or Device8 containers
for standalone targets and implicit/additional surfaces. The real Surface8
backend selector checks its class and takes no references. Full Device8 and
factory integration remain pending. Old core/source/header and Surface8
source/header reconstruct exactly: 745 of 748 old core functions unchanged,
five new core functions and one frontend selector, all storage layouts and
sixteen other complete files unchanged.

Native pair/owner: 36,400 checks / 4,135 cases, 25 strict assertion mutants and
ASAN. Real Surface8 adapter: 55,595 checks / 1,037 cases, four new selector
mutants and ASAN. Retained binding23/owner8/scalar10 mutants pass. Accepted
third actual XP: 128 seconds / 118 batches, 21 devices/Resets, 172 handles,
27 declarations and 43 shaders. Real SDK8 texture/cube/standalone containers,
20 pair bindings/four invalid-depth pairs, cache recovery, balanced references,
NULL state and live/recorded viewport behavior are covered. Earlier helper-name,
9-only cube readback route and full-clear reference mistakes were corrected;
failed attempts retained. The final cube scene explicitly zeros the whole face
then checks the center2x2 viewport clear, using existing shared8 CopyRects.

Both GPUs match 84 full images / 4,224 pixels with empty maps, four primary and
four additional Presents, no drops/errors, 1,529 commands (+128), 42 textures
and 56 draws. 1,218 selected corruptions cover all new command payload words,
framing, SDK summaries and retained opcode samples. 395 guest suites and one
new core fixture pass; prior95 complete core tests/fixtures and host code are
unchanged, so their stage49 baseline is inherited rather than claimed as96
fresh runs. 32 EXEs/four DLLs/static21/isolatedSDK8, site2 and scoped lint pass.
Exact accepted retail and same-source diagnostic binaries published. Full97
Device8/child refs/rawshader+block metadata/defaultfactory remain next.

### 2026-11-05: complete Device8 frontend (review 4L1)

The private SDK8 frontend now supplies all 97 Device8 methods over the common
core and existing cached resource adapters. It owns only API8 raw shader handles,
state-block shadows and frontend reference bookkeeping. Common snapshots reuse
the core capture engine with API8 scopes; Reset observes actual epoch and recorder
changes even on failure. Device caps reuse the existing Root8 policy.

Actual XP SDK120/220 normal/PURE probes call all 97 slots: 2,815 checks, four
devices/four resets, deleted-shader snapshots and child GetDevice revival. Both
SwiftShader and Metal pass 28 full images/1,408 pixels. All 398 guest suites,
27 new C mutations, retained 34 surface mutations, native lifetime/scope ASAN,
32 standard EXE builds and site checks pass. See deployment evidence 4L1.

The public default factory still uses the previous backend. Switching that entry
and retiring the duplicate implementation form the next functional batch. This
is not a claim of complete native compatibility, shader differential validation
or game/FPS acceptance.


### 2026-11-05: public Device8 switch and backend retirement (review 4L2)

`Direct3DCreate8` / `IDirect3D8::CreateDevice` now returns the complete shared-core
Device8 facade. The production Root8 source is 418 lines; the retired 8,350-line
backend is immutable test comparison data and is never linked into a DLL.
The shared core publishes the legacy HELLO; its existing functions are unchanged.
The retail DLL shrank from 261,120 to 193,024 bytes.

The public-entry XP run exercised all 97 SDK methods in four SDK120/220 normal/PURE
contexts: 2,815 checks, four Reset operations, 80 handles and 1,762 commands.
SwiftShader and Metal each validated 28 images / 1,408 pixels with zero error.
The 398 guest suites passed after four retired-source harness updates; historical
Device8 comparisons are explicitly separated from live Root8/adapter/core tests.
The exact accepted retail DLL and a same-source diagnostic DLL are published.
See deployment evidence 4L2. This does not close all phase 4 compatibility,
software vertex processing, native HAL/game acceptance, or phase 5 shader work.


### 2026-11-05: fallible shared stateblock replay (review 4L3)

The common replay engine stops at the first failed setter and returns its exact
HRESULT. Device8 applies its raw states and retained shader objects at the shared
replay boundaries, so partial failure leaves queries consistent with accepted
updates. Apply retains a detached raw snapshot and a device pin across reentrant
block deletion; the core continues to own resource snapshot references.

Native providers exercise 352 core failure positions and 400 raw8/lifetime
scenarios, including ASAN. All 38 mutations require a compiled C runtime failure.
401 guest suites pass. Public8 XP calls all 97 methods / 2,815 checks; ordinary9
XP covers 65 captures, 528 getter queries and two Reset operations. Both captures
produce 33 images / 1,728 pixels with zero error on SwiftShader and Metal.
The updated9 probe checks recording versus completed-block device pins, and its
wire model explicitly tracks the private FVF cache. See deployment evidence4L3.
Failure injection is native with controlled providers; XP validates real COM
objects in normal execution, without claiming Windows HAL failure-order identity.


### 2026-11-05: upstream Wine stateblock acceptance (review 4L5)

Unmodified Wine D3D8 and D3D9 stateblock suites are now pinned in the sibling
v86 repository and run inside XP with Windows expectations and live GPU
transport. Both SwiftShader and Metal runs pass all 9,283 D3D8 and 14,738
D3D9 checks, with no failures, todos, flaky failures or skips. These suites
exercise API state; they do not render pixel comparisons. Review 4L4 records
the separate live pixel matrix.

The first upstream run exposed16 D3D9 failures: recapturing a predefined
stateblock replaced its creation-time stream offsets. The common core now
retains those offsets while refreshing buffers/strides. Recorded blocks
continue to recapture offsets. All16 stream slots, storage replacement,
three recapture rounds, and the recorded/predefined distinction are checked
in native code;37 runtime mutants and ASan/UBSan pass. The full401 guest
suite regression and32 SDK sample builds pass.

Run `node tests/glbridge/d3d_wine_stateblock_xp.mjs` in v86 (`GL_SWIFTSHADER=1`
for software GPU). The runner checks upstream hashes, full completion and
exit summaries, and zero resources before driver owner-release cleanup.
See deployment evidence4L5. Published retail8/9 are the exact accepted Metal
builds; diagnostics are rebuilt from the same source. The complete Wine
device/visual/DirectDraw collection and game acceptance remain separate work.

### Review batch 4L6: Wine compatibility boundaries

The common 8/9 texture constructors now reject non-block-aligned top-level
DXT sizes while preserving tiny mip levels. D3D8 validation exports return
owned error strings and validate versions, caps, declarations and constants;
its display modes come from the actual driver and format pairs follow Wine's
Windows expectations. The selected XP compatibility group passed 2,996 checks
on both SwiftShader and Metal with zero GPU errors and clean resource release.
The full Wine device audit still has 805 observed failures and a compressed
volume failure; it is explicitly not a passing full-suite result. See
`../deployments/d3d-webgpu-review-4L6.json` for exact scope and binary hashes.
