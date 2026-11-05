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
/* Balanced frontend raw8-handle storage holds for actual declaration/VS/PS.
 * These calls borrow their inputs and do not change public COM/device refs.
 * After retaining storage, the frontend drops constructor COM refs separately.
 * Keep the device/storage alive during each call; release all owned holds in
 * the device metadata retire callback, while its core device/factory are valid.
 * A release requires a private hold; other owners' binding refs are not owned. */
HRESULT WINAPI d9wg_d3d9_core_shader_storage_add_ref8(struct IDirect3DDevice9 *device,
        struct IUnknown *object);
HRESULT WINAPI d9wg_d3d9_core_shader_storage_release8(struct IDirect3DDevice9 *device,
        struct IUnknown *object);
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
typedef struct D9WGSwapChainMethods8 {
    HRESULT (WINAPI *present)(struct IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
    HRESULT (WINAPI *get_back_buffer)(struct IDirect3DSwapChain9 *, UINT, UINT, struct IDirect3DSurface9 **);
} D9WGSwapChainMethods8;
/* Only additional chains with real8-profile storage are accepted. */
const D9WGSwapChainMethods8 *WINAPI d9wg_d3d9_core_swap_chain_methods8(
        struct IDirect3DSwapChain9 *chain);
typedef struct D9WGBufferMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IUnknown *, const GUID *, const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IUnknown *, const GUID *, void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IUnknown *, const GUID *);
    DWORD (WINAPI *set_priority)(struct IUnknown *, DWORD);
    DWORD (WINAPI *get_priority)(struct IUnknown *);
    void (WINAPI *preload)(struct IUnknown *);
    UINT (WINAPI *get_type)(struct IUnknown *);
    HRESULT (WINAPI *lock)(struct IUnknown *, UINT, UINT, void *, DWORD);
    HRESULT (WINAPI *unlock)(struct IUnknown *);
    HRESULT (WINAPI *get_desc)(struct IUnknown *, void *, UINT);
} D9WGBufferMethods8;
/* expected_type is SDK resource type6 (vertex) or7 (index). */
const D9WGBufferMethods8 *WINAPI d9wg_d3d9_core_buffer_methods8(
        struct IUnknown *buffer, UINT expected_type);
typedef struct D9WGTextureMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IDirect3DTexture9 *, const GUID *, const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IDirect3DTexture9 *, const GUID *, void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IDirect3DTexture9 *, const GUID *);
    DWORD (WINAPI *set_priority)(struct IDirect3DTexture9 *, DWORD);
    DWORD (WINAPI *get_priority)(struct IDirect3DTexture9 *);
    void (WINAPI *preload)(struct IDirect3DTexture9 *);
    UINT (WINAPI *get_type)(struct IDirect3DTexture9 *);
    DWORD (WINAPI *set_lod)(struct IDirect3DTexture9 *, DWORD);
    DWORD (WINAPI *get_lod)(struct IDirect3DTexture9 *);
    DWORD (WINAPI *get_level_count)(struct IDirect3DTexture9 *);
    HRESULT (WINAPI *get_level_desc)(struct IDirect3DTexture9 *, UINT, void *, UINT);
    HRESULT (WINAPI *get_surface_level)(struct IDirect3DTexture9 *, UINT, struct IDirect3DSurface9 **);
    HRESULT (WINAPI *lock_rect)(struct IDirect3DTexture9 *, UINT, void *, const RECT *, DWORD);
    HRESULT (WINAPI *unlock_rect)(struct IDirect3DTexture9 *, UINT);
    HRESULT (WINAPI *add_dirty_rect)(struct IDirect3DTexture9 *, const RECT *);
} D9WGTextureMethods8;
const D9WGTextureMethods8 *WINAPI d9wg_d3d9_core_texture_methods8(struct IDirect3DTexture9 *texture);
typedef struct D9WGCubeTextureMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IDirect3DCubeTexture9 *, const GUID *, const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IDirect3DCubeTexture9 *, const GUID *, void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IDirect3DCubeTexture9 *, const GUID *);
    DWORD (WINAPI *set_priority)(struct IDirect3DCubeTexture9 *, DWORD);
    DWORD (WINAPI *get_priority)(struct IDirect3DCubeTexture9 *);
    void (WINAPI *preload)(struct IDirect3DCubeTexture9 *);
    UINT (WINAPI *get_type)(struct IDirect3DCubeTexture9 *);
    DWORD (WINAPI *set_lod)(struct IDirect3DCubeTexture9 *, DWORD);
    DWORD (WINAPI *get_lod)(struct IDirect3DCubeTexture9 *);
    DWORD (WINAPI *get_level_count)(struct IDirect3DCubeTexture9 *);
    HRESULT (WINAPI *get_level_desc)(struct IDirect3DCubeTexture9 *, UINT, void *, UINT);
    HRESULT (WINAPI *get_cube_map_surface)(struct IDirect3DCubeTexture9 *, UINT, UINT, struct IDirect3DSurface9 **);
    HRESULT (WINAPI *lock_rect)(struct IDirect3DCubeTexture9 *, UINT, UINT, void *, const RECT *, DWORD);
    HRESULT (WINAPI *unlock_rect)(struct IDirect3DCubeTexture9 *, UINT, UINT);
    HRESULT (WINAPI *add_dirty_rect)(struct IDirect3DCubeTexture9 *, UINT, const RECT *);
} D9WGCubeTextureMethods8;
const D9WGCubeTextureMethods8 *WINAPI d9wg_d3d9_core_cube_texture_methods8(struct IDirect3DCubeTexture9 *texture);
typedef struct D9WGVolumeTextureMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IDirect3DVolumeTexture9 *, const GUID *, const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IDirect3DVolumeTexture9 *, const GUID *, void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IDirect3DVolumeTexture9 *, const GUID *);
    DWORD (WINAPI *set_priority)(struct IDirect3DVolumeTexture9 *, DWORD);
    DWORD (WINAPI *get_priority)(struct IDirect3DVolumeTexture9 *);
    void (WINAPI *preload)(struct IDirect3DVolumeTexture9 *);
    UINT (WINAPI *get_type)(struct IDirect3DVolumeTexture9 *);
    DWORD (WINAPI *set_lod)(struct IDirect3DVolumeTexture9 *, DWORD);
    DWORD (WINAPI *get_lod)(struct IDirect3DVolumeTexture9 *);
    DWORD (WINAPI *get_level_count)(struct IDirect3DVolumeTexture9 *);
    HRESULT (WINAPI *get_level_desc)(struct IDirect3DVolumeTexture9 *, UINT, void *, UINT);
    HRESULT (WINAPI *get_volume_level)(struct IDirect3DVolumeTexture9 *, UINT, struct IDirect3DVolume9 **);
    HRESULT (WINAPI *lock_box)(struct IDirect3DVolumeTexture9 *, UINT, void *, const void *, DWORD);
    HRESULT (WINAPI *unlock_box)(struct IDirect3DVolumeTexture9 *, UINT);
    HRESULT (WINAPI *add_dirty_box)(struct IDirect3DVolumeTexture9 *, const void *);
} D9WGVolumeTextureMethods8;
const D9WGVolumeTextureMethods8 *WINAPI d9wg_d3d9_core_volume_texture_methods8(struct IDirect3DVolumeTexture9 *texture);
typedef struct D9WGVolumeMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IDirect3DVolume9 *, const GUID *,
            const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IDirect3DVolume9 *, const GUID *,
            void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IDirect3DVolume9 *, const GUID *);
    HRESULT (WINAPI *get_desc)(struct IDirect3DVolume9 *, void *, UINT);
    HRESULT (WINAPI *lock_box)(struct IDirect3DVolume9 *, void *, const void *, DWORD);
    HRESULT (WINAPI *unlock_box)(struct IDirect3DVolume9 *);
} D9WGVolumeMethods8;
const D9WGVolumeMethods8 *WINAPI d9wg_d3d9_core_volume_methods8(
        struct IDirect3DVolume9 *surface);
/* The SDK8 adapter uses this typed boundary without including SDK9 headers.
 * Its resource pointer must already belong to this8-profile core. */
typedef struct D9WGSurfaceMethods8 {
    HRESULT (WINAPI *set_private_data)(struct IDirect3DSurface9 *, const GUID *,
            const void *, DWORD, DWORD);
    HRESULT (WINAPI *get_private_data)(struct IDirect3DSurface9 *, const GUID *,
            void *, DWORD *);
    HRESULT (WINAPI *free_private_data)(struct IDirect3DSurface9 *, const GUID *);
    HRESULT (WINAPI *get_desc)(struct IDirect3DSurface9 *, void *, UINT);
    HRESULT (WINAPI *lock_rect)(struct IDirect3DSurface9 *, void *, const RECT *, DWORD);
    HRESULT (WINAPI *unlock_rect)(struct IDirect3DSurface9 *);
} D9WGSurfaceMethods8;
const D9WGSurfaceMethods8 *WINAPI d9wg_d3d9_core_surface_methods8(
        struct IDirect3DSurface9 *surface);
/* One weak frontend8 view per real resource storage identity. The caller
 * holds a resource reference across get/attach. A missing view returns S_OK
 * and NULL. Successful attach owns the metadata until the retirement callback
 * runs exactly once,after resource private-data finalizers. Duplicate attach
 * leaves the existing view intact;retiring objects reject get/attach.
 * The callback releases only frontend metadata,not the dying core object. */
/* Device9 objects with compatibility profile8 may also carry weak frontend
 * metadata. Device retirement follows all implicit surface retire callbacks;
 * private-data finalizers may revive the device before destruction commits.
 * A retire callback only tears metadata down;it must not revive dying storage. */
typedef void (WINAPI *D9WGCoreViewRetire8)(void *view);
HRESULT WINAPI d9wg_d3d9_core_get_view8(struct IUnknown *object, void **out);
HRESULT WINAPI d9wg_d3d9_core_attach_view8(struct IUnknown *object, void *view,
        D9WGCoreViewRetire8 retire);
/* Optional frontend CPU metadata cleanup runs before native recording,
 * owned blocks and live bindings are cleared, outside the transport lock and
 * under the core temporary device pin. It must be idempotent: an explicit
 * cleanup followed by final Release can notify it again. Retirement remains
 * the final teardown callback; these observers never own public references. */
typedef void (WINAPI *D9WGCoreDeviceCleanup8)(void *view);
HRESULT WINAPI d9wg_d3d9_core_attach_device_view8(struct IDirect3DDevice9 *device,
        void *view, D9WGCoreDeviceCleanup8 cleanup, D9WGCoreViewRetire8 retire);
/* Call while the frontend retains device/storage. Rejects nested cleanup;
 * callbacks may remove the last other pin, so the device may retire here. */
HRESULT WINAPI d9wg_d3d9_core_release_owned8(struct IDirect3DDevice9 *device);
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

/* Private backend dispatch for the Device8 facade. SDK8 struct bytes stay
 * opaque here; only the SDK9 implementation casts to concrete SDK layouts.
 * Extra9 swap/target indices and stream offsets are fixed to8 defaults.
 * Raw8 handles/state scopes and paired-target policy stay with their adapters.
 * Query the table once for a genuine8-profile device,then retain that device
 * across calls. No public COM/device references are owned by this table. */
typedef struct D9WGDeviceMethods8 {
    HRESULT (WINAPI *test_cooperative_level)(struct IDirect3DDevice9*);
    UINT (WINAPI *get_available_texture_mem)(struct IDirect3DDevice9 *);
    HRESULT (WINAPI *resource_manager_discard_bytes)(struct IDirect3DDevice9 *, DWORD);
    HRESULT (WINAPI *get_display_mode)(struct IDirect3DDevice9*, void *);
    HRESULT (WINAPI *get_creation_parameters)(struct IDirect3DDevice9 *, void *);
    HRESULT (WINAPI *set_cursor_properties)(struct IDirect3DDevice9 *, UINT, UINT, struct IDirect3DSurface9 *);
    void (WINAPI *set_cursor_position)(struct IDirect3DDevice9 *, int, int, DWORD);
    BOOL (WINAPI *show_cursor)(struct IDirect3DDevice9 *, BOOL);
    HRESULT (WINAPI *present)(struct IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    HRESULT (WINAPI *get_back_buffer)(struct IDirect3DDevice9 *, UINT, UINT, struct IDirect3DSurface9 **);
    HRESULT (WINAPI *get_raster_status)(struct IDirect3DDevice9 *, void *);
    void (WINAPI *set_gamma_ramp)(struct IDirect3DDevice9 *, DWORD, const void *);
    void (WINAPI *get_gamma_ramp)(struct IDirect3DDevice9 *, void *);
    HRESULT (WINAPI *get_render_target)(struct IDirect3DDevice9 *, struct IDirect3DSurface9 **);
    HRESULT (WINAPI *get_depth_stencil_surface)(struct IDirect3DDevice9 *, struct IDirect3DSurface9 **);
    HRESULT (WINAPI *set_render_target)(struct IDirect3DDevice9 *, struct IDirect3DSurface9 *);
    HRESULT (WINAPI *set_depth_stencil_surface)(struct IDirect3DDevice9 *, struct IDirect3DSurface9 *);
    HRESULT (WINAPI *begin_scene)(struct IDirect3DDevice9 *);
    HRESULT (WINAPI *end_scene)(struct IDirect3DDevice9 *);
    HRESULT (WINAPI *clear)(struct IDirect3DDevice9 *, DWORD, const void *, DWORD, DWORD, float, DWORD);
    HRESULT (WINAPI *set_transform)(struct IDirect3DDevice9 *, UINT, const void *);
    HRESULT (WINAPI *get_transform)(struct IDirect3DDevice9 *, UINT, void *);
    HRESULT (WINAPI *multiply_transform)(struct IDirect3DDevice9 *, UINT, const void *);
    HRESULT (WINAPI *set_viewport)(struct IDirect3DDevice9 *, const void *);
    HRESULT (WINAPI *get_viewport)(struct IDirect3DDevice9 *, void *);
    HRESULT (WINAPI *set_material)(struct IDirect3DDevice9 *, const void *);
    HRESULT (WINAPI *get_material)(struct IDirect3DDevice9 *, void *);
    HRESULT (WINAPI *set_light)(struct IDirect3DDevice9 *, DWORD, const void *);
    HRESULT (WINAPI *get_light)(struct IDirect3DDevice9 *, DWORD, void *);
    HRESULT (WINAPI *light_enable)(struct IDirect3DDevice9 *, DWORD, BOOL);
    HRESULT (WINAPI *get_light_enable)(struct IDirect3DDevice9 *, DWORD, BOOL *);
    HRESULT (WINAPI *set_clip_plane)(struct IDirect3DDevice9 *, DWORD, const float *);
    HRESULT (WINAPI *get_clip_plane)(struct IDirect3DDevice9 *, DWORD, float *);
    HRESULT (WINAPI *set_clip_status)(struct IDirect3DDevice9 *, const void *);
    HRESULT (WINAPI *get_clip_status)(struct IDirect3DDevice9 *, void *);
    HRESULT (WINAPI *get_texture)(struct IDirect3DDevice9 *, DWORD, struct IDirect3DBaseTexture9 **);
    HRESULT (WINAPI *set_texture)(struct IDirect3DDevice9 *, DWORD, struct IDirect3DBaseTexture9 *);
    HRESULT (WINAPI *validate_device)(struct IDirect3DDevice9 *, DWORD *);
    HRESULT (WINAPI *set_palette_entries)(struct IDirect3DDevice9 *, UINT, const void *);
    HRESULT (WINAPI *get_palette_entries)(struct IDirect3DDevice9 *, UINT, void *);
    HRESULT (WINAPI *set_current_texture_palette)(struct IDirect3DDevice9 *, UINT);
    HRESULT (WINAPI *get_current_texture_palette)(struct IDirect3DDevice9 *, UINT *);
    HRESULT (WINAPI *draw_primitive)(struct IDirect3DDevice9 *, UINT, UINT, UINT);
    HRESULT (WINAPI *draw_primitive_up)(struct IDirect3DDevice9 *, UINT, UINT, const void *, UINT);
    HRESULT (WINAPI *draw_indexed_primitive_up)(struct IDirect3DDevice9 *, UINT, UINT, UINT, UINT, const void *, UINT, const void *, UINT);
    HRESULT (WINAPI *set_vertex_shader_constant)(struct IDirect3DDevice9 *, UINT, const float *, UINT);
    HRESULT (WINAPI *get_vertex_shader_constant)(struct IDirect3DDevice9 *, UINT, float *, UINT);
    HRESULT (WINAPI *set_stream_source)(struct IDirect3DDevice9 *, UINT, struct IDirect3DVertexBuffer9 *, UINT);
    HRESULT (WINAPI *get_stream_source)(struct IDirect3DDevice9 *, UINT, struct IDirect3DVertexBuffer9 **, UINT *);
    HRESULT (WINAPI *set_indices)(struct IDirect3DDevice9 *, struct IDirect3DIndexBuffer9 *);
    HRESULT (WINAPI *get_indices)(struct IDirect3DDevice9 *, struct IDirect3DIndexBuffer9 **);
    HRESULT (WINAPI *set_pixel_shader)(struct IDirect3DDevice9 *, struct IDirect3DPixelShader9 *);
    HRESULT (WINAPI *set_pixel_shader_constant)(struct IDirect3DDevice9 *, UINT, const float *, UINT);
    HRESULT (WINAPI *get_pixel_shader_constant)(struct IDirect3DDevice9 *, UINT, float *, UINT);
    HRESULT (WINAPI *draw_rect_patch)(struct IDirect3DDevice9 *, UINT, const float *, const void *);
    HRESULT (WINAPI *draw_tri_patch)(struct IDirect3DDevice9 *, UINT, const float *, const void *);
    HRESULT (WINAPI *delete_patch)(struct IDirect3DDevice9 *, UINT);
} D9WGDeviceMethods8;
const D9WGDeviceMethods8 *WINAPI d9wg_d3d9_core_device_methods8(
        struct IDirect3DDevice9 *device);
/* Legacy8 SetRenderTarget preflights the color/depth pair before any mutation.
 * NULL color preserves the live color and viewport; NULL depth unbinds depth.
 * Non-NULL color resets the live viewport, even during state-block recording.
 * Binding storage is shared with9; failures retain the committed prefix. */
HRESULT WINAPI d9wg_d3d9_core_set_render_target8(struct IDirect3DDevice9 *device,
        struct IDirect3DSurface9 *color, struct IDirect3DSurface9 *depth);
/* Borrowed parent for wrapping a held public surface: kind0=device,
 * kind3=texture child,kind4=cube child. Standalone target storage and swap
 * surfaces use the Device8 container. No references are changed; the caller
 * must keep the surface public reference alive while using this result.
 * With valid output pointers, rejected input clears both outputs. */
HRESULT WINAPI d9wg_d3d9_core_surface_owner8(struct IDirect3DSurface9 *surface,
        struct IUnknown **owner, UINT *kind);
#endif
