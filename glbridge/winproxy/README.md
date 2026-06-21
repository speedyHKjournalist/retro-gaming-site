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
- `glTexImage2D`
- `glTexSubImage2D`
- `glTexParameteri`
- `glTexParameterf`
- `glGetTexParameteriv`
- `glGetTexParameterfv`
- `glPixelStorei`
- `glTexEnvi`
- `glTexEnvf`
- `glTexCoord2f`
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
- `glLineWidth`
- `glPolygonMode`
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

The proxy also advertises and routes the following fixed-function extension
families through the DMA/WebAssembly bridge: packed pixels, rescale normals,
separate specular color, edge-clamp and texture LOD parameters, 3D and
compressed texture uploads, blend
color/equation/separate factors, cube maps, multisample coverage, DOT3 and
crossbar texture environments, transpose matrices, mipmap generation, shadow
texture parameters, fog coordinates, secondary color, point parameters,
stencil wrap, mirrored repeat, point sprites, non-power-of-two textures, and
`GL_ARB_vertex_buffer_object`. VBO contents remain in guest memory and are
packed into the existing client-array draw records at draw time, which keeps
the PCI protocol asynchronous while preserving VBO semantics for array and
element buffers.

It is enough for toy fixed-pipeline demos such as a triangle or a rotating
colored cube. It is **not** enough for WineD3D or real games yet.

`glGet*` queries currently return values cached in the guest proxy plus
conservative defaults. They do not synchronously query browser/WebGL state.

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

## Trace one command from DLL to WebGL

The transport has correlated diagnostic logs at each hop. In an XP command
prompt, launch the test with tracing enabled:

```bat
set V86GL_TRACE_CALLS=1
gl_triangle_test.exe
```

Open/map/submit/present boundaries are logged by default; set
`V86GL_TRACE=0` only to suppress them. `V86GL_TRACE_CALLS` also logs every
encoded command record. The DLL writes to `OutputDebugString`; use
Sysinternals DebugView with **Capture Win32** enabled. The kernel driver
writes `DbgPrint` records; enable DebugView's **Capture Kernel** to see the
`[v86gl.sys]` lines.

Keep the browser developer console open. With `v86gl_pci.trace: true` (already
enabled in `app.js`), the resulting sequence for the same `frame` is:

```text
[v86gl.dll] submit -> sys frame=N commands=C commandBytes=B ...
[v86gl.sys] SUBMIT #K frame=N commands=C commandBytes=B ...
[v86gl.sys] doorbell #K ...
[v86gl-pci] descriptor accepted { frameId: N, ... }
[v86gl] pci frame received { frameId: N, submitCount: K, ... }
[v86gl] command { name: "gl...", ... }
[v86gl] gl4es dispatch gl...
```

The first missing line is the failed boundary: no `v86gl.sys` line means the
DLL could not enter the driver; no `v86gl-pci` doorbell means the PCI I/O BAR
was not reached; no `pci frame received` means event delivery failed; a
`gl4es export threw` or `export not found` message means the command reached
the browser bridge but failed at the gl4es layer.

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
- The fixed-pipeline matrix stack, depth test, shading mode, face-culling, blending/alpha/scissor state, basic 2D texture calls, and client arrays above are forwarded to gl4es, but there is still no clipping, lighting, display lists, compressed textures, multitexture, VBOs, or WineD3D compatibility.
- `glReadPixels` performs a synchronous PCI DMA readback. The host writes the result into the response area of the submitted guest-RAM command record before the IOCTL returns. It currently requires the browser-side gl4es renderer to be ready and supports the pixel formats accepted by the proxy's pack-state validation.
- `SwapBuffers` is exported by `gdi32.dll`, not `opengl32.dll`; normal apps that import `SwapBuffers` from `gdi32.dll` are not intercepted by this DLL. This toy bridge presents on `glFlush`, `glFinish`, `wglSwapLayerBuffers`, and the nonstandard helper export `wglSwapBuffers`.
- The driver exposes one shared buffer, so only one guest process can own the
  transport at a time.
