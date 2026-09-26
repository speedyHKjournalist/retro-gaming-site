# Direct D3D9 to WebGPU guest frontend (protocol 1.3)

This app-local `d3d9.dll` bypasses WineD3D, `opengl32.dll`, gl4es, and
WebGL, the same way `../d3d8proxy/d3d8.dll` does for D3D8. It emits D9WG
commands into the existing `v86gl.sys` 16 MiB DMA arena. The VGL2 descriptor
is only the transport envelope; command `0xFFE1` carries a versioned D9WG
batch decoded by `../d3d9-webgpu/d3d9_executor.js`. D9WG is an independent
protocol from D8WG (own opcode numbering, own resource handle namespace,
own payload shapes) — see `d3d9_protocol.h` and
`docs/d3d9-webgpu-implementation-plan.zh-CN.md` section 6.

The current protocol intentionally breaks compatibility with older DLL/page
pairs. It adds an asynchronous host-to-guest response tail for GPU queries and
readback, and implements the SM3/HDR/MSAA and resource paths needed by a
3DMark06-class workload. Implemented:

- `IDirect3D9`/`IDirect3DDevice9` COM lifecycle, adapter enumeration, caps
  (`VertexShaderVersion`/`PixelShaderVersion` report `(3,0)` by default; the
  `ffp` and `sm2` diagnostic profiles remain selectable), and `CreateDevice`;
- `Reset`, `Present`, `Clear`, `BeginScene`, `EndScene`;
- vertex/index buffer create/`Lock`/`Unlock` with dirty-range upload;
- 2D textures: create, `LockRect`/`UnlockRect` with subrect upload,
  `A8R8G8B8`/`X8R8G8B8`/`R5G6B5`/`X1R5G5B5`/`A1R5G5B5`/`A4R4G4B4`/`L8`/`A8`/
  DXT1/DXT3/DXT5, `R16F`/`G16R16F`/`A16B16G16R16F`, and
  `R32F`/`G32R32F`/`A32B32G32R32F`;
- vertex declarations (`CreateVertexDeclaration`/`SetVertexDeclaration`):
  `POSITION`/`POSITIONT`/`NORMAL`/`COLOR`/`TEXCOORD`/`PSIZE`/`BLENDWEIGHT`/
  `BLENDINDICES`/`TANGENT`/`BINORMAL`/`FOG` usages, default method,
  `FLOAT1`-`FLOAT4`/`D3DCOLOR`/`UBYTE4`/`SHORT2`/`SHORT4`/`UBYTE4N`/
  `SHORT2N`/`SHORT4N`/`USHORT2N`/`USHORT4N`/`UDEC3`/`DEC3N`/`FLOAT16_2`/
  `FLOAT16_4` types, up to 4 streams;
  `SetFVF`/`GetFVF` as a compatibility path that expands the FVF bits into
  the same element shape (`XYZ`/`XYZRHW`, `NORMAL`, `DIFFUSE`, `SPECULAR`,
  up to 8 default-size 2D `TEXn`) and sends it to the host the same way
  `CreateVertexDeclaration` does — the host never decodes raw FVF bits
  (see plan section 4.3);
- `SetStreamSource`, `SetIndices`, `SetTransform`, `SetViewport`,
  `SetRenderState`, `SetTextureStageState`, `SetTexture`;
- `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`,
  `DrawIndexedPrimitiveUP`, for the fixed-function `XYZ`/`XYZRHW` path and
  for programmable shaders alike;
- **M2:** `IDirect3DVertexShader9`/`IDirect3DPixelShader9`
  (`CreateVertexShader`/`CreatePixelShader`/`SetVertexShader`/
  `SetPixelShader`/`GetFunction`) for shader model 1.x–3.0 bytecode. The
  guest never interprets the bytecode: it walks the token stream to find the
  terminator, hashes it, keeps a shadow copy for `GetFunction` and Reset
  replay, and ships the raw stream. Translation to WGSL happens host-side in
  `../d3d9-webgpu/d3d9_shader_pipeline.js`;
- **M2:** all six `Set*ShaderConstant{F,I,B}` entry points and their `Get*`
  counterparts, backed by a device-side shadow of the whole register file.
  The shadow suppresses redundant sets and narrows a changed set to the
  dirty sub-range, so re-uploading a 32-register bone palette where one bone
  moved costs one `float4` on the wire;
- **M2:** `SetSamplerState` as real independent sampler state, feeding the
  host's `GPUSampler` cache rather than being recorded and ignored;
- exclusive-fullscreen behaviour a real D3D9 runtime provides and no part of
  this stack otherwise does: the display mode change, and *maintaining* the
  foreground for the device window rather than claiming it once at
  `CreateDevice`. See `maintain_fullscreen_foreground` for why one claim is not
  enough and why `SetForegroundWindow` needs help to work at all;
- a 64-bit per-process session namespace carried by every D9WG batch and
  verified by `HELLO`; the host keeps each live process's device, resource,
  query, cursor and not-yet-presented frame context separate, so launchers and
  Futuremark capability helpers can overlap the benchmark without destroying
  its colliding numeric handles; and epoch-changing `Reset` with managed buffer/
  texture/cube-texture/vertex-declaration shadow reconstruction, reusing the
  exact transport/batching/handle-allocation strategy already validated by the
  D3D8 path;
- persistent implicit-back-buffer contents across WebGPU canvas acquisitions:
  each Present keeps an owned snapshot and the next frame restores it before
  replay. This is required by partial-redraw loops such as 3DMark06's loader,
  which issued 3342 consecutive frames without a colour Clear; `loadOp: load`
  alone only loaded the new canvas texture's undefined contents and displayed
  black;
- **M3:** `SetMaterial`/`SetLight`/`LightEnable` are now consumed by the host's
  fixed-function vertex stage rather than only recorded, and the whole
  `SetTextureStageState` surface drives a real multi-stage blending cascade.
  Both were caps promises `fill_caps()` had been making since M1
  (`MaxTextureBlendStages = 8`, `D3DVTXPCAPS_DIRECTIONALLIGHTS`,
  `MaxActiveLights = 8`, a large `TextureOpCaps` set) without keeping;
- **M3:** `IDirect3DCubeTexture9` (six faces per level, `LockRect`/`UnlockRect`
  per face) with stable `GetCubeMapSurface` child objects;
- **M3:** `IDirect3DStateBlock9` — `CreateStateBlock(ALL/PIXELSTATE/
  VERTEXSTATE)`, `Apply`, `Capture`, and `BeginStateBlock`/`EndStateBlock`.
  Recording keeps an explicit Set-call mask (including same-value and
  write-then-revert calls), and captured textures, shaders, declarations and
  buffers retain their COM references until the block is released;
- **M3:** `SetScissorRect`/`GetScissorRect`, gated by
  `D3DRS_SCISSORTESTENABLE`;
- **M3 (brought forward from M4):** render targets and depth surfaces —
  `CreateRenderTarget`, `CreateDepthStencilSurface`,
  `D3DUSAGE_RENDERTARGET`/`D3DUSAGE_DEPTHSTENCIL` textures,
  `SetRenderTarget` for four MRT slots, `SetDepthStencilSurface` and both
  `Get*`, plus `StretchRect` and `ColorFill`. A 2005-era D3D9 title renders
  most of its frame into textures, so without these it has no picture at all.
  `StretchRect` handles the back buffer as source or destination (deferred to
  Present, because that is the only point where the swap chain has a view), and
  scales or converts format through a blit pass when a plain copy cannot express
  it;
- **Protocol 1.3:** real asynchronous `IDirect3DQuery9` results. `EVENT` waits
  for `GPUQueue.onSubmittedWorkDone`, `OCCLUSION` resolves a WebGPU query set,
  and `TIMESTAMP`/`TIMESTAMPFREQ`/`TIMESTAMPDISJOINT` use timestamp queries with
  a monotonic-clock fallback when the optional WebGPU feature is absent;
- **M3:** `D9WG_DUMP_SHADERS=1` writes every shader's raw token stream to
  `d3d9_dump\` beside this DLL, named by content hash, for offline replay
  through `../tests/d3d9_shader_corpus_test.js`. See the comment above
  `dump_shader_bytecode` for why a hand-written translator needs real-game
  bytecode more than it needs more hand-written tests;
- **Protocol 1.3:** GPU-produced render targets and the implicit back buffer
  are copied to a mapped staging buffer, format-converted back to their D3D9
  layout, and returned in bounded chunks by `GetRenderTargetData` and
  `GetFrontBufferData`. Present also retains an owned GPU snapshot, because a
  canvas current texture is no longer valid when a later guest batch requests
  the front/back buffer. Known CPU shadows retain their fast path;
- `UpdateSurface`, 3D `IDirect3DVolumeTexture9` creation/locking/upload/sampling,
  `UpdateTexture` for 2D/cube/volume resources, stream-frequency instancing,
  six user clip planes, `MultiplyTransform`, and clip/raster status state;
- FP16/FP32 textures and render targets, MRT/depth pairing, sampling, blitting,
  resolve and readback. Four-sample MSAA is advertised with one quality level
  and resolved by the executor for swap-chain and texture render targets;
- **SM3:** the translator accepts SM1.x–SM3 bytecode, including VS3 vertex
  texture fetch. Pixel samplers s0–s15 and vertex samplers s0–s3 use disjoint
  bind-group ranges;
- `D9WG_CAPS_PROFILE=ffp` exposes the fixed-function-only profile and `sm2`
  keeps a shader-model-2 diagnostic profile; the default is SM3;
- **M5:** declaration-specific shader variants convert compact skinning inputs
  `UBYTE4`/`SHORT2`/`SHORT4`/`UDEC3`/`DEC3N`; sRGB reads and writes use
  compatible texture/target views; blend constants, separate-alpha blending,
  front/back stencil state, stencil reference, constant/slope depth bias, and
  per-draw scissor reset are all carried into WebGPU;
- **Warcraft III shadow fix:** projected texture division now preserves the
  sign of `q` in both fixed-function and shader-modifier paths. Clamping a
  negative `q` to positive epsilon made behind-projector UVs sample an opaque
  shadow-edge texel across whole terrain triangles, producing large black
  wedges. The shadow pass also receives its complete blend/stencil/depth state.
- **Warcraft III campaign state/cursor fix:** the guest state cache now starts
  from the real D3D9 render, texture-stage, and sampler defaults. This is
  correctness-critical because equal-value Set calls are suppressed: an
  all-zero cache discarded the first `ZENABLE=FALSE`/`ZWRITEENABLE=FALSE`
  transition and left scene depth active over UI, fog-of-war, and shadow
  overlays. Stage defaults now also preserve the first transition from stage
  1's `TEXCOORDINDEX=1` to War3's shared texcoord 0. Fixed-function BORDER
  addressing selects `D3DSAMP_BORDERCOLOR` in WGSL instead of stretching an
  opaque edge texel beyond projected shadow/fog masks. The GDI cursor capture
  fallback is now enabled by default because the browser cursor is hidden; set
  `D9WG_GDI_CURSOR=0` only for a title that intentionally draws its own cursor
  geometry.
- **Kart Rider texture-surface lifetime fix:** a surface from
  `IDirect3DTexture9::GetSurfaceLevel` is a sub-object of its texture, not a
  free-standing COM object. The texture now owns one surface per level
  (`D9TextureLevel::level_surface`), returns that same pointer from every
  `GetSurfaceLevel` for the level, forwards the surface's `AddRef`/`Release` to
  the texture's refcount, and frees the surface only in `texture_release`.
  Allocating a self-owning surface per call instead was a crash rather than a
  leak: Kart Rider uploads with `GetSurfaceLevel`, `LockRect`, `Release`,
  `UnlockRect`, dropping its surface reference while it still holds the texture
  and then using the pointer it kept — which is legal, and which the old code
  answered by freeing the object, so `UnlockRect` read a zeroed vtable and
  called through a null pointer during the loading screen.
  `../sample/d3d9_surface_lifetime_test.c` covers the sequence, plus the
  pointer identity that apps rely on to tell two level surfaces apart.
  `../d3d8proxy/d3d8_proxy.c`'s `texture_get_surface_level` still has the
  original per-call behaviour.
- **GTA San Andreas SDK-version fix:** `Direct3DCreate9` accepts both 31
  (`D3D9b_SDK_VERSION`, the DirectX 9.0b SDK) and 32 (`D3D_SDK_VERSION`, 9.0c),
  and masks off the `0x80000000` bit an app compiled with `D3D_DEBUG_INFO` sets
  to ask for the debug runtime. The real d3d9.dll does not police this. Checking
  the argument against the single value in mingw's header made the very first
  call return NULL for any 9.0b title, and San Andreas — which passes 31 — can
  only report that as a generic "unable to initialise DirectX". `../d3d8proxy`
  had already learned the identical lesson for 120 (8.0) vs 220 (8.1).
- **GTA San Andreas implicit-surface lifetime fix:** the back buffer and the
  auto depth-stencil are sub-objects of the *device*, exactly as a texture level
  surface is a sub-object of its texture. Each is now created once, cached on
  `D9Device` (`implicit_back_buffer`/`implicit_depth_stencil`), handed back by
  the same pointer from every `GetBackBuffer`/`GetDepthStencilSurface`, has its
  `AddRef`/`Release` forwarded to the device, and is freed only in device
  teardown. Allocating a self-owning surface per call meant the app's release
  destroyed it, and the trace showed the heap handing that freed block straight
  back out as the next index buffer while RenderWare still held the pointer it
  had cached during device setup — the same failure shape as the Kart Rider
  entry above, one level up. `surface_is_implicit()` identifies both from fields
  that already existed (`swap_chain`, `auto_depth_stencil`), each written by
  exactly one place. `../sample/d3d9_surface_lifetime_test.c`'s
  `verify_implicit_surfaces` covers the pointer identity and the
  survives-past-zero-references property; the test device needed
  `EnableAutoDepthStencil` before it had an auto depth-stencil to ask for.
- **GTA San Andreas adapter-mode-list fix:** `GetAdapterModeCount`/
  `EnumAdapterModes` reported a fixed three entries (640x480, 800x600,
  1024x768). They now enumerate the guest display driver through
  `EnumDisplaySettings`. San Andreas stores the chosen video mode in
  `gta_sa.set` as an **index into that list**, not as a resolution, so the
  length of the list is load-bearing: run the title once against a `d3d9.dll`
  offering a longer one (SwiftShader reports 18 modes per format), let it save
  an index valid there, and the next run against a three-entry list finds the
  index out of range and destroys its windows *during enumeration* -- before
  `CreateDevice`, before a single frame. The failure therefore depends on what
  ran previously, which is why it presented as "sometimes it dies at the legal
  screen, sometimes before the window appears". `EnumDisplaySettings` is the
  right source because it is also the constraint at the other end: exclusive
  fullscreen calls `ChangeDisplaySettings`, so every advertised mode has to be
  one the guest can actually switch into.
- **Presentation-interval fix:** `Present` honours
  `D3DPRESENT_INTERVAL_ONE`..`FOUR` (and `DEFAULT`, which D3D9 documents as
  equivalent to `ONE`) by sleeping until a deadline one interval after the
  previous `Present`'s, at the device's reported refresh rate. That wait is the
  *only* back-pressure a D3D9 title has on its own frame rate — the render loop
  is `while (running) { render(); Present(); }` and nothing else in it sleeps —
  so returning immediately let San Andreas' loading screen reach roughly 1000
  `Present`/s, starving every other thread in the guest of timeslices. A frame
  that finishes late by less than an interval keeps the cadence (the next
  frame waits less), so frames that straddle the interval still average the
  refresh rate; later than that, the cadence restarts. The clock is
  `QueryPerformanceCounter`: the first version measured from `GetTickCount`,
  which advances in whole 10 ms clock ticks on XP, so a 14 ms frame read as
  10 ms and slept 6 more -- a KartRider race whose frames took 14 ms ran at
  50 frames/s with a fifth of the game thread asleep, and no frame time could
  reach 60. `IMMEDIATE` is never throttled, and `D9WG_PRESENT_NO_THROTTLE=1`
  disables it outright.
- **GTA San Andreas legal-screen exit fix:** GTA's frontend loader exits when
  `CGame::InitialiseEssentialsAfterRW()` fails. The failing branch is the car
  environment-map pipeline's caps gate: it requires
  `D3DPSHADECAPS_SPECULARGOURAUDRGB`, but the proxy reported `ShadeCaps`
  `0x00084008`, missing that `0x00000200` bit. The backend already computes
  fixed-function per-vertex specular and adds it after the texture cascade, so
  `GetDeviceCaps` now reports the implemented capability (`0x00084208`). This
  is the one relevant difference exposed by the working SwiftShader caps, not
  a timing race; the GTA-specific 750 ms startup pacing workaround has been
  removed, and so has `D9WG_PRESENT_MIN_MS`, the environment override that
  existed to test the timing hypothesis. Keeping it would have left a switch
  whose only purpose was a question already answered -- and a misleading one,
  since pinning the frame time is exactly what a reader would reach for next
  time a title exits during startup. `D9WG_PRESENT_NO_THROTTLE=1` stays,
  because "run without the interval wait" is a question about this proxy's own
  behaviour rather than about one title's bug.
- **GTA San Andreas New Game crash fix:**
  `IDirect3DVertexDeclaration9::GetDeclaration` defines `pNumElements` as an
  output value, including when the caller supplies a declaration array. The
  proxy incorrectly treated its incoming, potentially uninitialised value as
  that array's capacity and returned `D3DERR_INVALIDCALL`. GTA's first world
  streaming pass makes exactly that valid call after displaying "Francis INTL
  Airport"; its RenderWare path then used the missing result and faulted at
  `0x007C91B1`. The proxy now always copies the declaration when an array is
  supplied and writes the resulting count, matching the D3D9 contract.
- **GTA San Andreas black-character fix (host side):** when lighting is enabled
  but a declaration has no `NORMAL`, D3D9 uses a zero normal for the light dot
  products; it does not bypass ambient and emissive lighting. The host used to
  skip the whole lighting path and pass GTA's black pre-lit `COLOR0` through,
  producing a black player silhouette. Zero-normal draws now retain global
  ambient times material ambient plus material emissive, and are counted by
  `drawsWithZeroNormalLighting`.
- **Kart Rider BCn mip-tail fix (host side):** `writeTexture` now rounds a
  block-compressed copy extent up to the 4x4 block grid. WebGPU measures such a
  copy in whole blocks and a mip level's physical extent is its logical size
  rounded up, so the 2x2 and 1x1 tail of a DXT chain has to be written as a full
  block. Passing the logical size failed validation as an *uncaptured device
  error* rather than an exception, so the only symptom was the smallest mips
  sampling as garbage behind several hundred console errors.
- **Kart Rider back-buffer size fix (host side):** the swap-chain colour
  attachment is sized from what the guest asked for at `CreateDevice`/`Reset`
  (`deviceState.backBufferWidth/Height`), not from `deviceState.surface`.
  `emit_present_and_flush` fills the `PRESENT` width/height from
  `GetClientRect` so the page can place the overlay canvas; a windowed game's
  client area is shorter than its back buffer (800x587 against 800x600 here).
  Reading it as the render size made every back-buffer pass look like it
  disagreed with the auto depth target, and the mismatch path then dropped
  depth — depth testing off for the whole game, caused by a window title bar.
- **Kart Rider half-pixel fix (host side):** every vertex stage now applies the
  D3D9 half-pixel offset — `pos.x += pos.w / viewport.x`,
  `pos.y -= pos.w / viewport.y` — in both the fixed-function generator
  (`HALF_PIXEL_OFFSET_BODY`) and the DXSO translator's `o_position` fixup, the
  latter reading a `viewport` uniform now present in every translated vertex
  shader. D3D9 samples a pixel at its integer corner; WebGPU, like D3D10 and
  everything after it, samples at the centre. A title that blits UI 1:1 has
  already subtracted that half pixel itself (the "Directly Mapping Texels to
  Pixels" adjustment), so replaying its geometry unchanged lands every sample
  exactly on a texel boundary and bilinear filtering returns the mean of two
  texels. 3D art does not care — none of it is pixel-aligned — but 12px CJK
  glyphs turn to mush, which is the split Kart Rider showed: a clean track and
  an unreadable shop. Same fix as wined3d's `posFixup` and DXVK's half-pixel
  offset.

- **Kart Rider viewport fix (host side):** a D3D9 viewport *clips*; WebGPU's
  `setViewport` only maps NDC to pixels. Nothing else cut a draw off at the
  viewport edge, so geometry an app restricted to a small panel with
  `SetViewport` alone was drawn across the whole target. Every draw now sets a
  clip rect (`intersectRects`) — the viewport intersected with
  `D3DRS_SCISSORTESTENABLE`'s rect, since D3D9 applies both — clamped into the
  attachment. The same call also stopped discarding the viewport's `MinZ`/`MaxZ`,
  which `D9WGSetViewport` had carried since M1 while `recordDraw` hardcoded
  `0, 1`: an app compositing a 3D object into a 2D panel routinely restricts the
  depth range so the object cannot collide in depth with the interface around
  it, and ignoring that puts the object at its natural depth instead.

- **3DMark06 off-screen viewport fix (guest side):** `SetViewport` validates
  against the dimensions of the currently bound RT0, not always against the
  back buffer. `SetRenderTarget(0)` resets the viewport to the complete new
  target as D3D9 requires; a 2048x2048 shadow/precomputation target on an
  800x600 device is therefore valid. Rejecting that automatic viewport made
  `SetRenderTarget` return `D3DERR_INVALIDCALL`, which 3DMark06 turned into a
  C++ exception during Load Test before the queued target bind could flush.

- **Kart Rider shop-panel fix (host side):** XYZRHW ("pre-transformed")
  coordinates are absolute render-target pixels, not viewport-relative ones, so
  the fixed-function screen path subtracts the viewport origin before
  normalising — `setViewport` puts that origin back when it maps NDC into the
  viewport rect, and doing both is what makes the round trip an identity. The
  two cancelled out only for a viewport at 0,0, which is every full-screen UI
  pass, so this stayed invisible until a title drew pre-transformed geometry
  through a small offset viewport: Kart Rider renders each shop item preview
  through a 110x109 viewport at x=368..636, and its pre-transformed geometry
  landed several viewport-widths outside the box and was clipped away. The
  panels whose contents were entirely pre-transformed came out empty, while the
  one item built from world-space geometry rendered perfectly. That split is
  what identified it, and it was only visible by counting pre-transformed draws
  per viewport for one frame — a temporary probe, removed before release
  because it walked every draw op and built strings on every present. If a
  comparable symptom returns (geometry missing or mis-scaled only inside a
  sub-viewport), re-adding that count in `finishFrame` is the shortest path.
  Same correction as wined3d's transformed-position projection matrix, which
  carries a `-2x/w` term for this reason.

**Guest refusals now reach the browser console.** `D9WG_OP_GUEST_LOG` (opcode
11) carries a short ASCII string from the guest DLL to the executor, which
prints it as `[d3d9-guest] …`. Before the 1.3 response tail, command traffic
only ran guest-to-host, so a call the guest turned down was invisible: the console
sees a clean stream of valid commands, and the guest's own trace file lives
inside a VM whose filesystem the page cannot reach — the exact reason several
"the picture is wrong" investigations here ran on guesswork. `host_log()` is
compiled into the *ordinary* DLL, not just the diagnostic one (needing a special
build to learn that a call was refused is most of the problem), and deduplicates
by exact text so a failure inside a per-frame path costs one message rather than
one per frame, capped at 48 distinct messages. It is deliberately not a general
logging channel: only refusals and failures are sent — every `UNSUPPORTED()`
site, `SetViewport` rejection, `CreateOffscreenPlainSurface`'s format
restriction, and the `CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer`
failure paths. Protocol 1.3 deliberately requires the matching executor and
bridge; older page/DLL pairs are not a compatibility target.

The remaining deliberately unsupported entry points trace `STUB <Method>` and
are mirrored once to the browser console through `D9WG_OP_GUEST_LOG`:
higher-order patches (`DrawRectPatch`/`DrawTriPatch`/`DeletePatch`, N-patches
and adaptive tessellation), and `SwapChain::GetFrontBufferData` on an
*additional* chain. Their related caps/usages are not advertised.

`ProcessVertices`, additional swap chains, palettized textures and
`Surface::GetDC`/`ReleaseDC` were on that list and are now implemented; three
of the four live entirely on this side of the wire, because the thing each one
returns is guest memory rather than a picture:

- **`ProcessVertices`** is a real software vertex pipeline over the state the
  guest already shadows for SetTransform/SetViewport/SetMaterial/SetLight. It
  has to be here: the call's whole purpose is that the result lands in memory
  the app can Lock and read, and this stack's host is asynchronous, so no
  synchronous GPU round trip could answer it. A bound vertex shader is refused
  rather than served by the fixed-function pipeline, and
  `D3DRS_SPECULARENABLE`'s highlight is copied rather than computed --
  `sample/d3d9_process_vertices_test.c` checks the numbers rather than the
  pixels.
- **`Surface::GetDC`/`ReleaseDC`** copy the surface into a DIB section for the
  DC's lifetime and copy back on release, through the ordinary LockRect upload.
  A DIB is the one bitmap GDI will both draw into and hand back a pointer for,
  which is the same reason wined3d uses one.
- **Additional swap chains** are the exception that needs the host: the chain
  targets a different HWND, so it gets its own canvas through the executor's
  `createSwapChainCanvas` hook. Its back buffer is deliberately an ordinary
  render-target texture rather than a second "handle zero" special case, so
  draws, `StretchRect` and readback all treat it as a texture and the chain is
  special only in the step that puts its image on screen. An embedder that
  supplies no hook gets the chain's frames counted and reported, not silently
  dropped.

**The diagnostic DLL now traces the paths a title can quietly give up in.**
Chasing GTA San Andreas's silent exit turned up three blind spots, each able to
swallow a whole call. The whole `IDirect3D9` enumeration and caps family was
completely dark and now logs arguments and results, including three `CAPS` lines
carrying the fields titles actually gate on. `create_shader` rejected bytecode
*before* emitting anything, so a refused `CreateVertexShader` left no `OK` and no
`FAIL` — only a gap; both paths now trace, with the version token. And
`PROCESS_DETACH` dumps `LAST` (the last marked method entered and left,
previously printed only from the exception handler) alongside `WINDOW` (whether
the device window outlived the process).

The third was the ~100 device methods with no trace of their own —
`SetRenderState`, `SetTextureStageState`, `SetTransform` and the rest. One
`TRACE` at `reserve_command_locked`, the single chokepoint every host-bound
command passes through, covers all of them in one line and is what identified
where San Andreas stopped. It is left commented in that function rather than
compiled in, because it costs one `WriteFile` per command: invaluable up to the
first frame, unusable once a title is drawing. `Device.TestCooperativeLevel` is
untraced for the same reason. Both are two-line changes when a startup
investigation needs them.

For an exit D3D9 cannot explain at all, the diagnostic build installs
thread-local `WH_CALLWNDPROC` and `WH_GETMESSAGE` hooks and logs the window
messages that end a program, as `MSG sent` / `MSG posted`. `WM_QUIT` only ever
travels through the message pump, so `WH_GETMESSAGE` is the one place it is
visible. The ordinary DLL installs nothing.

**A trace where nothing fails is not a trace where nothing is wrong.** That
combination above produced, for San Andreas, a complete RenderWare device setup
— device, six buffers, three vertex declarations, six `ps_1_1` pixel shaders,
`DXT1` and `D24S8` format checks — in which *every single D3D9 call succeeded*:
no `FAIL`, no `STUB`, no `EXCEPTION`, followed by an orderly teardown. The
conclusion drawn from that, that the exit was not D3D9's doing and the next
place to look was `dinput8`/`dsound`, was wrong: the same build of the game runs
under SwiftShader on the same guest. Returning `D3D_OK` is not the same as
returning the *right value*, and a trace of return codes cannot tell the two
apart. The adapter identifier, the mode list and all ~67 `D3DCAPS9` fields this
proxy reports are fabricated by `fill_caps()`, and an app that reads one and
quietly takes a different path leaves no mark on the trace at all.

That claim also has to be *earned*, and for two rounds it was not. Of the 283
error returns in `d3d9_proxy.c`, **249 were invisible**: a bare
`return D3DERR_INVALIDCALL` emits no `FAIL` line, is not covered by the
per-frame `rejected=` counter (that only wraps the four draw entry points), and
`UNSUPPORTED()` only covers the deliberate stubs. A validation refusal inside
`Clear`, `LockRect`, `SetStreamSource` or `CreateTexture` was simply silence --
indistinguishable, in the log, from the call never happening. The diagnostic
build now wraps every one of them in `TRACE_REFUSE()`, which logs
`REFUSE <function>:<line> -> <hr>`; it compiles to the identity in the shipping
DLL, which carries no `REFUSE` string at all. Read "nothing in the trace failed"
as evidence only for calls whose failure path is instrumented.

**A refusal that cannot be explained cannot be fixed.** `TRACE_REFUSE()` gives
every rejection a line, but a line is not the same as an answer.
`device_create_cube_texture` had four reasons to refuse -- edge size, format,
usage, pool -- and logged none of its arguments, so a 3DMark06 crash produced a
six-megabyte trace whose decisive event read, in full,
`REFUSE device_create_cube_texture:9013 -> 8876086C`. The refusal was visible
and still unattributable. Entry points that can refuse for several reasons now
log their parameters *before* anything can reject them, the way
`device_create_texture` already did.

### Saying yes and then failing

`CheckDeviceFormat` and the `Create*` entry points encoded the same rules twice,
and the copies had drifted. The query blessed any cube map whose format was
sampleable; `device_create_cube_texture` refused every cube map carrying a usage
flag. 3DMark06 asked whether it could filter a `G16R16` cube map, was told yes,
and got `D3DERR_INVALIDCALL` from the create.

Real D3D9 does not produce that combination, so nothing defends against it.
3DMark threw a C++ exception out of the failed create (`E06D7363`, magic
`0x19930520`) and died at `004A1406` dereferencing a COM smart pointer that
still held uninitialised stack data -- `mov ecx,[eax]` with `eax=3F800000`,
which is `1.0f`, the leftover of a matrix. A capability lie does not surface as
a wrong image; it surfaces as a crash in someone else's error handling, several
frames from anything we wrote.

The asymmetry is worth stating plainly, because it is not symmetric between the
two callers. For the **query**, answering "no" is always safe: an app told no
picks another format, or does without. For the **create**, refusing is the
dangerous direction -- that is the direction that produced this crash. So the
shared `texture_create_supported()` predicate answers only the question a create
asks (*can this resource exist?*), and `CheckDeviceFormat` layers the stricter
"do we implement this behaviour?" test on top. The query is therefore never
weaker than the create, and the create never refuses something D3D9 would build.

Usage bits that describe what a resource is *for* rather than whether it can
exist -- `D3DUSAGE_AUTOGENMIPMAP`, `_DMAP`, `_NPATCHES`, `_SOFTWAREPROCESSING`
-- are on the query side of that line. `AUTOGENMIPMAP` in particular is handled
the way D3D9 handles it: it is stripped, levels are clamped to one (nothing
would fill a chain nobody generates, and sampling unwritten mips is worse than
having none), and the create returns `D3DOK_NOAUTOGEN` -- a success code -- with
a usable texture.

`glbridge/sample/d3d9_cube_texture_test.c` asserts the invariant directly: for
every format the query blesses, the matching create must succeed.

The invariant has a second half that is easy to miss, and missing it cost a
round trip. Routing `CheckDeviceFormat` through the creation predicate made it
answer *yes* for `D3DRTYPE_SURFACE` with no usage -- correct as far as it goes,
because `CreateOffscreenPlainSurface` really does succeed. But those surfaces
are CPU-only here: they own their pixels and have no GPU resource, so
`StretchRect` cannot use one, and D3D9 requires both its operands to be
`D3DPOOL_DEFAULT` GPU surfaces. 3DMark06 asked, was told yes, created one,
blitted with it, and put up an `IDirect3DDevice9::StretchRect failed` box.

So "a create the query blessed must succeed" is not sufficient. **The
operations D3D9 defines on the thing that was created have to work too**, and
where they do not, the query is the last place to say so before the app
commits. That surface type answers no again, deliberately and with the reason
written down, until it has GPU backing.

### Hardware shadow maps

A D3D9 depth *texture* is not only an attachment. The standard shadow map is
`CreateTexture(..., D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8/D32, D3DPOOL_DEFAULT)`,
rendered into through `GetSurfaceLevel` + `SetDepthStencilSurface`, then handed
to `SetTexture` so the lighting pass can read it. There is no syntax for the
read: `tex2D`/`tex2Dproj` on a sampler whose texture happens to be a depth
format silently becomes a hardware depth comparison returning filtered
visibility.

The executor modelled a depth resource as attachment-only -- no
`TEXTURE_BINDING` usage and `view: null` -- on the reasoning that nothing can
read a depth surface's pixels back, so "a depth buffer exists" was the whole of
its observable behaviour. 3DMark06 creates a 2048x2048 `D3DFMT_D32` shadow map
and samples it, and the consequence was not a wrong image. The `null` reached
`createBindGroup()` as a binding resource and threw a `TypeError`.

WGSL makes the two cases different types -- `texture_depth_2d` sampled through a
`sampler_comparison`, versus `texture_2d<f32>` through a `sampler` -- and
neither substitutes for the other, so the same bytecode has to translate
differently depending on what is bound. That decision lives in the pixel-shader
variant key alongside alpha test and clip planes, and the translator reports
which samplers it actually emitted as depth (`reflection.samplers[].depth`).
The bind group layout is built from that report and nothing else: a
`texture_depth_2d` declaration paired with a float layout entry does not draw
badly, it fails pipeline creation. `tex2Dproj` supplies the comparison
reference as `z/w`, which is why `coordinateFor()` carries `z` out alongside
the uv for a depth sampler instead of discarding it with the other unread
components.

Three cases cannot be shadow maps and take the 1x1 fallback with a warning
rather than an invalid binding: the fixed-function cascade and vertex texture
fetch (neither has a comparison reference to offer), and a depth surface still
bound as the current pass's attachment (WebGPU cannot read and write one
texture in a single pass, and enforces it by failing the whole submit). The
depth fallback is cleared to the far plane through an empty render pass --
depth formats cannot be written with `writeTexture`, and a zero-initialised one
would read as fully shadowed rather than fully lit.

### Cube render targets (protocol 1.4)

`D9WG_OP_SET_RENDER_TARGET` named a resource and a mip level. A cube map is
bound one face at a time -- `SetRenderTarget` takes what
`GetCubeMapSurface(face, level)` returned -- so with no face on the wire every
face of a dynamic environment map resolved to array layer 0 and five of the six
renders were painted over the first. The capability was previously reported as
absent for exactly that reason; 1.4 carries the face and it is now real.

`StretchRect` and `ColorFill` got the same field in the same version, because a
cube render target that cannot be blitted or cleared per face is a feature that
works until something touches it. `ColorFill` also stopped building its
attachment view by hand: it left the array layers unbounded, which on a cube map
is a six-layer view where a pass wants one.

Minor versions are now accepted as a **range** rather than an exact match. The
guest DLL is copied into the VM image and this file reloads with the page, so
requiring them to agree exactly made every protocol addition a window where
nothing rendered and the only symptom was `unsupported D9WG version`. Every
field added since the minimum is length-gated at its decode site, so an older
payload is simply shorter and each missing field reads as the default that
version meant. A change that reinterprets *existing* bytes still needs the major
version, which is still exact.

### Depth surfaces larger than the render target

D3D9 requires the depth surface to be **at least** the size of the colour
target. WebGPU requires it to be **exactly** that size. Render-to-texture leans
on the D3D9 rule constantly: one full-screen depth surface, reused by
half-resolution HDR downsamples, shadow projections and reflections.

Dropping depth for those passes -- which is what a size mismatch used to do --
is not a small approximation. Nothing occludes anything any more, so every
alpha-blended draw that should have been hidden behind geometry paints over it
instead. The frame washes out and solid objects turn translucent, which reads as
a shading bug rather than a missing depth buffer. An oversized depth surface now
gets a size-matched substitute, so the depth *test* still happens. What the
substitute cannot carry is depth written into the larger surface by an earlier
pass; an app doing this almost always clears depth on entry, which is why this
trades a fault that ruins the frame for one that is usually invisible. A depth
surface *smaller* than the target is still refused: there is nothing to
substitute that would be more correct than admitting the pass cannot depth-test.

### Formats: palettes, packed pairs, and two kinds of depth read

Everything `CheckDeviceFormat` used to say no to and an app quietly did without:

- **D3DFMT_P8 / A8P8.** A D3D9 palette is device state consulted when a texel is
  sampled, not something baked into the texture -- the same surface takes on
  different colours as the app switches palettes with no upload in between.
  WebGPU has no palettized format, so the expansion happens on the CPU, which
  means the *indices* have to be kept and replayed whenever the table or the
  selection changes. `SetPaletteEntries` and `SetCurrentTexturePalette` stopped
  being stubs to make that possible.
- **D3DFMT_UYVY / YUY2 / R8G8_B8G8 / G8R8_G8B8.** Two texels per 32-bit block
  sharing chroma, expanded through the same block machinery BCn uses -- a 2x1
  block rather than 4x4, which also enforces D3D9's even-width requirement. The
  YUV pair is BT.601 studio swing, because these formats exist for video frames
  and a decoder writes 16..235 luma; treating it as full range washes blacks out
  to dark grey, which is exactly the kind of error that looks intentional.
- **D3DFMT_Q16W16V16U16**, joining the other high-precision formats on
  `rgba16float`.
- **DF16 / DF24 / INTZ.** D3D9 has two ways to read a depth texture and they are
  not interchangeable. A D16/D24X8/D32 texture bound to a sampler is a hardware
  shadow map: the driver compares and returns filtered visibility. These FOURCC
  formats return the stored depth itself, which is the entire reason an app
  chooses one. Both are `texture_depth_2d` in WGSL; only the first takes a
  `sampler_comparison`. Sampling either as though it were the other produces an
  image that looks deliberate and is wrong, so the reflection reports which mode
  a stage compiled to and the bind group layout is built from that.

`naga` earned its place here: `textureSampleLevel` on a depth texture takes a
*concrete integer* mip level, unlike the `f32` every colour-texture overload
takes. The `f32` form was rejected at validation instead of failing
driver-specifically inside the browser.

### Automatic mipmap generation

`D3DUSAGE_AUTOGENMIPMAP` hands the driver everything below level 0 and asks it
to keep the chain current. It used to be stripped, with `D3DOK_NOAUTOGEN`
reported back -- honest, and it left 3DMark06 building its own downsample chain
and one `CheckDeviceFormat said no` line on every run.

The shape D3D9 describes is unusual enough to be worth stating: such a texture
reports `GetLevelCount() == 1` and hands out no surface below the top, because
the sublevels are not the app's. So the guest keeps one visible level and the
host allocates the full chain behind it -- one visible level is the documented
contract, not a simplification.

The trigger lives on the host, because the host is the side that can see when
the top level actually changed: an upload landed on level 0, or a render pass
had level 0 as its attachment. Regeneration is queued at the point the texture
is next *sampled*, which is what orders it correctly -- after whatever wrote
level 0, before the draw that reads below it -- and skipped entirely when
nothing dirtied the chain, so a static texture is not rebuilt every frame.
`GenerateMipSubLevels` arrives as its own opcode for the explicit case. Each
level is a blit from the one above through the existing blit pipeline, which is
the linear box downsample `D3DTEXF_LINEAR` describes.

Volume textures are the exception, and only because D3D9 does not support
automatic generation for those either; they still report `D3DOK_NOAUTOGEN`.

### Offscreen plain surfaces are GPU surfaces

The other half of the capability lesson, learned the hard way a second time.
`CreateOffscreenPlainSurface` built a CPU-only surface: it owned its pixels and
had no GPU resource. That is right for `D3DPOOL_SYSTEMMEM`, where the surface
exists to be locked, and wrong for `D3DPOOL_DEFAULT`, where D3D9 lets it be a
`StretchRect` operand -- both operands have to be `DEFAULT`, and this call is
how apps get the second one.

A `DEFAULT`-pool offscreen surface now carries the same texture a render target
does. `ColorFill` moved with it: it used to require `D3DUSAGE_RENDERTARGET`,
which such a surface does not carry even though ColorFill is defined on it, so
the test is now whether the WebGPU texture can be an attachment at all -- which
is the question that was actually being asked.

### A warning that fires when nothing is wrong is not a warning

The oversized-depth substitution above reported itself on every pass it touched.
That was noise: the substitution only loses something if the pass depth-tests
against contents an earlier pass wrote into the larger surface, and a pass that
clears depth on entry -- which is what render-to-texture does -- has none to
lose. It now tracks whether the stand-in was cleared this frame and reports only
the case that actually changes the image, where geometry that should have been
occluded is not.

### Derivatives inside data-dependent branches

D3D9 hardware runs both sides of a branch, so `dsx`/`dsy` inside an `if` is well
defined there. WGSL forbids the call outright, and the whole module fails to
compile -- which is not a subtle difference: 3DMark06's airship rendered as a
solid black silhouette because its pixel shader would not build at all, and the
only evidence was one `'dpdy' must only be called from uniform control flow`
line in the console.

The derivative almost always reads a register computed *before* the branch, so
the call is hoisted to just above it: legal, and exactly the value the shader
asked for. Whether that is true is tracked rather than assumed -- each open
non-uniform region records the registers written inside it, and an operand the
branch overwrote has no value above the branch to differentiate, so it degrades
to zero with a note instead.

The same machinery fixed a larger, older approximation. Sampling inside a
data-dependent branch used to drop to mip level 0, which is correct-ish and
visibly aliased wherever a surface is minified -- most of a scene. Hoisting the
coordinate and both derivatives out and sampling with `textureSampleGrad`
(explicit gradients, so WGSL permits it under any control flow) is not a
degradation at all: it is the mip level the shader would have selected on D3D9
hardware. Level 0 remains the fallback for coordinates the branch itself
computes, and `naga` is what proves the gradient form really is legal there.

Depth samplers are excluded from that path: WGSL has no
`textureSampleCompareGrad`, so there is no gradient form to recover *to*.

### One bad command is not a bad batch

`executeBatch()` decoded its command loop without a per-command guard, so a
throw from any handler unwound to `submit()`, which logged
`batch failed` and discarded everything still queued behind it -- render target
bindings, resource creations, vertex declarations. The guest was never told.
Frames after that drew against state the host had never received and reported
it as *"a render target slot names a resource the host does not know"* and
*"no vertex declaration"*: symptoms several batches removed from the one
command that actually failed, and pointing at subsystems that were working.

Command failures are now contained to their own command and counted in
`stats.commandsFailed`. The exception is framing: a payload that contradicts
the batch layout means the producer and the consumer disagree about the bytes,
so nothing else in the batch can be trusted either. Those throw
`D9WGStreamError` and still fail the whole batch. The rule that keeps the two
apart is mechanical -- every site that increments `stats.malformedBatches` is by
definition saying the batch is malformed rather than that one command failed.

### Fixed-function vertex blending

`D3DRS_VERTEXBLEND` is implemented, indexed and not. `vertexBlendPlan()` in
`d3d9_executor.js` resolves the render state against what the declaration
carries, and the generated vertex stage builds `sum(w_i * (v * WORLDMATRIX(i)))`
rather than posing every vertex by `D3DTS_WORLD`. Three details are worth
keeping in mind, because each of them is a way to get this subtly wrong:

- **The weights are one short on purpose.** `D3DVBF_1WEIGHTS` names one weight
  and blends *two* matrices; D3D9 defines the last matrix's weight as
  `1 - sum(the rest)`. Reading a fourth weight out of the attribute instead
  would leave the weights failing to sum to 1 wherever the exporter rounded.
- **`BLENDINDICES` is the one fixed-function attribute that is not `f32`.**
  `D3DDECLTYPE_UBYTE4` maps to WebGPU's `uint8x4`, and WebGPU rejects a pipeline
  whose WGSL declares that location as a float. `declTypeInputScalar()` picks
  the base type from the declaration; the WGSL validation test compiles all
  three (`u32`/`i32`/`f32`) through naga so a wrong one cannot reach a browser.
- **A blended draw cannot fold the world matrix into the chain.** Which world
  matrix applies is a per-vertex question, so the uniform block carries
  `view_projection` (and `view_matrix`) instead of `world_view_projection` (and
  `world_view`). Leaving the folded matrix in place would re-apply world matrix
  0 on top of the blend, which looks almost right and is not.

The FVF spelling works too: `fvf_to_declaration()` expands `D3DFVF_XYZB1`
through `XYZB5` (and `D3DFVF_XYZW`), honouring `D3DFVF_LASTBETA_UBYTE4` /
`D3DFVF_LASTBETA_D3DCOLOR` — and treating `XYZB5` as implicitly carrying
indices, since five float weights have no `D3DDECLTYPE` to be declared as.
Those codes used to be refused outright, which made `SetFVF` the one entry
point from which vertex blending could not be reached at all.

`fill_caps()` reports `MaxVertexBlendMatrices = 4` and
`MaxVertexBlendMatrixIndex = 255` accordingly. It deliberately does **not** set
`D3DVTXPCAPS_TWEENING`: tweening interpolates two vertex streams by
`D3DRS_TWEENFACTOR` and merely shares the `D3DVERTEXBLENDFLAGS` enum with
blending — nothing implements it, and `D3DVBF_TWEENING` falls back to world
matrix 0 with a warning.

Indexed blending uploads a palette sized from the highest `D3DTS_WORLDMATRIX(n)`
the guest has set, rounded up through power-of-two buckets: sizing it exactly
would put the count in the shader cache key and mint a new module and pipeline
every time an engine adds a bone. State blocks capture world matrices 0..3 only;
an app recording a block around an *indexed* skinned pass gets the palette back
as that pass left it, which is a stated limit rather than an unnoticed one.

`drawsWithUnappliedVertexBlend` survives, but it now means something different
and much weaker: the declaration carried `BLENDWEIGHT`/`BLENDINDICES` while
`D3DRS_VERTEXBLEND` was `DISABLE`, so nothing blended. That is *correct* — D3D9
ignores the data too — and it is common, because engines share one declaration
between their skinned and unskinned passes. It stays counted rather than warned
about because "the character is stuck in bind pose" and "the character is
missing" look alike from the outside, and a nonzero count next to a zero
`blendedDraws` says which. The translated-shader skinning path (M5 `UBYTE4`/
`SHORT2`/`SHORT4`/`UDEC3`/`DEC3N` inputs) is a separate path and is unaffected.

The protocol version is 1.3, and the executor accepts exactly that version.
Older DLL batches and newer unmatched batches both fail before their first
command executes: the command layouts and response tail change together, so
mixing versions must never interpret resource data at the wrong offsets.

Set `D9WG_CAPS_PROFILE=ffp` in the guest environment to make `GetDeviceCaps`
report the supported M4.5 fixed-function profile (`VertexShaderVersion`/
`PixelShaderVersion` = `(0,0)`); `D9WG_CAPS_PROFILE=sm2` selects the SM2
diagnostic profile, while SM3 is the default. The legacy
`D9WG_SHADER_MODEL=0` spelling remains accepted.

Build an XP-compatible DLL without a C runtime dependency:

```sh
./glbridge/d3d9proxy/build.sh /private/tmp/d3d9.dll
```

The build enforces `-Wall -Wextra -Werror` and asserts the output imports
only `KERNEL32.dll`/`USER32.dll`/`GDI32.dll` — no MSVCRT/UCRT dependency,
matching the D3D8 path's XP-compatibility requirement.

`Direct3DCreate9` is exported alongside `DebugSetMute` and the `D3DPERF_*`
PIX-instrumentation hooks (`BeginEvent`/`EndEvent`/`SetMarker`/`SetRegion`/
`QueryRepeatFrame`/`SetOptions`/`GetStatus`) as harmless no-ops. The D3D8
path hit exactly this class of problem with Warcraft III's
`ValidateVertexShader`/`ValidatePixelShader` static imports — a title that
imports a symbol this DLL does not export fails to *load*, so
`Direct3DCreate9` is never even reached, and the failure surfaces as a
generic "unable to initialize DirectX."

Install the resulting DLL beside the target executable. Use a separate game
deployment profile from `d3d8.dll` and the custom `opengl32.dll` proxy: all
three frontends share one mapped DMA arena and a game directory may load
only one of them (see plan section 4.6/20).

Host-side executor: `../d3d9-webgpu/d3d9_executor.js`, which requires
`../d3d9-webgpu/d3d9_shader_pipeline.js` to be loaded first (it resolves the
translator at load time, not lazily — see the `<script>` order in
`game.html`).

Tests:

- `../tests/d3d9_shader_pipeline_test.js` — bytecode → WGSL translation,
  driven by hand-assembled D3D9 tokens (`node`, no browser);
- `../tests/d3d9_shader_wgsl_validation_test.js` — runs the generated WGSL
  through `naga` when one is installed (`cargo install naga-cli`, or point
  `D9_NAGA` at the binary), so a syntax/type error surfaces in a second
  rather than as a black screen inside v86;
- `../tests/d3d9_webgpu_executor_test.js` — real D9WG batches against a fake
  WebGPU device that enforces bind-group/`writeBuffer` validation rules;
- `../tests/d3d9_webgpu_browser_test.html` — the same paths against real
  WebGPU in a browser, including a translated `vs_2_0`/`ps_2_0` pair with an
  M5 compact vertex input, a lit two-stage cube-sampling draw rendered into a
  texture, partial Clear/ColorFill, and an sRGB back-buffer write.
  Run it against real WebGPU rather than trusting `naga` alone: `naga` and Tint
  disagree on real cases, and the M3 round found one only Tint catches (a bind
  group layout must declare `viewDimension`, because `naga` validates a module
  in isolation and never sees the module/layout pairing);
- `../tests/d3d9_shader_corpus_test.js` — replays a directory of `.d9sh` files
  dumped by `D9WG_DUMP_SHADERS=1` through the translator, grouping failures by
  message. Opt-in via `D9_SHADER_CORPUS`; it is a measurement, not a gate,
  unless `D9_CORPUS_STRICT=1`;
- `../sample/d3d9_shader_test.c` — the real DLL in the real XP guest
  (`./build_smoke_test.sh`);
