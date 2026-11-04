#ifndef D9WG_D3D9_CORE_H
#define D9WG_D3D9_CORE_H
#include <windows.h>
/* Deliberately forward declared: a D3D8 frontend can include this boundary
 * without mixing incompatible D3D8 and D3D9 SDK declarations. Its adapter
 * implementation uses the D3D9 SDK in a separate compilation unit. */
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSwapChain9;
struct IDirect3DSurface9;
struct IDirect3DTexture9;
struct D9WGProcess;
#define D9WG_D3D8_SHADER_TOKENS 8192u
#define D9WG_D3D8_CAPS_BYTES 212u
#define D9WG_D3D8_RASTER_ZBIAS 0x00004000u
#define D9WG_D3D8_IDENTIFIER_BYTES 1068u
#define D9WG_D3D8_PARAMETERS_BYTES 52u
#define D9WG_D3D8_SURFACE_DESC_BYTES 32u
#define D9WG_D3D8_NO_WHQL_LEVEL 2u
/* One attach/detach pair per embedding frontend module, called from that
 * module's loader entry. Release all owned COM objects before explicit
 * detach. The common runtime coordinates one owner with other frontends. */
BOOL WINAPI d9wg_d3d9_core_module(HINSTANCE instance, DWORD reason, LPVOID reserved);
struct IDirect3D9 *WINAPI d9wg_d3d9_core_create(UINT sdk_version);
/* Writes exactly the 53-word D3DCAPS8 prefix. Both SDK layouts are checked
 * when compiling their respective adapters. The caller supplies that object. */
void WINAPI d9wg_d3d9_core_caps8(void *caps8);
/* D3D8 owns the public wrapper; one private factory reference belongs to that
 * wrapper. Its module borrows this process attachment, never adds a second
 * runtime attachment, and delegates final flush/release to core_module. */
struct IDirect3D9 *WINAPI d9wg_d3d9_core_create8(void);
struct D9WGProcess *WINAPI d9wg_d3d9_core_process(void);
ULONG WINAPI d9wg_d3d9_core_root_release(struct IDirect3D9 *factory);
UINT WINAPI d9wg_d3d9_core_adapter_count(struct IDirect3D9 *factory);
HMONITOR WINAPI d9wg_d3d9_core_adapter_monitor(struct IDirect3D9 *factory, UINT adapter);
HRESULT WINAPI d9wg_d3d9_core_register_software(struct IDirect3D9 *factory, void *initialize);
HRESULT WINAPI d9wg_d3d9_core_adapter_identifier8(struct IDirect3D9 *factory,
        UINT adapter, DWORD flags, void *out, UINT bytes);
HRESULT WINAPI d9wg_d3d9_core_adapter_display_mode(struct IDirect3D9 *factory,
        UINT adapter, void *out);
/* Only private API8-profile factories accept UNKNOWN as the legacy flattened
 * list selector. This keeps ordinary D3D9 per-format enumeration unchanged. */
UINT WINAPI d9wg_d3d9_core_adapter_mode_count8(struct IDirect3D9 *factory, UINT adapter);
HRESULT WINAPI d9wg_d3d9_core_enum_adapter_modes8(struct IDirect3D9 *factory,
        UINT adapter, UINT index, void *out);
HRESULT WINAPI d9wg_d3d9_core_check_type(struct IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, UINT backbuffer, BOOL windowed);
HRESULT WINAPI d9wg_d3d9_core_check_multisample8(struct IDirect3D9 *factory, UINT adapter,
        UINT type, UINT format, BOOL windowed, UINT multisample);
HRESULT WINAPI d9wg_d3d9_core_check_depth(struct IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, UINT render, UINT depth);
HRESULT WINAPI d9wg_d3d9_core_check_format8(struct IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, DWORD usage, UINT resource_type, UINT format);
/* Internal non-null52-byte inputs/outputs; preserves API8 metadata, stages
 * in-place input, and computes effective storage through SDK9 layout. */
void WINAPI d9wg_d3d9_core_parameters_effective8(void *out8, const void *requested8,
        UINT width, UINT height, UINT format, HWND window);
/* Actual core9 device constructed with API8 initial-validation/default policy.
 * Other device/resource adapter entry points are migrated separately. */
HRESULT WINAPI d9wg_d3d9_core_create_device8(struct IDirect3D9 *factory, UINT adapter,
        UINT type, HWND focus, DWORD behavior, const void *parameters8,
        struct IDirect3DDevice9 **out);
/* Immutable52-byte API8 Reset input;NULL forwards to core Reset status rules. */
HRESULT WINAPI d9wg_d3d9_core_reset8(struct IDirect3DDevice9 *device,
        const void *parameters8);
/* Immutable52-byte API8 additional-chain construction/metadata bridge. */
HRESULT WINAPI d9wg_d3d9_core_additional8(struct IDirect3DDevice9 *device,
        const void *parameters8, struct IDirect3DSwapChain9 **out);
/* Private standalone API8 target; preserves the legacy4096 size ceiling,
 * six-format policy, single/4x sample policy and early failure output rules.
 * The returned surface is a real core9 object, owned by the embedding wrapper. */
HRESULT WINAPI d9wg_d3d9_core_render_target8(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT format, UINT multisample, BOOL lockable,
        struct IDirect3DSurface9 **out);
/* Legacy depth creation keeps its six-format/sample/output policy, then
 * uses the core8192 storage bound. Discard is false to preserve contents. */
HRESULT WINAPI d9wg_d3d9_core_depth_surface8(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT format, UINT multisample,
        struct IDirect3DSurface9 **out);
/* API8 2D creation policy (4096 bound, format/pool/mip/output rules).
 * This migrates creation and ordinary CPU texture storage; renderable texture
 * readback and complete public8 resource wrapping remain separate work. */
HRESULT WINAPI d9wg_d3d9_core_texture8(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT levels, DWORD usage, UINT format, UINT pool,
        struct IDirect3DTexture9 **out);
/* CPU-only SYSTEMMEM image with API8 linear formats (including8-only65). */
HRESULT WINAPI d9wg_d3d9_core_image_surface8(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT format, struct IDirect3DSurface9 **out);
/* Exact32-byte SDK8 descriptor, including legacy Size instead of9 quality.
 * Invalid size/profile/format/provider results leave caller bytes unchanged. */
HRESULT WINAPI d9wg_d3d9_core_surface_desc8(struct IDirect3DSurface9 *surface,
        void *out, UINT bytes);
/* Body starts after version token;success count excludes END. Version/handle
 * policy stays in the8 frontend. Failed calls leave body_count unchanged. */
BOOL WINAPI d9wg_d3d9_core_validate_shader_body8(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel, UINT minor, UINT *body_count);
#endif
