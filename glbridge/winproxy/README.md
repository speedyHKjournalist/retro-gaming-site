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

The proxy also advertises and routes the following extension
families through the DMA/WebAssembly bridge: packed pixels, rescale normals,
separate specular color, edge-clamp and texture LOD parameters, 3D and
compressed texture uploads, blend
color/equation/separate factors, cube maps, DOT3 and
crossbar texture environments, transpose matrices, mipmap generation, shadow
texture parameters, fog coordinates, secondary color, point parameters,
stencil wrap, mirrored repeat, point sprites, non-power-of-two textures, and
`GL_ARB_vertex_buffer_object` / `GL_ARB_occlusion_query`, plus the ARB vertex
and fragment program backends used for D3D8 shaders. VBO contents remain in guest memory and are
packed into the existing client-array draw records at draw time, which keeps
the PCI protocol asynchronous while preserving VBO semantics for array and
element buffers. Query objects use WebGL2 boolean occlusion queries underneath.
The first result request in a frame refreshes every pending guest query in one
PCI record (up to Cube 2's 2048-object pool), and later reads use the proxy
cache. A visible boolean result is exposed as a saturated desktop sample count,
so engines using a fragment threshold do not mistake WebGL's value `1` for an
almost-empty query. A not-yet-ready blocking result is conservatively visible.

The extended WineD3D profile supports DXT1/3/5, mipmapped cube and volume
textures, ARB-program-backed D3D8 shaders, and eight fixed-function texture
stages. The host module requires WebGL2; it does not silently fall back to a
renderer that cannot execute volume textures. Because upstream gl4es maps
`GL_TEXTURE_3D` onto its GLES2 2D compatibility path, the integration wrapper
keeps gl4es's desktop state bookkeeping but redirects marked volume objects,
uploads, subimage updates, sampler parameters and mipmap generation to real
WebGL2 3D textures. Emscripten's WebGL2 backwards-compatibility conversion
upgrades the generated GLSL 100 `texture3D()` shaders to GLSL 300 ES. The
wrapper also normalizes WineD3D's null BGRA/packed-pixel mip allocations and
populated BGR/BGRA/RGBA packed uploads to WebGL2-valid RGBA8 storage. It
restores gl4es-generated `_gl4es_Sampler3D_*` uniforms to explicitly qualified
`sampler3D` declarations, so later 3D subimage uploads and ARB fragment-program
sampling operate on the same complete mip chain.

Most fixed-function `glGet*` queries return values cached in the guest proxy
plus conservative defaults. Shader/program status, logs, limits, parameters,
and error strings use ordered synchronous browser queries because WineD3D
depends on the compile/link result before its first draw.

The OpenGL extension string and hardware limits are capability snapshots, not
static browser guesses. The JS bridge reads the live WebGL2 context and the
guest clamps its result to bridge-owned tables (8 texture units, 16 generic
attributes, 4 draw buffers and 4 color attachments). `GL_ARB_texture_float`
and `GL_ARB_half_float_pixel` are exposed only when float targets are both
renderable (`EXT_color_buffer_float`) and linearly filterable
(`OES_texture_float_linear`); anisotropy is exposed only when the browser's
anisotropy extension is enabled. A missing/late renderer yields the
conservative bridge-guaranteed profile and never enables those optional paths.
Destroying the last WGL context invalidates the snapshot so a replacement
canvas/context is probed again. S3TC remains advertised independently because
the bridge implements DXT1/3/5 by deterministic CPU decode. Pixel-buffer
objects likewise use guest-side storage and resolve pack/unpack offsets before
the PCI transfer; compiled vertex arrays are a safe performance hint, and the
two-sided stencil extension is routed through separate front/back state.

### OpenGL 1.3 / 1.4 / 1.5 core coverage

`glGetString(GL_VERSION)` reports `2.1` and GLSL reports `1.20`. OpenGL 1.1
through 2.1 core entry
points are exported, available through `wglGetProcAddress`, and routed through
the PCI/WASM transport. This includes every multitexture overload, transpose
matrix operation, compressed texture upload/subimage entry point, separate
blend factors, point integer parameters, fog-coordinate and secondary-color
arrays, multi-draw, the full 2D/3D `glWindowPos*` family, VBO entry points, and
query-object entry points. The 3D texture path also implements and exports
`glCopyTexSubImage3D`/`EXT`, preserving the destination volume slice through
native WebGL2 copy or an exact readback-backed subimage upload.

`GL_EXT_texture_compression_s3tc` is advertised. DXT1/3/5 uploads and partial
block updates retain their compressed guest-side cache/readback representation
but are decoded deterministically to RGBA before reaching WebGL. Explicit mip
chains therefore work without a browser S3TC extension, and
`glGenerateMipmap` now generates real 1D/2D/cube/3D mip chains instead of
demoting the minification filter.

Explicit compressed and uncompressed mip chains are preserved and sampled
normally. The host keeps gl4es automatic mipmap mode disabled so WineD3D's
uploaded levels and `GL_*_MIPMAP_*` min filters reach WebGL unchanged. Legacy BGRA packed
uploads using `GL_UNSIGNED_SHORT_1_5_5_5_REV` and
`GL_UNSIGNED_SHORT_4_4_4_4_REV` are accepted and converted by gl4es to the
WebGL-compatible RGBA packed representation.

`GL_ARB_vertex_program` and `GL_ARB_fragment_program` are advertised for the
WineD3D D3D8 shader backend, including ordered synchronous program/error/cap
queries. Before gl4es conversion, the host bridge renames ARB assembly
identifiers such as WineD3D's `const` register that are reserved in GLSL; it
caches the original program so `GL_PROGRAM_LENGTH_ARB` and
`GL_PROGRAM_STRING_ARB` retain desktop-GL query semantics. WebGL default
texture stand-ins are also rebound per target and texture unit before texture
parameter updates and after deleting a bound texture. `GL_ARB_imaging` remains
outside the advertised profile.

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
  -o d3d8_mipmap_filter_test.exe d3d8_mipmap_filter_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_texture_formats_test.exe d3d8_texture_formats_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_texture_stage_ops_test.exe d3d8_texture_stage_ops_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_dynamic_resources_test.exe d3d8_dynamic_resources_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_raster_stencil_test.exe d3d8_raster_stencil_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_stateblock_test.exe d3d8_stateblock_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_reset_lifecycle_test.exe d3d8_reset_lifecycle_test.c \
  -ld3d8 -lgdi32 -luser32 -lkernel32

i686-w64-mingw32-gcc -mwindows -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 \
  -o d3d8_caps_audit_test.exe d3d8_caps_audit_test.c \
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
d3d8_mipmap_filter_test.exe
d3d8_texture_formats_test.exe
d3d8_texture_stage_ops_test.exe
d3d8_dynamic_resources_test.exe
d3d8_raster_stencil_test.exe
d3d8_stateblock_test.exe
d3d8_reset_lifecycle_test.exe
d3d8_caps_audit_test.exe
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

After fog succeeds, run `d3d8_mipmap_filter_test.exe`. It uploads all eight
levels of a 128x128 A8R8G8B8 mip chain. Level zero is a blue/yellow checker;
levels 1 through 7 are distinct solid colours. Six panels separate minifying,
magnifying, mip-selection, and address-mode behaviour:

```text
top-left:     POINT, no mip, WRAP, UV 0..16 (high-frequency alias reference)
top-middle:   LINEAR + MIP POINT, UV 0..16 (one solid selected mip level)
top-right:    LINEAR + MIP LINEAR, UV 0..16 (blend of adjacent mip colours)
bottom-left:  MAG POINT, CLAMP, UV 0..1 (crisp enlarged checker)
bottom-middle:MAG LINEAR, CLAMP, UV -0.5..1.5 (soft centre, clamped borders)
bottom-right: MAG POINT, WRAP, UV -0.5..1.5 (repeated checker)
```

The top-middle panel should be close to one of the solid mip colours, while
the top-right panel should be an intermediate colour rather than an aliasing
checker. The lower-left checker has hard texel transitions, the lower-middle
has softened transitions and extended edge texels, and the lower-right wraps
the texture outside 0..1. Its success title is
`Present S_OK - top min/mip, bottom mag/address`.

Then run `d3d8_texture_formats_test.exe`. Its 4x2 panel order is:

```text
top:    X8R8G8B8 | A8R8G8B8 | R5G6B5     | A1R5G5B5
bottom: A4R4G4B4 | L8       | A8 grayscale | A8R8G8B8 subrect
```

All full textures are filled row-by-row using the returned `Pitch`. RGB
formats show the same two-axis colour/checker pattern at their respective bit
precision. The alpha-bearing formats blend left-to-right over dark blue:
A1R5G5B5 has one abrupt alpha transition, while A4R4G4B4 and A8R8G8B8 show
progressively smoother steps. L8 is grayscale; A8 is displayed with
`D3DTA_ALPHAREPLICATE`. The last panel starts purple, then a 32x32 subrectangle
lock replaces only its centre with a green/yellow checker. Pixels outside that
centre must remain purple. Success is
`Present S_OK - 7 formats plus green/yellow subrect`.

Next run `d3d8_texture_stage_ops_test.exe`. It uses constant stage-0 RGBA
`(64,128,192,64)` and stage-1 RGBA `(128,64,32,192)` textures. The 3x3 grid is:

```text
base reference       | MODULATE2X       | ADD
ADDSIGNED            | SUBTRACT         | BLENDTEXTUREALPHA
BLENDCURRENTALPHA    | COMPLEMENT       | ALPHAREPLICATE
```

Approximate expected RGB values are respectively `(64,128,192)`, `(64,64,48)`,
`(192,192,224)`, `(64,64,96)`, `(0,64,160)`, `(112,80,72)`, `(80,112,152)`,
`(127,191,223)`, and `(192,192,192)`. This makes an ignored operation or wrong
argument source visible as a panel matching another cell. Success is
`Present S_OK - 3x3 stage-op colour grid`.

Finally run `d3d8_dynamic_resources_test.exe`. It creates default-pool dynamic
VB and IB ring buffers with three slots. Every frame, slot zero is locked with
`D3DLOCK_DISCARD`; slots one and two are appended with
`D3DLOCK_NOOVERWRITE`. Each slot is drawn before the next append. Three
red/green/blue gradient rectangles continuously move in separate horizontal
lanes, and their INDEX16 diagonal changes every 30 frames so both vertex and
index updates remain visible.

The title must continuously advance through
`frame N Present S_OK - VB+IB DISCARD then 2x NOOVERWRITE`. Leave it running
until at least frame 600. A frozen lane indicates stale VB contents, a missing
or torn lane indicates an overwritten append region, and an unchanged or
corrupted colour diagonal indicates stale IB contents. Any failed lock/draw
stops the timer and leaves its frame, slot, and lock mode in the title.

Run `d3d8_raster_stencil_test.exe` next. Its top row checks opposite triangle
windings under `D3DCULL_CW`/`D3DCULL_CCW`, an untransformed NDC quad mapped by
a restricted `D3DVIEWPORT8`, and isolated red/green/blue colour-write masks.
The bottom row checks coplanar `D3DRS_ZBIAS`, a three-pass stencil write and
`EQUAL`/`NOTEQUAL` split, and paired `ZWRITEENABLE=FALSE/TRUE` probes. The
expected bottom-left result is red without bias and green with bias; the
stencil panel is a green diamond inside a red rectangle; the Z-write panel is
yellow on the left and blue on the right. D3D8 has no D3D9-style
`SetScissorRect` or `D3DRS_SCISSORTESTENABLE`, so viewport mapping/clipping is
the deliberate D3D8 clipping comparison rather than a synthetic scissor API.

Then run `d3d8_stateblock_test.exe`. Its three panels are a direct-state
reference, a block recorded with `BeginStateBlock`/`EndStateBlock`, and a
`D3DSBT_ALL` block made with `CreateStateBlock`. Before each panel the test
deliberately unbinds the texture and corrupts blend, depth, and stage state.
After `ApplyStateBlock`, it checks the texture COM pointer and reads every
important state back before drawing. All three panels must be identical
semi-transparent blue/yellow checkers over dark blue, with no red depth probe.

`d3d8_reset_lifecycle_test.exe` first performs four complete
CreateDevice/Clear/Present/Release cycles and requires every device reference
count to reach zero. A long-lived fifth device then presents at 480x360,
releases its default-pool VB, resizes the client and backbuffer to 720x480,
calls `Reset`, verifies that the managed checker texture description and
contents survived. Managed DXT5, cube (all faces/mips), and volume (all
slices/mips) resources are also populated and preloaded both before and after
Reset. The test then recreates the default-pool VB and presents through the 2D
texture and VB. The final window must be 720x480 and contain the repeated
blue/yellow checker. Inspect the browser log as well: the four renderer
shutdown/startup cycles must not crash, accumulate warnings, or show growing
resource-leak counts. Renderer shutdown deliberately uses gl4es's no-clean
path: each guest WGL context owns a disposable WASM instance, so the bridge
flushes it, runs the lightweight shutdown hooks, explicitly loses/destroys the
WebGL context, and then drops the whole linear-memory instance. This avoids
gl4es's unsafe deep ARB-shader/render-list teardown while still releasing the
browser context deterministically. Both the guest proxy and JS bridge coalesce
the `WM_NCDESTROY`/`wglDeleteContext` pair so one guest context is destroyed
and replaced only once.

Finally run `d3d8_caps_audit_test.exe`. It always prints a complete
`GetDeviceCaps` dump and `CheckDeviceFormat` matrix to guest debug output. The
four dashboard bars represent core texture/depth formats, extended texture
formats, tested fixed-pipeline caps, and required extended caps. Green means
consistent; red means that category has at least one mismatch. The extended
profile requires DXT1/3/5, cube and volume textures with complete mip chains,
vertex/pixel shader 1.1 or newer, at least 96 vertex constants, and eight
simultaneous textures/stages. The audit also creates, locks, updates, binds,
and draws all three DXT formats, all cube faces/mips, all volume mips, an
eight-stage combiner, and a vertex/pixel shader pair so a caps-only false
positive cannot pass. A8R8G8B8 render-target textures are also exercised
end-to-end through CreateTexture, GetSurfaceLevel, SetRenderTarget, Clear,
backbuffer restoration and texture sampling. This uses WineD3D's backbuffer
offscreen path and does not advertise the still-incomplete OpenGL FBO
extension family. For `glCopyTexImage2D` and `glCopyTexSubImage2D`, the guest
proxy reuses its framebuffer readback and transmits tightly packed
`glTexImage2D` / `glTexSubImage2D` updates. This avoids an invalid
default-framebuffer RGB to A8R8G8B8 texture copy and stores explicit pixels in
the save-state journal. Repeated full-level updates with identical unpack
state are compacted to the newest payload, while mipmap generation and other
content dependencies form conservative compaction barriers. Raw copy
operations that cannot be captured remain intentionally non-reconstructible.

The OpenGL-only diagnostics can then be run with `gl_triangle_test.exe`,
`gl_rotate_cube_test.exe`, or
`gl_client_arrays_test.exe`, `gl_blend_ui_test.exe`, or
`gl_query_multitexture_test.exe`, or `gl_fog_material_test.exe`.

The demo calls the fake WGL/OpenGL subset directly and presents with
`wglSwapLayerBuffers` plus `glFlush`, so it does not depend on intercepting
`gdi32.dll`'s real `SwapBuffers`.

## Build the gl4es host module

Build the WebGL2 browser module from an active emsdk shell. By default the script uses:

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
