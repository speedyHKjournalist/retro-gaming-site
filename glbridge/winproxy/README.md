# Simple v86 OpenGL wrapper

This is a deliberately tiny fake `opengl32.dll` for experiments in v86.

It does **not** implement real OpenGL. It captures a very small subset of WGL/OpenGL calls and sends compact `VGL1` command packets as UDP broadcast datagrams. OpenGL calls are batched into one frame packet and flushed on `wglSwapLayerBuffers`/`wglSwapBuffers`. A browser-side JavaScript bridge receives the packets from v86 `net0-send` and forwards the frame to gl4es/WebGL.

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
- `glPixelStorei`
- `glTexEnvi`
- `glTexEnvf`
- `glTexCoord2f`

It is enough for toy fixed-pipeline demos such as a triangle or a rotating
colored cube. It is **not** enough for WineD3D or real games yet.

## Build the DLL

From Linux/macOS with mingw-w64:

```bash
cd src/glbridge/winproxy
i686-w64-mingw32-gcc -shared -Os -s \
  -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 \
  -o opengl32.dll opengl32_proxy.c opengl32.def \
  -Wl,--kill-at -luser32 -lkernel32 -lws2_32
```

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
```

These commands intentionally avoid the MinGW C runtime. Some modern MinGW-w64
packages link tiny programs against Universal CRT `api-ms-win-crt-*` DLLs, which
are not present in Windows XP/98 by default.

Copy both files into the same folder in the Windows XP guest:

```text
opengl32.dll
gl_triangle_test.exe
gl_rotate_cube_test.exe
```

Run `gl_triangle_test.exe` or `gl_rotate_cube_test.exe`.

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
emulator.add_listener("net0-send", ...)
```

It filters IPv4 UDP packets on port `46000` and renders the received command
stream into the WebGL canvas. The guest must have the v86 NE2K network device
available because the DLL uses WinSock UDP broadcast.

For a host-only smoke test, open:

```text
src/glbridge/sample/host_triangle_demo.html
```

That page uses a tiny emulator stub and feeds the same `VGL1` packets to
`v86_network_bridge.js`, without booting Windows XP.

## Important limitations

- UDP broadcast over `net0-send` is only for proof of concept. GL calls are batched per frame to reduce guest stalls, but this is still not a production transport for real games.
- The fixed-pipeline matrix stack, depth test, shading mode, face-culling, and basic 2D texture calls above are forwarded to gl4es, but there is still no clipping, lighting, display lists, client arrays, compressed textures, multitexture, or WineD3D compatibility.
- `SwapBuffers` is exported by `gdi32.dll`, not `opengl32.dll`; normal apps that import `SwapBuffers` from `gdi32.dll` are not intercepted by this DLL. This toy bridge presents on `glFlush`, `glFinish`, `wglSwapLayerBuffers`, and the nonstandard helper export `wglSwapBuffers`.
- For real performance, replace UDP broadcast with a v86 PCI/MMIO shared command ring or another zero-copy shared command transport.
