# OpenGL 1.1-2.1 on WebGPU

The host half of the OpenGL path. The guest DLL
(`../openglproxy/opengl32_proxy.c`) retains the same wire ABI: it
serialises the same 217 opcodes into the same PCI DMA arena. What changed is
everything after the opcode.

```text
game.exe
  -> opengl32.dll            (../openglproxy, unchanged)
  -> v86gl.sys / PCI DMA     (../v86gl_driver, unchanged)
  -> v86_network_bridge.js   (routing only)
  -> gl_executor.js          GL state machine, resources, pipelines
       gl_shader_translator.js   GLSL 1.10/1.20 -> WGSL
       gl_fixed_function.js      the fixed pipeline, generated as WGSL
       gl_arb_program.js         ARB assembly programs -> WGSL
       gl_state_layout.js        the GL state uniform block, shared by all three
       gl_wire.js, gl_constants.js  generated from the guest
  -> ../webgpu_host.js       one GPUDevice and one canvas, shared with D3D9
  -> WebGPU / WGSL           #d3d_webgpu_canvas
```

Design notes live in
`../../docs/opengl-webgpu-implementation-plan.zh-CN.md`; this file is the
record of what the implementation actually does, and
[COVERAGE.md](COVERAGE.md) is the per-entry-point table.

## Three decisions worth knowing before reading the code

**The authoritative GL state machine is here, not in the guest.** That is what
lets `glGetError`, `glGetIntegerv`, `glGetString`, `glGetUniformLocation`,
`glGetShaderiv` and `glCheckFramebufferStatus` be answered inside the port
write that delivered them -- the guest is blocked in `DeviceIoControl` and
reads the answer out of the record it submitted. Only `glReadPixels` and
occlusion-query results need a GPU round trip, and those are the only two
places the guest spins.

Those two are worth stating precisely, because getting them wrong looks like
success. Their answer cannot exist when the port write returns: it comes from
a buffer mapping, and `mapAsync` resolves in a later browser turn. So the host
writes `PENDING` into the record, completes it from the mapping callback
(pixels first, status word last), and the guest waits -- `wait_for_host_status`
for `glReadPixels`, a re-poll loop for `GL_QUERY_RESULT`. A guest that checks
once always reads `PENDING`; a host that never resolves leaves a legal
`while (!available)` loop spinning forever. `tests/gl_executor_test.js` asserts
both halves, including that the status still reads `PENDING` the instant the
batch returns.

**Clip space is flipped once, in the vertex shader** (`clip.y = -clip.y`).
Framebuffer row 0 is then GL's bottom row, so `glViewport`, `glScissor`,
`glReadPixels`, `glCopyTexImage2D` and render-to-texture orientation all need
no conversion at all. The two costs are reversed winding -- which lives in
exactly one function, `gpuFrontFace()` -- and a flip in the present blit.

**A draw resolves state into a signature and looks the pipeline up.** Any
state that changes the picture must reach the signature. A field that does not
is a bug whose symptom is "I changed the state and nothing happened", which is
the hardest class of bug in this directory to find;
`tests/gl_fixed_function_wgsl_test.js` walks every signature field to keep it
honest.

## Draw-path caching

Indexed client-array packing removes unused prefixes; sparse index ranges are
compacted by referenced vertex, with the draw indices remapped consistently.
VBO attributes that WebGPU can read remain native. Incompatible attributes
(including Cube 2's signed byte3 normals) are converted separately and cached
by buffer object, type, size, stride, offset, normalization and shader width.
BufferData/SubData invalidates the buffer's conversions conservatively;
deletion, replay reset, context teardown and device loss release them. Resident
conversion storage is capped at 64 MiB / 256 entries; saturation uses the
compact per-draw fallback rather than evicting a not-yet-encoded draw's data.

Unsigned-short/int EBOs for triangle, line and point lists bind directly.
Strips and polygon edge/point expansion retain the CPU index path. Pending
index reads participate in the same copy-on-write protection as vertex reads.

Uniform layouts are cached; inactive bindings allocate nothing. Active blocks
are serialized and compared byte-for-byte, uploading only changed snapshots
within a submission. Bind groups reuse identical pipeline/resource/slice keys.
Submission clears these caches before rings can rewind; changed uniforms always
get a new slice. This preserves multipass and query correctness rather than
mutating data referenced by earlier draws. State serialization is still CPU
work; these changes do not move v86 or rendering off the main thread.

Regression coverage: `gl_executor_test.js`, `gl_multipass_browser_runner.js`
(real GPU pixels, sparse/native indices, cached normals, ring rollover and
segmented queries), and `gl_diagnostic_buffer_test.js` for the guest writer.

## Known deviations

WebGPU cannot express some of OpenGL exactly. Every case is listed here with
what it does instead and when the difference is visible. Numbers are permanent:
if a deviation is later eliminated the entry stays, marked as such.

| Code | Feature | What happens instead | When it shows |
| --- | --- | --- | --- |
| D-01 | `glLogicOp` | **Resolved for fixed-function and GLSL draws.** The executor snapshots the destination attachment and applies all 16 8-bit channel Boolean operations in WGSL. Pixel-rectangle and ARB-assembly draws remain outside this rewrite path | Logic-op draws cost one attachment copy; logic op combined with those two exceptional paths is still refused or approximated |
| D-02 | Separate two-sided stencil masks | WebGPU has one `stencilReadMask` and one `stencilWriteMask` for both faces; the front face's are used and the divergence is reported once | Only algorithms that set different front and back masks -- a rare stencil-shadow variant |
| D-03 | `GL_CLAMP`, `GL_CLAMP_TO_BORDER` | WebGPU has no border addressing; the sampler clamps to edge | A texture sampled outside [0,1] with a border colour shows the edge texel instead of the border |
| D-04 | Line width > 1, `GL_LINE_SMOOTH`, `GL_POLYGON_SMOOTH` | WebGPU draws one-pixel lines and has no smooth hint. Wide lines are not yet expanded to quads | Wide or antialiased lines draw one pixel wide and hard-edged |
| D-05 | User clip planes | Implemented as a fragment `discard` on an interpolated distance, not as geometry clipping (WebGPU's `clip-distances` feature is not yet available anywhere) | Geometry is not actually clipped, only its fragments; a depth pre-pass combined with clip planes can differ |
| D-06 | Multisampling | The current render targets are single-sampled; sample coverage maps to the one available coverage bit | Coverage value 0 can suppress samples, but intermediate coverage values do not gain multisample quality |
| D-07 | Occlusion query sample counts | WebGPU's occlusion query answers "did any sample pass"; a visible result reports a saturated count. A GL query spanning passes/submissions is split into GPU segments, whose visibility is ORed. Availability waits for GL end and every segment's readback; stale generations are discarded, and failed readbacks conservatively report visible | An algorithm thresholding on the *number* of samples sees the saturated value rather than an exact count |
| D-08 | `glDrawBuffer(GL_FRONT)` | There is no front buffer; writes to it are refused and counted | Old debugging code that draws directly to the front buffer produces nothing |
| D-09 | Accumulation buffer | **Resolved with an RGBA16Float ping-pong accumulation target.** `GL_ACCUM`, `GL_LOAD`, `GL_RETURN`, `GL_MULT`, `GL_ADD` and masked/scissored clears are implemented | Extremely high dynamic-range accumulation has 16-bit-float precision rather than an implementation-selected desktop format |
| D-10 | Texture fetch in non-uniform control flow | `textureSample` requires uniform control flow; a fetch inside a conditional or loop uses `textureSampleGrad` with the coordinate's derivatives. Counted as `stats.nonUniformSamples` | Mip selection inside a divergent branch can differ slightly from desktop |
| D-11 | `noise1()` … `noise4()` | Return 0, which the GLSL spec permits and every desktop driver does | A shader genuinely relying on GLSL noise -- already broken on real hardware |
| D-12 | `GL_MAX_VARYING_FLOATS` | 16 vec4 slots (WebGPU's `maxInterStageShaderVariables`), reported as 64 floats. Linking fails with a message naming this code when a program needs more | A program with more than 16 packed varying slots does not link. Desktop's floor is 8 vec4, so this is the more generous limit |
| D-13 | `glFinish` | Answered after `queue.submit()` rather than after `onSubmittedWorkDone()` | Code using `glFinish` to time GPU work measures submission, not completion |
| D-14 | Colour-index rendering | `glIndex*` and a colour-index framebuffer are not implemented (paletted *textures* are) | 1996-era colour-index code produces nothing; no target game uses it |
| D-15 | `glDrawPixels`, `glBitmap`, `glCopyPixels` | Colour `DrawPixels`, bitmaps and colour copies use textured quads/copies with pixel-store, transfer, zoom, alpha, depth/stencil, blending, masks and scissor state. Depth/stencil pixel copies remain refused | Software paths that copy depth/stencil rectangles cannot use the pixel API |
| D-16 | Line and polygon stipple | **Polygon stipple is resolved** with the exact 32x32 pattern in fragment coordinates. Line stipple is retained but not yet applied | Stippled lines draw solid; stippled polygons do not |
| D-17 | `glPolygonMode(GL_LINE\|GL_POINT)` | **Resolved by converting triangles to edge or vertex index lists.** If front/back modes differ without culling, the front mode is used for both and reported | Shared triangle edges may be rasterized twice; different uncullled front/back modes cannot be represented in one WebGPU draw |
| D-18 | An ARB fragment program with the fixed vertex pipeline | Refused: the two do not share a varying layout. The other direction -- `GL_VERTEX_PROGRAM_ARB` with the fixed fragment stage -- is resolved by generating the fixed fragment shader against the ARB stage's varyings | Enabling only `GL_FRAGMENT_PROGRAM_ARB` draws nothing, loudly |

Refusals are never silent. Each one logs once with enough context to locate
it, increments `getStats().refusals`, and -- where GL defines an error for the
situation -- sets it so `glGetError` reports it.

## Capability reporting

`glGetString(GL_EXTENSIONS)` and every `GL_MAX_*` are decided by what this
executor implements against the adapter it actually got, never guessed:

- `GL_EXT_texture_compression_s3tc` is advertised only when the adapter has
  `texture-compression-bc`. Without it, DXT blocks are still accepted and
  decoded deterministically on the CPU, so the picture is identical -- but the
  extension is not claimed, because a guest that believes it will keep handing
  us compressed data forever and deserves to choose.
- `GL_ARB_texture_float` and `GL_ARB_half_float_pixel` require
  `float32-filterable`.
- `GL_ARB_depth_clamp` requires `depth-clip-control`.
- The GLView 1.3/1.4 contract includes generic texture compression, automatic
  mipmap generation, sample coverage, and `GL_SGI_color_matrix`. Full
  `GL_ARB_imaging` is deliberately not advertised.
- `GL_MAX_TEXTURE_SIZE` and friends come from the device's limits.

`glGetString(GL_VERSION)` reports `2.1` and `GL_SHADING_LANGUAGE_VERSION`
reports `1.20`.

## Running the tests

```bash
cd glbridge
node tests/gl_protocol_consistency_test.js     # the wire format against the guest
node tests/gl_shader_translator_test.js        # the GLSL front end
node tests/gl_shader_wgsl_validation_test.js   # its output, through naga
node tests/gl_fixed_function_wgsl_test.js      # the fixed pipeline, through naga
node tests/gl_arb_program_test.js              # ARB assembly, through naga
node tests/gl_executor_test.js                 # the state machine and draw path
node tests/v86_network_bridge_gl_route_test.js # routing
node tests/v86_network_bridge_webgpu_state_test.js # WebGPU save/restore replay
node tools/gen_gl_coverage.js                  # regenerate COVERAGE.md
```

The checked-in browser smoke test also exercises the native WebGPU validation
layer (fixed-function drawing, logic ops, accumulation and `glDrawPixels`):

```bash
cd ../..
python3 -m http.server 8765
# Open http://127.0.0.1:8765/glbridge/tests/gl_webgpu_browser_test.html
```

It reports `PASS` in both the page title and body only after the WebGPU error
scope is clean.

The multipass pixel regression runs an isolated headless Chrome profile (set
`GL_CHROME` to the Chromium executable on non-macOS hosts):

```bash
# From the repository root
node glbridge/tests/gl_multipass_browser_runner.js
```

It compares 262,144 pixels across moving viewpoints, constant/array color
attributes, and direct-VBO/packed-vertex paths. In particular, Cube 2's depth
prepass reads a three-component position directly, while packed byte normals
can force its shaded pass through CPU packing. Both vertex outputs must be
[`@invariant`](https://www.w3.org/TR/WGSL/#invariant) to preserve depth equality
across backend optimization. The pipeline key must also include `stepMode` so
a constant model color never reuses a per-vertex color layout. On the local
Chrome/WebGPU regression, removing either fix independently produces 7,740
incorrect pixels; with both fixes the test passes without validation errors.
An additional 16,384-pixel case forces vertex/index and uniform allocations
across ring boundaries while an occlusion query spans passes and submissions
(278,528 pixels total). Old upload pages stay alive until the complete draw
is encoded, and slices bind their own buffer rather than the current ring.
The test checks unchanged pixels, a completed visible query, and clean native
WebGPU validation; ordinary occluded queries still return zero.
This isolates the rendering defects; it is not an end-to-end game test.

The three suites that say "through naga" validate their generated WGSL with
the compiler wgpu and Firefox use. It is optional -- they skip without it --
but it is the difference between "the translator produced a string" and "the
translator produced a shader", so install it:

```bash
cargo install naga-cli
```

## Where new work goes

New OpenGL work belongs in this directory or in `../openglproxy/`. The old
GL4ES/WebGL backend and its runtime selector were removed; there is one OpenGL
host path. A new deviation gets the next free code in the table above; an
eliminated one keeps its code and is marked resolved.
