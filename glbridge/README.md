# Guest graphics proxies

The host JavaScript implementations now live in the sibling v86 repository,
under `src/browser/glbridge/`. DLL/SYS sources, binaries, guest samples and
protocol/ABI tests remain here. Host and guest use matching headers generated
from the core's protocol schema. The transport uses custom virtio and
requires the updated driver; see [driver build and deployment](v86gl_driver/README.md).

After changing v86 or its graphics runtime, build and refresh the site:

```sh
make -C ../v86 build/libv86.js build/v86.wasm build/cpu-worker.js build/v86-bochs-vga.js glbridge
node utils/sync-v86.mjs ../v86
```

Run these commands from the retro-gaming-site root. The sync tool copies the
v86 core (`libv86.js`, `v86.wasm`, the CPU worker `cpu-worker.js` and the
Bochs VGA display plugin `v86-bochs-vga.js`, all to the site root) and the
generated graphics bundle/worker resources, and updates the page's
cache-busting versions: the four core files share one revision, which
`app.js` reads from the `libv86.js` tag and puts on the other three. `vendor/v86/` contains generated assets:
commit them with the core files (the site is deployed from Git, and without
them a game fails with "graphics_proxy requires libv86-webgpu.js").

Host tests now run with `make -C ../v86 test-glbridge`. Guest tests remain
`node glbridge/tests/<name>_test.js`; cross-repository tests default to the
sibling v86 checkout or use the `V86_ROOT` environment variable. Use matched
v86/Guest commits when publishing a release.

The OpenGL C–G migration and application acceptance are complete on `refactor`.
The [acceptance record](../../v86/docs/opengl-webgpu-acceptance.md) covers five
programs in both CPU modes, exact full-VM cold restore, continued frames,
process switching and paired F6/candidate measurements. It records the two
GLView shader-resource corrections, the separate Tremulous flare-map fixture
and the non-repeatable first worker timing difference. Guest/image hashes
remain in the [F6 deployment manifest](deployments/opengl-webgpu-F6.json);
the final host/matrix versions are in the [G manifest](deployments/opengl-webgpu-G.json).
These are local branch assets and local installed images.

The page runs the CPU in v86's worker (`cpu_worker: true`), selects the display
hardware with `graphics_adapter: "bochs_vga"`, turns the proxy on with
`graphics_proxy: true`, and keeps `window.v86gl = emulator.graphics_proxy` for
diagnostics. VGA alignment,
graphics lifecycle, and graphics state hooks are owned by v86. See
`../v86/docs/glbridge.md` (from the site root) for embedding details and the
existing D9WG snapshot limitation.
