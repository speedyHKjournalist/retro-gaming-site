# Retired Device8 reference

`d3d8_legacy_proxy.c` is the exact source of `glbridge/d3d8proxy/d3d8_proxy.c`
at site commit `26eb8f7949189e1402666155b1187c9f1eb90d60`.
SHA-256: `547b478204b9c175456b9fae6d79780e4f8323ba89fe5fca77228f633689cda2`.

This file is **not a shipping backend**. No DLL build links it. Historical
native comparison harnesses extract functions from it through the explicitly
named `legacy8_comparison.js` reader. The reader verifies the frozen source hash;
other paths, including mutation candidates and shared production headers, are
read unchanged. A passing historical comparison does not establish that the
new Device8 frontend works.

Production coverage uses the Root8 factory/caps/adapter/modes/format tests,
`d8wg_*_adapter_*`, `d8wg_device_frontend_*`, `d8wg_device_common_state_*`,
and the shared core tests. The SDK XP probe exercises all 97 methods through
both the private factory and, with `D8WG_PUBLIC_DEVICE=1`, the public
`Direct3DCreate8` / `IDirect3D8::CreateDevice` entry. Frozen commands from that
public run are checked independently and replayed on SwiftShader and Metal.

Keep this reference immutable. Fixes belong in the shared core or API8
adapters, with production tests. The caps test deliberately retains the old
Device8 query as a reference while testing the live Root8 conversion.
