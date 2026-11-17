# Site acceptance records

- [Site-specific D3D acceptance](d3d/)
- [GTA SA fixed graphics publication](d3d/gtasa-buffer-map-ordering-20261117.json)
- [OpenGL guest-image deployment records](deployments/)
- [Shared D3D acceptance in v86](../../../v86/tests/acceptance/d3d/)

Deployment records retain the source revisions, payload and image hashes,
backup names and checks from their original runs. They are historical evidence,
not deployment scripts or runtime assets.

The former JSON records in `docs/` and the shared records in
`glbridge/deployments/` were byte-identical copies of records in v86. They now
have one canonical copy in the shared acceptance directory linked above. The
remaining site-specific records were moved here without changing their bytes;
embedded paths and hashes continue to describe the original checkout.

Keep fixed test inputs in `glbridge/tests/fixtures/`, Markdown explanations in
`docs/`, and large generated logs or private images in local build directories.
