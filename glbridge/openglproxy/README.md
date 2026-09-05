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
`opengl32_trace_<pid>.log` beside the game executable, then beside the DLL,
falling back to `%TEMP%` when both directories are read-only. A file left under
the name `opengl32-diagnostic.dll` is not selected by the Windows OpenGL loader.
For launcher-based packages, "beside the game" means beside the child process
that imports OpenGL, not beside the launcher. In SauerbratenPortable that is
`App\Sauerbraten\bin\sauerbraten.exe`, so the diagnostic must replace
`App\Sauerbraten\bin\opengl32.dll` while the game is stopped.

**No guest environment variable is required.** The diagnostic DLL traces as
soon as it is loaded. A guest game is usually started from a `.bat` behind a
portable launcher, and the v86 disk is a cold-booted in-memory overlay, so a
variable is both awkward to set and lost on the next run.

The log records process and WGL lifetime, surfaces, frame/batch summaries, GL
errors, transport/readback failures, shader and ARB program failures, an
opcode histogram, and per-call detail. Per-call detail is capped at
`V86GL_TRACE_CALL_BUDGET` lines per frame (512 by default) because an
immediate-mode frame emits one record per vertex, and guest writes land in
that same in-memory overlay, so a runaway log is charged to browser RAM. A
frame that hit the cap writes a `CALLS suppressed=` line rather than dropping
detail silently.

| Guest setting | Effect |
| --- | --- |
| nothing set | summary plus per-call detail, budgeted per frame |
| `V86GL_TRACE_CALL_BUDGET=N` | change the per-frame budget; `0` is uncapped |
| `V86GL_TRACE_CALLS=1` | uncapped per-record firehose |
| `V86GL_TRACE_CALLS=0` | summary only, no per-call detail |
| `V86GL_TRACE=0` | summary only; also drops the transport and lifetime lines |
| `V86GL_TRACE_FLUSH_MS=N` | diagnostic file checkpoint interval, 0–60000 ms; default 1000; `0` restores a durable flush every frame |

The process, frame, error and histogram summary is written directly and is not
affected by any of these settings; the table controls the transport, lifetime
and per-call lines layered on top of it.

`V86GL_TRACE`/`V86GL_TRACE_CALLS` additionally enable the `OutputDebugString`
path, which is left opt-in: with no debugger attached it raises a first-chance
exception per line, which costs far more than the file write.

Diagnostic code is compiled out of the normal `opengl32.dll` built by
`build.sh`, which still traces nothing unless one of those variables is set.

The OpenGL diagnostic writer batches lines in a 64 KiB buffer. Full buffers
are written without a disk flush; frame checkpoints flush about once per
second by default. Attach, normal exit, reported errors and caught exceptions
force a flush (exception handling is best-effort if another thread owns the
writer lock). `TRACE_IO buffer_bytes=65536 flush_ms=1000` in the log confirms
the new build and its effective setting. Abrupt process termination, a browser
crash, or a stalled application can lose the unflushed tail; use
`V86GL_TRACE_FLUSH_MS=0` when investigating such failures. This does not change
the legacy DirectX diagnostic writer's default behavior.

For performance comparisons, run the **normal** `opengl32.dll` first using
the same save, route, resolution and video settings. Then compare the diagnostic
build (optionally `V86GL_TRACE_CALLS=0`). Summary-only diagnostics still format
and write log records, so they are not equivalent to the normal build. Compare
visible frame intervals and long-task durations, not just the in-game FPS HUD.

### Where the log lands

The trace goes beside the **executable**, not beside the game's top-level
folder. Sauerbraten's `sauerbraten.bat` starts `bin\sauerbraten.exe`, so the
log appears in `...\App\Sauerbraten\bin\`. The proxy falls back to the DLL
directory, then `%TEMP%`, when the executable directory is read-only.

The file exists only inside the running VM. v86 mounts the game image with
`async: true`, so guest writes stay in an in-memory block cache and never
reach the `.img` on the host. Read the log inside the guest, or copy it out
with the file-transfer tool, before the page reloads.

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
node glbridge/tests/gl_proxy_diagnostic_trace_test.js
node glbridge/tests/gl_diagnostic_buffer_test.js
```
