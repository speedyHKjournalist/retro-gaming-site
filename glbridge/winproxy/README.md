# Simple v86 OpenGL wrapper

This is a deliberately tiny fake `opengl32.dll` for experiments in v86.

It does **not** implement real OpenGL. It captures a small WGL/OpenGL subset,
writes the command stream into a contiguous guest-RAM buffer provided by
`v86gl.sys`, and rings the v86gl PCI I/O BAR. The v86 device reads that
descriptor once and sends the command stream to the browser-side gl4es/WebGL
bridge. There is no UDP or v86 network-device transport.

## What it supports

Guest DLL exports:

- `wglCreateContext`
- `wglDeleteContext`
- `wglMakeCurrent`
- `wglGetCurrentContext`
- `wglGetCurrentDC`
- `wglGetProcAddress`
- `wglShareLists`
- `wglSwapLayerBuffers`
- `wglSwapBuffers`
- `glGetString`
- `glGetError`
- `glGetIntegerv`
- `glGetFloatv`
- `glIsEnabled`
- `glViewport`
- `glClearColor`
- `glClear`
- `glBegin`
- `glEnd`
- `glColor3f`
- `glColor4f`
- `glVertex2f`
- `glVertex3f`
- `glVertex4f`
- `glFlush`
- `glFinish`
- `glMatrixMode`
- `glLoadIdentity`
- `glFrustum`
- `glOrtho`
- `glTranslatef`
- `glRotatef`
- `glScalef`
- `glPushMatrix`
- `glPopMatrix`
- `glClearAccum`
- `glAccum`
- `glEnable`
- `glDisable`
- `glDepthFunc`
- `glClearDepth`
- `glShadeModel`
- `glCullFace`
- `glFrontFace`
- `glGenTextures`
- `glDeleteTextures`
- `glBindTexture`
- `glTexImage1D`
- `glTexImage2D`
- `glTexSubImage1D`
- `glTexSubImage2D`
- `glCopyTexImage1D`
- `glCopyTexSubImage1D`
- `glTexParameteri`
- `glTexParameterf`
- `glGetTexParameteriv`
- `glGetTexParameterfv`
- `glPixelStorei`
- `glTexEnvi`
- `glTexEnvf`
- `glTexCoord2f`
- `glTexCoord4f`
- `glActiveTextureARB` / `glActiveTexture`
- `glClientActiveTextureARB` / `glClientActiveTexture`
- `glMultiTexCoord*ARB` common scalar/vector variants
- `glNormal3f`
- `glFogf`
- `glFogi`
- `glFogfv`
- `glMaterialf`
- `glMateriali`
- `glMaterialfv`
- `glMaterialiv`
- `glBlendFunc`
- `glAlphaFunc`
- `glDepthMask`
- `glColorMask`
- `glScissor`
- `glPointSize`
- `glLineWidth`
- `glLineStipple`
- `glPolygonMode`
- `glPolygonStipple`
- `glLogicOp`
- `glPixelMapfv`
- `glPixelMapuiv`
- `glPixelMapusv`
- `glPixelTransferf`
- `glPixelTransferi`
- `glPixelZoom`
- `glBitmap`
- `glCopyPixels`
- `glDrawPixels`
- `glReadPixels` synchronous PCI DMA readback for supported pixel formats
- `glEnableClientState`
- `glDisableClientState`
- `glVertexPointer`
- `glColorPointer`
- `glTexCoordPointer`
- `glNormalPointer`
- `glDrawArrays`
- `glDrawElements`
- `glInterleavedArrays`
- `glMap1*`
- `glMap2*`
- `glMapGrid1*`
- `glMapGrid2*`
- `glEvalCoord*`
- `glEvalPoint*`
- `glEvalMesh*`
- OpenGL 1.5 VBO entry points and `GL_ARB_vertex_buffer_object` aliases
- OpenGL 1.5 query-object entry points and `GL_ARB_occlusion_query` aliases
- basic display-list compile/replay through `glNewList`, `glEndList`,
  `glCallList`, and `glCallLists`

The proxy also advertises and routes the following fixed-function extension
families through the DMA/WebAssembly bridge: packed pixels, rescale normals,
separate specular color, edge-clamp and texture LOD parameters, 3D and
compressed texture uploads, blend
color/equation/separate factors, cube maps, multisample coverage, DOT3 and
crossbar texture environments, transpose matrices, mipmap generation, shadow
texture parameters, fog coordinates, secondary color, point parameters,
stencil wrap, mirrored repeat, point sprites, non-power-of-two textures, and
`GL_ARB_vertex_buffer_object` / `GL_ARB_occlusion_query`. VBO contents remain in guest memory and are
packed into the existing client-array draw records at draw time, which keeps
the PCI protocol asynchronous while preserving VBO semantics for array and
element buffers. Query objects are implemented as a conservative synchronous
compatibility layer: `GL_SAMPLES_PASSED` queries complete immediately with a
nonzero result so visibility-dependent fixed-function applications do not
accidentally cull everything when running through WebGL/gl4es.

It is enough for toy fixed-pipeline demos such as a triangle or a rotating
colored cube. It is **not** enough for WineD3D or real games yet.

`glGet*` queries currently return values cached in the guest proxy plus
conservative defaults. They do not synchronously query browser/WebGL state.

### OpenGL 1.3 / 1.4 / 1.5 core coverage

`glGetString(GL_VERSION)` reports `1.5`. OpenGL 1.1 through 1.5 core entry
points are exported, available through `wglGetProcAddress`, and routed through
the PCI/WASM transport. This includes every multitexture overload, transpose
matrix operation, compressed texture upload/subimage entry point, separate
blend factors, point integer parameters, fog-coordinate and secondary-color
arrays, multi-draw, the full 2D/3D `glWindowPos*` family, VBO entry points, and
query-object entry points.

Compressed-texture entry points and readback caching are implemented in the
proxy, but `GL_ARB_texture_compression` is intentionally not advertised: the
current WebGL/gl4es backend cannot reliably generate mipmaps for all legacy
compressed formats. This lets games select their uncompressed fallback rather
than sampling an incomplete texture as black.

`GL_ARB_imaging` and `GL_ARB_vertex_program` are separate optional extensions,
not requirements of the OpenGL 1.4 core profile; they are deliberately not
advertised until their complete execution paths are implemented.

## Build the DLL

From Linux/macOS with mingw-w64:

```bash
cd src/glbridge/winproxy
i686-w64-mingw32-gcc -shared -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 \
  -o opengl32.dll opengl32_proxy.c opengl32.def \
  -Wl,--kill-at -luser32 -lgdi32 -lkernel32
```

Build and start `../v86gl_driver/v86gl.sys` before running a guest OpenGL
application. Its WDK build and installation steps are in
`../v86gl_driver/README.md`.

Build the test programs:

```bash
cd src/glbridge/sample
i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_clear_test.exe d3d8_clear_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_triangle_test.exe d3d8_triangle_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_texture_test.exe d3d8_texture_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_indexed_test.exe d3d8_indexed_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_transform_depth_test.exe d3d8_transform_depth_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_textured_cube_test.exe d3d8_textured_cube_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_alpha_blend_test.exe d3d8_alpha_blend_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_multitexture_test.exe d3d8_multitexture_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_lighting_test.exe d3d8_lighting_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_fog_test.exe d3d8_fog_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_triangle_test.exe gl_triangle_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_rotate_cube_test.exe gl_rotate_cube_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_client_arrays_test.exe gl_client_arrays_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_blend_ui_test.exe gl_blend_ui_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_query_multitexture_test.exe gl_query_multitexture_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o gl_fog_material_test.exe gl_fog_material_test.c \
  -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_depth_clear_poison.exe gl_test_depth_clear_poison.c -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_blend_alpha_fade.exe gl_test_blend_alpha_fade.c -lopengl32 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_swap_postclear.exe gl_test_swap_postclear.c -lopengl32 -lgdi32 -luser32 -lkernel32
```

These commands intentionally avoid the MinGW C runtime. Some modern MinGW-w64
packages link tiny programs against Universal CRT `api-ms-win-crt-*` DLLs, which
are not present in Windows XP/98 by default.

Copy the required files into the same folder in the Windows XP guest:

```text
opengl32.dll
d3d8.dll                 WineD3D 1.7.52, 32-bit
wined3d.dll              WineD3D 1.7.52, 32-bit
d3d8_clear_test.exe
d3d8_triangle_test.exe
d3d8_texture_test.exe
d3d8_indexed_test.exe
d3d8_transform_depth_test.exe
d3d8_textured_cube_test.exe
d3d8_alpha_blend_test.exe
d3d8_multitexture_test.exe
d3d8_lighting_test.exe
d3d8_fog_test.exe
gl_triangle_test.exe
gl_rotate_cube_test.exe
gl_client_arrays_test.exe
gl_blend_ui_test.exe
gl_query_multitexture_test.exe
gl_fog_material_test.exe
```

Run `d3d8_clear_test.exe` first. It deliberately imports only `d3d8.dll` and
renders one solid blue frame through:

```text
Direct3DCreate8 -> GetAdapterDisplayMode -> CheckDeviceType -> CreateDevice
    -> Clear -> BeginScene -> EndScene -> Present
```

The window title reports the last failure HRESULT, or
`Present S_OK - expected solid blue` on success. Guest debug output prefixes
each stage with `[d3d8-smoke]`; the corresponding proxy/host log should include
`wglSwapBuffers`, `present requested`, a PCI submit with `present=1`, and a
browser `presentFrame` call. The test is restricted to a 32-bit process, one
window, one backbuffer, windowed mode, software vertex processing, and no
multisampling, depth/stencil buffer, reset, shaders, or render-to-texture.

After the clear test succeeds, run `d3d8_triangle_test.exe`. It extends the
same path with the smallest fixed-pipeline draw:

```text
CreateVertexBuffer -> Lock/copy/Unlock -> SetStreamSource
    -> SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
    -> Clear -> BeginScene -> DrawPrimitive -> EndScene -> Present
```

The expected result is one red/green/blue interpolated triangle on a black
background, with the title `Present S_OK - expected RGB triangle`. The test
uses pre-transformed vertices, vertex diffuse colour, no texture, no lighting,
no culling, no depth buffer, and no programmable shaders. Debug output is
prefixed with `[d3d8-triangle]`, and every newly exercised D3D8 call reports
its HRESULT independently. Before each call, the window title is changed to
`D3D8 triangle: calling NN API-name`; if WineD3D blocks, the title therefore
identifies the exact call that did not return.

After the coloured triangle succeeds, run `d3d8_texture_test.exe`. It adds a
single-level 64x64 managed `D3DFMT_A8R8G8B8` texture and exercises:

```text
CheckDeviceFormat -> CreateTexture -> LockRect/fill/UnlockRect
    -> CreateVertexBuffer(XYZRHW | DIFFUSE | TEX1)
    -> SetTexture(0) -> MODULATE(texture, diffuse)
    -> POINT filtering -> DrawPrimitive(two triangles) -> Present
```

The expected result is a blue/yellow 8x8 checkerboard enlarged inside a black
window, with crisp point-filtered edges and the title
`Present S_OK - expected blue/yellow checkerboard`. The test uses no mipmaps,
lighting, depth buffer, culling, alpha blending, programmable shaders, or
render-to-texture. Debug output is prefixed with `[d3d8-texture]`; the window
title is updated before all 34 D3D8 calls so a blocked call remains visible.

After the texture test succeeds, run `d3d8_indexed_test.exe`. It deliberately
returns to the known-good untextured `XYZRHW | DIFFUSE` path and changes only
the geometry submission path:

```text
CreateVertexBuffer(4 vertices) -> CreateIndexBuffer(6 x INDEX16)
    -> Lock/copy/Unlock -> SetIndices(base vertex 0)
    -> DrawIndexedPrimitive(2 triangles) -> Present
```

The expected result is a red/green/blue/yellow interpolated rectangle on a
black background, with the title
`Present S_OK - expected indexed RGB quad`. Debug output is prefixed with
`[d3d8-indexed]`; the window title is updated before all 27 D3D8 calls. This
test still has no texture, matrices, depth buffer, lighting, culling, or
programmable shaders, so a failure is isolated to the index-buffer path.

Only after the indexed quad succeeds, run `d3d8_transform_depth_test.exe`.
It retains `INDEX16` submission and adds the remaining fixed-pipeline pieces:

```text
CheckDeviceFormat(D24S8) -> CheckDepthStencilMatch(D24S8)
    -> CreateDevice(automatic D24S8 depth-stencil)
    -> CreateVertexBuffer(XYZ | DIFFUSE) -> CreateIndexBuffer
    -> SetTransform(WORLD, VIEW, PROJECTION)
    -> enable Z test/write -> Clear(target + Z)
    -> DrawIndexedPrimitive(12 cube triangles) -> Present
```

The expected result is a static, rotated, colour-interpolated cube whose
hidden surfaces are removed by the depth buffer, with the title
`Present S_OK - expected depth-tested colour cube`. The far face is submitted
after the near face, making correct visibility depend on Z testing rather than
submission order. Debug output is prefixed with `[d3d8-transform-depth]`; the
window title is updated before all 34 D3D8 calls. `D24S8` deliberately matches
the bridge's single conservative WGL pixel format (24 depth + 8 stencil bits).
If the test fails at call 04 or 05, treat that as a D24S8 capability/matching
failure; calls 27-29 isolate the
three transform uploads, call 30 isolates depth clear, and call 32 isolates
the indexed cube draw.

After the static transform/depth cube succeeds, run
`d3d8_textured_cube_test.exe`. This is the first multi-frame integration test
and combines all of the validated D3D8 fixed-pipeline paths:

```text
CreateTexture(A8R8G8B8) -> LockRect/fill/UnlockRect
    -> CreateVertexBuffer(XYZ | DIFFUSE | TEX1, 24 face-local vertices)
    -> CreateIndexBuffer(36 x INDEX16) -> MODULATE(texture, diffuse)
    -> SetTransform(VIEW, PROJECTION)
    -> every frame: SetTransform(WORLD) -> Clear(target + Z)
       -> DrawIndexedPrimitive(12 triangles) -> Present
```

The expected result is a continuously rotating, blue/yellow checkerboard cube
on black, with differently tinted faces and correct depth occlusion. The title
must repeatedly advance through
`frame N Present S_OK - rotating checker cube`; leave it running for at least
180 frames to cover a full revolution. Setup has 39 individually titled D3D8
checkpoints. During animation, the title identifies the current frame and the
last `SetTransform`, `Clear`, `BeginScene`, `DrawIndexedPrimitive`, `EndScene`,
or `Present` call, so delayed state corruption is distinguishable from setup
failure. The test deliberately keeps lighting, culling, mipmaps, alpha
blending, programmable shaders, and render-to-texture disabled.

After the rotating textured cube remains stable for at least one revolution,
run `d3d8_alpha_blend_test.exe`. It uses two A8R8G8B8 textures and five
pre-transformed quads to isolate the transparent fixed-pipeline states:

```text
opaque dark-blue background with ZWRITE
    -> left: ALPHATEST(GREATER, ref=127), ZWRITE=TRUE
       -> draw a farther cyan probe over the same rectangle
    -> right: ALPHABLEND=TRUE, SRCBLEND=SRCALPHA,
       DESTBLEND=INVSRCALPHA, ZWRITE=FALSE
       -> draw a farther green probe stripe across the blended rectangle
    -> Present
```

The expected left panel is a cyan square with a blue/yellow checker diamond in
front. The texture's transparent and alpha=64 magenta pixels must be absent;
the cyan probe appears only where those fragments were discarded and must not
overwrite the opaque diamond. The expected right panel is an orange alpha
gradient over the dark-blue background. A solid green vertical stripe drawn
farther away and after the blend must remain continuous through that panel,
proving that the transparent pass did not write depth. The success title is
`Present S_OK - left cutout, right blend, probes visible`.

Failure signatures are intentionally distinct: visible magenta means alpha
test/discard failed, an opaque orange rectangle means source-alpha blending
failed, a missing cyan surround means discarded fragments wrote depth, and a
green stripe interrupted only inside the blended rectangle means
`ZWRITEENABLE=FALSE` was not honoured. The window title is updated before each
new D3D8 state or draw call so the last call remains visible if WineD3D blocks.

After the alpha test succeeds, run `d3d8_multitexture_test.exe`. It uses an
`XYZRHW | DIFFUSE | TEX2` vertex buffer, two A8R8G8B8 textures, and three
side-by-side quads to isolate the two-stage fixed texture pipeline:

```text
stage 0: coordinate set 0, repeated blue/yellow checker * DIFFUSE
    -> left draw with stage 1 disabled
    -> centre draw: stage 1 MODULATE(CURRENT, grayscale lightmap)
    -> right draw: stage 1 ADD(CURRENT, grayscale lightmap)
    -> Present
```

The stage-0 coordinates repeat the checker twice in each direction, while
stage 1 selects coordinate set 1 and clamps a single coarse grayscale
lightmap across each panel. The expected result is: an unchanged bright
checker on the left; the same checker dark-to-bright under modulation in the
centre; and a progressively brighter, partly saturated checker under ADD on
the right. The success title is
`Present S_OK - base | MODULATE lightmap | ADD lightmap`.

The test first requires `MaxSimultaneousTextures >= 2`,
`MaxTextureBlendStages >= 2`, and the MODULATE/ADD texture-op capability bits,
then updates its title before all 55 capability, resource, state, draw, and
present calls. If the centre and right match the left, stage 1 is not active.
Grayscale-only centre/right panels mean stage-0 `CURRENT` was lost. A
twice-repeated coarse lightmap, or an unrepeated base checker, indicates a
`TEXCOORDINDEX`/TEX2 mix-up. A right panel that matches the centre indicates
that `D3DTOP_ADD` was not applied.

After the two-stage texture test succeeds, run `d3d8_lighting_test.exe`. It
uses 24 face-local `XYZ | NORMAL` vertices, an INDEX16 cube, and the already
validated World/View/Projection plus D24S8 paths. No vertex colour or texture
is present, so the visible colour must come from the fixed-function material
and lighting calculation:

```text
SetMaterial(blue ambient, orange diffuse)
    -> SetLight(0, warm directional light)
    -> left: LightEnable(0, FALSE), draw ambient-only cube
    -> right: LightEnable(0, TRUE), draw directionally lit cube
    -> Present
```

The expected result is two equally rotated, depth-tested cubes on black. The
left cube should be a uniform dark blue across its visible faces because only
global ambient light is active. The right cube should be brighter and mostly
gold/orange, with clearly different brightness on its top, front, and side
faces according to their transformed normals. Its success title is
`Present S_OK - left ambient blue, right directional gold`.

The test checks for at least one active light and the directional-light vertex
processing capability before creating the device, sets `COLORVERTEX=FALSE`
so `SetMaterial` is authoritative, and enables `NORMALIZENORMALS`. A black
left cube means the global ambient/material ambient path failed. Identical
flat blue cubes mean `LightEnable` or the directional contribution failed. A
uniform bright right cube means the `NORMAL` stream or its world transform was
not applied. Missing/incorrect hidden faces instead indicate regression in
the already-tested depth or matrix path. The title is updated before each of
the 44 capability, resource, lighting-state, draw, and present checkpoints.

After the lighting test succeeds, run `d3d8_fog_test.exe`. It draws two rows
of five equal-sized, untextured quads. Their eye-space depths are 2, 4, 6, 8,
and 10, matching linear fog start/end values of 2 and 10. Geometry is scaled
against the projection matrix so apparent size remains constant and only fog
colour changes:

```text
top orange row: FOGTABLEMODE=NONE, FOGVERTEXMODE=LINEAR
    -> draw five increasing-depth quads
bottom green row: FOGVERTEXMODE=NONE, FOGTABLEMODE=LINEAR
    -> draw the same five depths -> Present
```

Both rows should progress left-to-right through approximately 0%, 25%, 50%,
75%, and 100% fog. The first top rectangle is bright orange and the first
bottom rectangle is bright green. Each later rectangle approaches the same
blue-gray fog colour; the two far-right rectangles should therefore look
almost identical even though their original colours differ. The success title
is `Present S_OK - top vertex orange, bottom table green, both fade`.

Before device creation the test requires the `FOGVERTEX`, `FOGTABLE`, and at
least one of `ZFOG`/`WFOG` raster capability bits. It uses eye-space start/end
for WFOG, or projects the same endpoints into the 0..1 depth range for a
ZFOG-only device. An unfogged top row isolates the vertex-fog path; an unfogged
bottom row isolates table/pixel fog. Both rows remaining at their original
colours means `FOGENABLE` or the linear fog parameters failed. If the
rectangles change size with depth, that is a matrix/vertex-input regression
rather than a fog failure. The title is updated before all 40 capability,
resource, fog-state, draw, and present checkpoints.

The OpenGL-only diagnostics can then be run with `gl_triangle_test.exe`,
`gl_rotate_cube_test.exe`, or
`gl_client_arrays_test.exe`, `gl_blend_ui_test.exe`, or
`gl_query_multitexture_test.exe`, or `gl_fog_material_test.exe`.

The demo calls the fake WGL/OpenGL subset directly and presents with
`wglSwapLayerBuffers` plus `glFlush`, so it does not depend on intercepting
`gdi32.dll`'s real `SwapBuffers`.

## Build the gl4es host module

Build the browser module from an active emsdk shell. By default the script uses:

```text
/Users/renruisi/Desktop/Code/gl4es/include
/Users/renruisi/Desktop/Code/gl4es/lib/libGL.a
```

```bash
cd glbridge/libglwasm
./build_gl4es_module.sh
```

If gl4es lives somewhere else:

```bash
GL4ES_ROOT=/path/to/gl4es ./build_gl4es_module.sh
```

This generates:

```text
glbridge/libglwasm/gl4es.js
glbridge/libglwasm/gl4es.wasm
```

The generated module exports stable `v86gl_gl*` wrapper functions. The browser
bridge calls those wrappers, and the wrappers forward into `gl4es_gl*`.

## v86 browser side

Include `gl4es.js`, `gl4es_loader.js`, and `v86_network_bridge.js` in your v86
page. Add an overlay canvas above the v86 screen canvas:

```html
<canvas id="v86gl_canvas"
        style="position:absolute;left:0;top:0;display:none;pointer-events:none"></canvas>
```

After creating the v86 emulator:

```js
const v86gl = installV86GLNetworkBridge(
    emulator,
    document.getElementById("v86gl_canvas"),
    { gl4es: window.GL4ES }
);
```

The bridge listens to:

```js
emulator.add_listener("v86gl-pci-frame", ...)
```

It executes the descriptor command stream directly and renders it into the
WebGL canvas. Create the emulator with `v86gl_pci` enabled and set
`net_device.type` to `"none"` when no guest networking is required.

For a host-only smoke test, open:

```text
src/glbridge/sample/host_triangle_demo.html
```

That page predates the PCI-only transport and is not a smoke test for this
configuration.

## Important limitations

- `v86gl.sys` currently uses the fixed BAR base `0xF100`. Keep the v86gl PCI
  BAR at that address until the driver is upgraded to use PnP PCI resources.
- The fixed-pipeline matrix stack, depth test, shading mode, face-culling, blending/alpha/scissor state, 1D/2D/3D and compressed textures, multitexture, lighting, and client arrays above are forwarded to gl4es. It is still not a WineD3D implementation and does not provide OpenGL 1.5+ programmable-pipeline compatibility.
- `glReadPixels` performs a synchronous PCI DMA readback. The host writes the result into the response area of the submitted guest-RAM command record before the IOCTL returns. It currently requires the browser-side gl4es renderer to be ready and supports the pixel formats accepted by the proxy's pack-state validation.
- `SwapBuffers` is exported by `gdi32.dll`, not `opengl32.dll`; normal apps that import `SwapBuffers` from `gdi32.dll` are not intercepted by this DLL. This toy bridge presents on `glFlush`, `glFinish`, `wglSwapLayerBuffers`, and the nonstandard helper export `wglSwapBuffers`.
- The driver exposes one shared buffer, so only one guest process can own the
  transport at a time.
