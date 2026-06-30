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
  -Wl,--kill-at -luser32 -lkernel32
```

Build and start `../v86gl_driver/v86gl.sys` before running a guest OpenGL
application. Its WDK build and installation steps are in
`../v86gl_driver/README.md`.

Build the test programs:

```bash
cd src/glbridge/sample
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

Copy both files into the same folder in the Windows XP guest:

```text
opengl32.dll
gl_triangle_test.exe
gl_rotate_cube_test.exe
gl_client_arrays_test.exe
gl_blend_ui_test.exe
gl_query_multitexture_test.exe
gl_fog_material_test.exe
```

Run `gl_triangle_test.exe`, `gl_rotate_cube_test.exe`, or
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
