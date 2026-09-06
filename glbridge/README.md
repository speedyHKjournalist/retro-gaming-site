# Guest graphics proxies

The host JavaScript implementations now live in the sibling v86 repository,
under `src/browser/glbridge/`. DLL/SYS sources, binaries, guest samples and
protocol/ABI tests remain here. The renderer protocol is unchanged. The transport now uses custom virtio and
requires the updated driver; see [driver build and deployment](v86gl_driver/README.md).

After changing v86 or its graphics runtime, build and refresh the site:

```sh
make -C ../v86 build/libv86.js glbridge
node utils/sync-v86.mjs ../v86
```

Run these commands from the retro-gaming-site root. The sync tool copies the
v86 JS/WASM and the generated graphics bundle/worker resources, and updates
the page's cache-busting versions. `vendor/v86/` contains generated assets
and is ignored by Git: run the sync step before serving or packaging a fresh
checkout. Include the generated directory in the deployed static site.

Host tests now run with `make -C ../v86 test-glbridge`. Guest tests remain
`node glbridge/tests/<name>_test.js`; cross-repository tests default to the
sibling v86 checkout or use the `V86_ROOT` environment variable. Use matched
v86/Guest commits when publishing a release.

The page passes `installV86GLGraphicsAdapter` to v86 and keeps
`window.v86gl = emulator.graphics_adapter` for diagnostics. VGA alignment,
graphics lifecycle, and graphics state hooks are owned by v86. See
`../v86/docs/glbridge.md` (from the site root) for embedding details and the
existing D9WG snapshot limitation.
