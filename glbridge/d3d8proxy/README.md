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
