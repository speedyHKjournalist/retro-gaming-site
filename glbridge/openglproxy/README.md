# OpenGL guest proxy

`opengl32.dll` is the Windows XP guest half of the desktop OpenGL/WGL
WebGPU path. It exports the 831-entry ABI, captures application inputs,
encodes VGL2 records and uses the matched `v86gl.sys` virtio transport.
The authoritative GL state, object names, errors, capability profile,
queries, lists, evaluators, selection/feedback and link interfaces belong
to v86's CPU `GLShadow`. The page executor renders accepted records and
returns explicitly asynchronous GPU results.

```text
game.exe -> app-local opengl32.dll -> v86gl.sys / virtio
         -> v86 CPU GLShadow -> browser GLWebGPUExecutor -> WebGPU
```

There is no GL4ES or WebGL fallback. The DLL still owns TLS, client-pointer
capture, response lifetimes, WGL/window bookkeeping and GDI font access;
it is a thin transport/API shim, not an empty forwarding DLL. Ordinary
GL getters have no guessed-limit or local-state fallback. CPU answers,
including `glGetError`, reach the caller before submission returns in
both local and CPU-worker modes. Failed queries preserve application
output. Explicit GPU-derived reads, fences, selection vertices and query
results use pending responses and bounded waits.

The single protocol source is
[`v86/gl_protocol.js`](../../../v86/src/browser/glbridge/gl-webgpu/gl_protocol.js).
It defines all 257 wire records, control records, payload layouts and
constants. Regenerate the C headers with:

```sh
node ../v86/tools/gen_gl_protocol_header.mjs glbridge/openglproxy
```

Run that from the site root. Generated headers have a size/offset assert
for every payload; guest-supplied internal resolved-state records are
rejected. The [behavior coverage table](../../../v86/src/browser/glbridge/gl-webgpu/COVERAGE.md)
links actual passing tests for each record. The [architecture](../../../v86/docs/glbridge.md),
[plan](../../../v86/docs/opengl-webgpu-plan.md) and
[acceptance record](../../../v86/docs/opengl-webgpu-acceptance.md) describe
the completed migration and its tested scope.

The mapped DMA arena is 16 MiB. Large inputs are bounded/chunked, and
synchronous answers retain their valid lifetime while the recursive API
guard is held. Asynchronous waits use a six-second host-silence deadline
renewed by the heartbeat; GPU waits are bounded to five seconds. The last
16 arena bytes hold the heartbeat and are excluded from command capacity.
Inline answers expire on the next submission. Actual C ABI fixtures cover
both transports, progress, silence, tick wrap and command boundaries.

Window visibility/occlusion uses `WINDOW_STATE` control records. WinEvent
callbacks register changes; a separate serialized dispatcher also polls
bound-window liveness and updates idle windows. No application window
procedure is replaced. Full GL checkpoints save CPU authority plus live
GPU resources, every texture mip/layer, depth/stencil/MSAA samples and the
last presented image separately from an unfinished back frame.

## Capability profiles

The default renderer strings are:

```text
GL_VENDOR   = VMware, Inc.
GL_RENDERER = SVGA3D; v86 WebGPU bridge
GL_VERSION  = 2.1 v86gl (WebGPU)
GLSL        = 1.20 v86gl
```

The WineD3D compatibility profile preserves the VMware/SVGA3D identity used
by legacy WineD3D capability selection, but names the backend
`SVGA3D; v86 WebGPU bridge`.

The CPU freezes adapter capabilities and limits in the context profile;
snapshots preserve that profile. Compatibility hints can hide capabilities,
never grant an unimplemented one. The entry-point classification
and known semantic deviations are documented in
[coverage table](../../../v86/src/browser/glbridge/gl-webgpu/COVERAGE.md) and
[retained differences](../../../v86/src/browser/glbridge/gl-webgpu/README.md).

## Build

```bash
glbridge/openglproxy/build.sh               # opengl32.dll
glbridge/openglproxy/build.sh --diagnostic  # opengl32-diagnostic.dll
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

## Host page and tests

Use the generated `vendor/v86/glbridge/libv86-webgpu.js` bundle with the
matching core, WASM, CPU worker, display plugin and shader resources.
The `V86` graphics proxy option installs its lifecycle/restore hooks; an
embedding should not manually load the retired site-side module paths.
Build, sync and embedding instructions are in the
[graphics README](../README.md) and [host architecture](../../../v86/docs/glbridge.md).

From the site root, run all guest ABI tests and core regression:

```sh
for test in glbridge/tests/*_test.js; do node "$test" || exit 1; done
make -C ../v86 test-glbridge
make -C ../v86 glbridge-gpu-tests
```

The standalone scripts are also directly runnable with `node`.
The application matrix and its required local inputs are documented in
the [acceptance record](../../../v86/docs/opengl-webgpu-acceptance.md).

### Thread and window ownership

Every exported GL/WGL entry takes a recursive call guard, including client
input encoding and synchronous response reads. Win32 TLS selects the calling
thread's context; switching threads saves and submits the old encoding state
before loading the new one. Mapping return storage is scoped to the share group.
WinEvent callbacks only mark changes; window procedures are never replaced,
leaving application Unicode/ANSI subclass chains intact. Bound-window liveness is
also polled, so destruction does not depend on a message pump. A separate dispatcher uses the same guard
for idle visibility/occlusion updates, retains the DLL while contexts exist and
exits after the final context is deleted. The host ABI fixtures separately
exercise real entry bodies, TLS/locking, all 831 preprocessed exports, callbacks
and dispatcher lifetime; their small-body shim is test-only.

### Window pixel formats

Display windows expose RGB24 plus alpha8, depth24/stencil8, with double,
4-sample double and single-buffer variants. The format is a window property:
unset returns zero, repeating the same selection succeeds, changing it fails.
Contexts require a selected compatible format. Bitmap/memory DCs are explicitly
unsupported and are never converted to a screen overlay. ARB selection handles
integer and floating constraints, and queries validate all fields before writing.
The actual-C format fixture and its 12 mutants cover these rules; an XP worker
Cube 2 complex run also checks the current DLL and GLSL probe.

### Window swaps

SwapBuffers validates and presents the supplied DC, independently of the current
rendering context. Single-buffer Swap is a no-op; Flush/Finish presents front
writes. Swap interval is window state, defaults to 1 and clamps to 4. Negative
values and calls without a current DC fail. The host replies after the requested
refresh callbacks, bounding queued work; interval 0 removes that wait, while the
browser compositor retains control of physical presentation. Expired arena,
device-loss and timeout callbacks cannot present later. The real-GPU test covers
two windows, no-current swaps and cold restore in both transports; XP/Cube 2 also
passes with the matching guest and host protocol.

WGL sharing migrates every member of an unused target group. Once any target
member has been current, namespace merging is explicitly refused; already
shared contexts remain accepted. Nonzero wglCopyContext masks return
ERROR_CALL_NOT_IMPLEMENTED instead of false success. The context-operations
fixture covers these limits, format compatibility and unadvertised layer refusal.

### Recovering transport failures

A failed open/map can be attempted again. A failed submit keeps the process's
mapping and previously accepted objects; the driver resets its queue and can
register the same arena again. The failed batch is discarded, never replayed
when acceptance is uncertain. The next API entry resends the current surface.
There is no permanent disabled flag. Error messages use OutputDebugString only
when tracing was explicitly requested; diagnostic builds retain file logging.

### Truthful query and font results

Texture residency queries validate names on the CPU. This implementation keeps
all live textures resident; aggregate true leaves the caller's array untouched.
Invalid/deleted/zero names and Begin/End queries report GL errors. Bitmap fonts
use matching ANSI/Unicode GDI calls, preserve unpack state and PBO binding, and
compile actual glyph bits with their origins and both advances. Undefined glyphs
create empty lists. GDI/allocation errors fail without placeholder geometry.
Outline fonts remain explicitly unsupported. RGBA formats have no layer palette,
so palette calls fail rather than returning fabricated entries or success.
The XP probe compares a Unicode glyph against GDI pixels and checks application
window chains before allowing Cube 2 to launch.

### F6 installed images

`../deployments/opengl-webgpu-F6.json` records the exact source, shipping and
installed-image hashes plus original-image backup names. XP, Cube 2 and Warcraft
III local images contain the current driver/proxy. The XP system OpenGL files
are protected and remain unchanged. The old and new Cube DLLs are both 145408
bytes: use hashes and the XP GL_VERSION/probe result, never file size alone.
Local branch assets and images are prepared; no remote publication is implied.
