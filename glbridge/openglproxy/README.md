# OpenGL guest proxy

`opengl32.dll` is the Windows XP guest half of the OpenGL 1.1–2.1 WebGPU
path. It exports the system OpenGL/WGL ABI, encodes calls into the VGL2 record
format, and submits batches through `v86gl.sys`.

```text
game.exe
  -> opengl32.dll (this directory)
  -> v86gl.sys + v86 PCI DMA
  -> ../v86_network_bridge.js
  -> ../gl-webgpu/gl_executor.js
  -> WebGPU
```

There is no GL4ES or WebGL fallback. `v86gl.sys` remains a transport only; the
authoritative GL state machine and all rendering live in `../gl-webgpu/`.

## What the proxy does

- Exports the OpenGL 1.1 core ABI plus the OpenGL 1.2–2.1/extension entry
  points returned by `wglGetProcAddress`.
- Batches fixed-size and extended-size records in a 16 MiB mapped DMA arena.
- Keeps large texture, buffer, shader and direct-draw payloads inline with the
  record that owns them.
- Reserves response storage for synchronous state queries and asynchronous GPU
  readback.
- Tracks guest-visible generated object names and program/uniform/attribute
  locations so replay is deterministic.
- Exposes conservative extension profiles for OpenGL 1.5 WineD3D, ARB-program
  WineD3D and OpenGL 2.1 applications.

The protocol schema is `../gl_protocol.json`; constants shared with the host
are in `../gl-webgpu/gl_constants.js` and the decoder table is in
`../gl-webgpu/gl_wire.js`. `../tests/gl_protocol_consistency_test.js` checks
that these views stay identical.

## Synchronous calls

The host executes a submitted batch synchronously while the guest is blocked
inside `DeviceIoControl`. State-only queries such as `glGetError`,
`glGetIntegerv`, `glGetString`, shader/program status, uniform/attribute
location mapping, and framebuffer status write their answer into the command
record before the IOCTL returns.

`glReadPixels` and occlusion results require GPU completion. Their records
contain status/heartbeat fields and response storage in the shared DMA arena;
the host completes them after WebGPU mapping without allocating a second IPC
channel.

## Capability profiles

The default renderer strings are:

```text
GL_VENDOR   = Anthropic v86gl
GL_RENDERER = v86 WebGPU bridge
GL_VERSION  = 2.1 (v86gl/WebGPU)
GLSL        = 1.20
```

The WineD3D compatibility profile preserves the VMware/SVGA3D identity used
by legacy WineD3D capability selection, but names the backend
`SVGA3D; v86 WebGPU bridge`.

Optional formats are enabled only after the private host-capability query is
answered by the live WebGPU adapter. The complete entry-point classification
and known semantic deviations are documented in
[`../gl-webgpu/COVERAGE.md`](../gl-webgpu/COVERAGE.md) and
[`../gl-webgpu/README.md`](../gl-webgpu/README.md).

## Build

```bash
glbridge/openglproxy/build.sh              # opengl32.dll
glbridge/openglproxy/build_diagnostic.sh   # opengl32-diagnostic.dll
```

This is a 32-bit, CRT-free MinGW-w64 build; the script fails if a C runtime
import appears or if the export count drops below the 831-entry ABI. The
resulting DLL goes beside the guest game executable. Install and start the
matching `v86gl.sys` from `../v86gl_driver/` first.

## Diagnostic build

`opengl32-diagnostic.dll` keeps the same 831-entry ABI as the shipping DLL,
but adds the persistent trace used to diagnose guest-only failures. Copy it
beside the game and rename it to `opengl32.dll`. It writes
`opengl32_trace_<pid>.log` beside the DLL, falling back to `%TEMP%` when the
game directory is read-only.

The default log is bounded: it records process and WGL lifetime, surfaces,
frame/batch summaries, GL errors, transport/readback failures, shader and ARB
program failures, and an opcode histogram. Set `V86GL_TRACE_CALLS=1` in the
guest process only when a full per-record trace is required. Diagnostic code
is compiled out of the normal `opengl32.dll` built by `build.sh`.

## Host page

Load the WebGPU modules in dependency order, then the router:

```html
<canvas id="d3d_webgpu_canvas"></canvas>
<script src="glbridge/webgpu_host.js"></script>
<script src="glbridge/gl-webgpu/gl_constants.js"></script>
<script src="glbridge/gl-webgpu/gl_wire.js"></script>
<script src="glbridge/gl-webgpu/gl_state_layout.js"></script>
<script src="glbridge/gl-webgpu/gl_shader_translator.js"></script>
<script src="glbridge/gl-webgpu/gl_fixed_function.js"></script>
<script src="glbridge/gl-webgpu/gl_executor.js"></script>
<script src="glbridge/v86_network_bridge.js"></script>
```

```js
installV86GLNetworkBridge(
    emulator,
    document.getElementById("d3d_webgpu_canvas"),
    { graphicsCanvas: document.getElementById("d3d_webgpu_canvas") }
);
```

The same canvas may be shared with the D3D8 and D3D9 WebGPU executors because
a game directory drives only one graphics API backend at a time. D3D batches
use tagged envelopes; untagged VGL2 records always route to OpenGL WebGPU.

## Tests

From the repository root:

```bash
node glbridge/tests/gl_protocol_consistency_test.js
node glbridge/tests/cube2_gl_proc_coverage_test.js
node glbridge/tests/wined3d_caps_profile_test.js
node glbridge/tests/gl_executor_test.js
node glbridge/tests/v86_network_bridge_gl_route_test.js
```
