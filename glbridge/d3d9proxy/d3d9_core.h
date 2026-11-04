#ifndef D9WG_D3D9_CORE_H
#define D9WG_D3D9_CORE_H
#include <windows.h>
/* Deliberately forward declared: a D3D8 frontend can include this boundary
 * without mixing incompatible D3D8 and D3D9 SDK declarations. Its adapter
 * implementation uses the D3D9 SDK in a separate compilation unit. */
struct IUnknown;
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSwapChain9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DStateBlock9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DSurface9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DCubeTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DVolume9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct D9WGProcess;
#define D9WG_D3D8_SHADER_TOKENS 8192u
#define D9WG_D3D8_CAPS_BYTES 212u
#define D9WG_D3D8_RASTER_ZBIAS 0x00004000u
#define D9WG_D3D8_IDENTIFIER_BYTES 1068u
#define D9WG_D3D8_PARAMETERS_BYTES 52u
#define D9WG_D3D8_SURFACE_DESC_BYTES 32u
#define D9WG_D3D8_VOLUME_DESC_BYTES 32u
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
/* API8 front readback keeps its larger-target/exterior-preservation policy,
 * format rules and texture-view uploads; ordinary9 front rules stay separate. */
HRESULT WINAPI d9wg_d3d9_core_get_front_buffer8(struct IDirect3DDevice9 *device,
        struct IDirect3DSurface9 *destination);
/* Ordered API8 rectangles with legacy CPU/GPU pool and overlap rules. */
HRESULT WINAPI d9wg_d3d9_core_copy_rects8(struct IDirect3DDevice9 *device,
        struct IDirect3DSurface9 *source, const RECT *rects, UINT count,
        struct IDirect3DSurface9 *destination, const POINT *points);
/* Core8 textures use the same native SYSTEMMEM -> DEFAULT copy policy and
 * actual core9 mip/face/volume preflight, shadow updates and uploads. */
HRESULT WINAPI d9wg_d3d9_core_update_texture8(struct IDirect3DDevice9 *device,
        struct IDirect3DBaseTexture9 *source, struct IDirect3DBaseTexture9 *destination);
/* Backend-state translation only: caller owns raw8 Get/state-block metadata.
 * Deleted states have no backend effect; ZBIAS and sampler TSS are mapped. */
HRESULT WINAPI d9wg_d3d9_core_set_render_state8(struct IDirect3DDevice9 *device,
        UINT state, DWORD value);
HRESULT WINAPI d9wg_d3d9_core_set_texture_stage_state8(struct IDirect3DDevice9 *device,
        UINT stage, UINT state, DWORD value);
/* API8 buffer creation with core lifetime/lock/descriptor implementation. */
HRESULT WINAPI d9wg_d3d9_core_vertex_buffer8(struct IDirect3DDevice9 *device,
        UINT length, DWORD usage, DWORD fvf, UINT pool,
        struct IDirect3DVertexBuffer9 **out);
HRESULT WINAPI d9wg_d3d9_core_index_buffer8(struct IDirect3DDevice9 *device,
        UINT length, DWORD usage, UINT format, UINT pool,
        struct IDirect3DIndexBuffer9 **out);
/* API8 3D creation policy;32-byte volume descriptor adds legacy Size. */
HRESULT WINAPI d9wg_d3d9_core_volume_texture8(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        UINT format, UINT pool, struct IDirect3DVolumeTexture9 **out);
HRESULT WINAPI d9wg_d3d9_core_volume_desc8(struct IDirect3DVolume9 *volume,
        void *out, UINT bytes);
/* API8 cube creation: six face-major mip chains,4096/legacy policy. */
HRESULT WINAPI d9wg_d3d9_core_cube_texture8(struct IDirect3DDevice9 *device,
        UINT edge, UINT levels, DWORD usage, UINT format, UINT pool,
        struct IDirect3DCubeTexture9 **out);
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
/* API8 token declaration -> actual private core9 layout object. Raw tokens
 * and shader-local constant metadata remain owned by the8 handle adapter. */
HRESULT WINAPI d9wg_d3d9_core_vertex_declaration8(struct IDirect3DDevice9 *device,
        const DWORD *tokens, BOOL declaration_only,
        struct IDirect3DVertexDeclaration9 **out);
/* Backend8 FVF binding uses the8 dialect and clears the VS;zero clears the
 * layout too. Raw8 GetVertexShader/FVF-vs-handle state stays in the frontend.
 * Internal9 GetFVF iszero because the actual converted layout is authoritative. */
HRESULT WINAPI d9wg_d3d9_core_set_fvf8(struct IDirect3DDevice9 *device, DWORD fvf);
/* Process into a real9 VB whose raw FVF belongs to8. Source bindings and
 * math use the shared core pipeline;destination8 masks stay versioned. */
HRESULT WINAPI d9wg_d3d9_core_process_vertices8(struct IDirect3DDevice9 *device,
        UINT src_start, UINT dst_index, UINT count,
        struct IDirect3DVertexBuffer9 *destination, DWORD flags);
/* Frontend8 supplies its selected raw snapshot through recordable translated
 * setters on this device. The callback/context are used only during this call;
 * the published real9 block retains values/objects, never frontend pointers.
 * Raw8 masks/handles/Get metadata and Capture rebuild policy stay frontend-owned. */
typedef HRESULT (WINAPI *D9WGStateBlockRecorder8)(struct IDirect3DDevice9 *device,
        const void *snapshot);
HRESULT WINAPI d9wg_d3d9_core_record_state_block8(struct IDirect3DDevice9 *device,
        D9WGStateBlockRecorder8 recorder, const void *snapshot,
        struct IDirect3DStateBlock9 **out);
/* One weak frontend8 view per real resource storage identity. The caller
 * holds a resource reference across get/attach. A missing view returns S_OK
 * and NULL. Successful attach owns the metadata until the retirement callback
 * runs exactly once,after resource private-data finalizers. Duplicate attach
 * leaves the existing view intact;retiring objects reject get/attach.
 * The callback releases only frontend metadata,not the dying core object. */
typedef void (WINAPI *D9WGCoreViewRetire8)(void *view);
HRESULT WINAPI d9wg_d3d9_core_get_view8(struct IUnknown *object, void **out);
HRESULT WINAPI d9wg_d3d9_core_attach_view8(struct IUnknown *object, void *view,
        D9WGCoreViewRetire8 retire);
/* Device-owned DWORD state blocks: no public9 block/device parent pin.
 * Raw8 scopes and Get metadata remain frontend-owned. Capture rebuilds the
 * mapped snapshot behind the same token; failed rebuild preserves it. */
/* Raw8 Begin/End masks stay in the frontend. Mapped setters record in the
 * actual9 recorder; successful End publishes a device-owned DWORD token. */
HRESULT WINAPI d9wg_d3d9_core_begin_state_block8(struct IDirect3DDevice9 *device);
HRESULT WINAPI d9wg_d3d9_core_end_state_block_handle8(struct IDirect3DDevice9 *device,
        DWORD *out);
HRESULT WINAPI d9wg_d3d9_core_create_state_block_handle8(struct IDirect3DDevice9 *device,
        D9WGStateBlockRecorder8 recorder, const void *snapshot, DWORD *out);
HRESULT WINAPI d9wg_d3d9_core_capture_state_block_handle8(struct IDirect3DDevice9 *device,
        DWORD token, D9WGStateBlockRecorder8 recorder, const void *snapshot);
HRESULT WINAPI d9wg_d3d9_core_apply_state_block_handle8(struct IDirect3DDevice9 *device,
        DWORD token);
HRESULT WINAPI d9wg_d3d9_core_delete_state_block_handle8(struct IDirect3DDevice9 *device,
        DWORD token);

/* Raw8 BaseVertexIndex stays with the frontend index binding/Get shadow.
 * This backend entry preserves its bits in core9 per-draw signed storage. */
HRESULT WINAPI d9wg_d3d9_core_draw_indexed8(struct IDirect3DDevice9 *device,
        UINT primitive_type, UINT base_vertex_index,
        UINT min_vertex_index, UINT vertex_count, UINT start_index,
        UINT primitive_count);
/* Bind a resolved8 declaration/VS pair (NULL VS permits declaration-only).
 * Both owners are checked before setters run;NULL/NULL clears8 bindings.
 * Frontend8 owns DWORD handle publication/deletion and raw Get shadows. */
HRESULT WINAPI d9wg_d3d9_core_set_vertex_shader8(struct IDirect3DDevice9 *device,
        struct IDirect3DVertexDeclaration9 *layout,
        struct IDirect3DVertexShader9 *shader);
/* Real core8 shader objects. VS pairs8 declaration with its local DEFs;
 * frontend retains original8 function/declaration bytes for public queries. */
HRESULT WINAPI d9wg_d3d9_core_vertex_shader8(struct IDirect3DDevice9 *device,
        const DWORD *declaration, const DWORD *function,
        struct IDirect3DVertexDeclaration9 **layout,
        struct IDirect3DVertexShader9 **shader);
HRESULT WINAPI d9wg_d3d9_core_pixel_shader8(struct IDirect3DDevice9 *device,
        const DWORD *function, struct IDirect3DPixelShader9 **shader);
/* Body starts after version token;success count excludes END. Version/handle
 * policy stays in the8 frontend. Failed calls leave body_count unchanged. */
BOOL WINAPI d9wg_d3d9_core_validate_shader_body8(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel, UINT minor, UINT *body_count);
#endif
