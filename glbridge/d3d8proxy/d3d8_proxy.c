/*
 * App-local Direct3D 8 frontend for Windows XP guests running in v86.
 *
 * This DLL deliberately does not load WineD3D or opengl32.dll.  It keeps D3D8
 * COM/state/resource semantics in the guest, batches high-level commands in
 * the existing v86gl.sys DMA arena, and sends one D8WG stream to WebGPU.
 *
 * The fixed-function path implements Maple's XYZRHW and XYZ geometry,
 * transforms, material/lights, fog, depth/stencil, texture stages and dynamic
 * resources. Unsupported resource and programmable paths return
 * D3DERR_INVALIDCALL rather than pretending that rendering succeeded.
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
#include <stdint.h>
#include "../openglproxy/v86gl_ioctl.h"
#include "d3d8_protocol.h"

/* These legacy D3D shade-cap bits are omitted by some d3d8.h variants. */
#ifndef D3DPSHADECAPS_COLORFLATRGB
#define D3DPSHADECAPS_COLORFLATRGB 0x00000002u
#endif
#ifndef D3DPSHADECAPS_ALPHAFLATBLEND
#define D3DPSHADECAPS_ALPHAFLATBLEND 0x00001000u
#endif

#define D8WG_LOG_PREFIX "[d3d8-webgpu] "
#ifdef D8WG_DIAGNOSTIC_TRACE
#define V86WG_DIAGNOSTIC_COMPONENT "d3d8-webgpu"
#define V86WG_DIAGNOSTIC_FILE_STEM "d3d8_trace"
#include "../diagnostic_trace.h"
#define D8WG_TRACE_ERROR(result) \
    v86wg_diagnostic_hresult((HRESULT)(result), __func__, __LINE__)
#define D8WG_TRACE(...) v86wg_diagnostic_write(__VA_ARGS__)
#else
#define D8WG_TRACE_ERROR(result) (result)
#define D8WG_TRACE(...) ((void)0)
#endif
#define D8WG_MAX_RENDER_STATES 256u
#define D8WG_MAX_TEXTURE_STAGES 8u
#define D8WG_MAX_TEXTURE_STAGE_STATES 32u
#define D8WG_MAX_STREAMS 16u
#define D8WG_MAX_LIGHTS 8u
#define D8WG_TRANSFORM_VIEW_SLOT 0u
#define D8WG_TRANSFORM_PROJECTION_SLOT 1u
#define D8WG_TRANSFORM_TEXTURE_SLOT 2u
#define D8WG_TRANSFORM_WORLD_SLOT 10u
#define D8WG_MAX_TRANSFORMS (D8WG_TRANSFORM_WORLD_SLOT + 256u)
#define D8WG_VGL2_RECORD_HEADER_BYTES 8u
#define D8WG_HANDLE_GENERATION_ONE (1u << 20)
/* Stage 6: D3D8 shader model 1.x. MaxVertexShaderConst/MaxPixelShaderValue
 * were already pre-staged in fill_caps() ahead of Stage 6 landing. */
#define D8WG_MAX_VS_CONSTANTS 96u
#define D8WG_MAX_PS_CONSTANTS 8u
#define D8WG_MAX_SHADER_TOKENS 8192u
/* A D3D9 vertex declaration may name at most one element per vertex register
 * per stream; D3D8 declarations cannot exceed 16 registers, and the FVF path
 * tops out at position + normal + psize + 2 colours + 8 texture coordinate
 * sets + blend weights/indices. 32 covers both with room to spare. */
#define D8WG_MAX_VERTEX_ELEMENTS 32u
#define D8WG_MAX_CLIP_PLANES 6u
/* D3D8 does not bound the palette index; this is the table the guest keeps
 * shadowed for GetPaletteEntries, and matches what the host allocates. */
#define D8WG_MAX_PALETTES 256u

typedef struct D8Direct3D D8Direct3D;
typedef struct D8Device D8Device;
typedef struct D8VertexBuffer D8VertexBuffer;
typedef struct D8IndexBuffer D8IndexBuffer;
typedef struct D8Texture D8Texture;
typedef struct D8Surface D8Surface;
typedef struct D8StateBlock D8StateBlock;
typedef struct D8SwapChain D8SwapChain;
typedef struct D8Shader D8Shader;
typedef struct D8CubeTexture D8CubeTexture;
typedef struct D8VolumeTexture D8VolumeTexture;
typedef struct D8Volume D8Volume;
typedef struct D8LightState D8LightState;

/*
 * The device window's client area in screen coordinates.
 *
 * D8WG had a wire command shaped exactly like this; D9WG does not, because
 * D3D9 apps present continuously and the host can read the geometry off every
 * PRESENT. The D3D8 path still needs it separately: a title that draws one
 * frame and then only pumps messages must still have its overlay follow the
 * window, which is why emit_surface_update_and_flush() exists at all.
 */
typedef struct D8ClientArea {
    uint32_t device_handle;
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} D8ClientArea;

typedef struct D8TextureLevel {
    BYTE *shadow;
    UINT width;
    UINT height;
    /* 1 for a 2D level and for one cube face's level; the slice count for a
     * volume level. byte_count covers all slices, slice_pitch one of them. */
    UINT depth;
    UINT row_pitch;
    UINT row_count;
    UINT slice_pitch;
    UINT byte_count;
    RECT lock_rect;
    /* Volume locks carry a slice range as well as a rectangle. For 2D and cube
     * levels this is always the single slice 0. */
    UINT lock_z;
    UINT lock_depth;
    DWORD lock_flags;
    BOOL locked;
} D8TextureLevel;

typedef struct D8StreamBinding {
    D8VertexBuffer *buffer;
    UINT stride;
} D8StreamBinding;

/*
 * MaxActiveLights limits how many lights participate in one draw; it does not
 * limit the DWORD indices an application may assign. Keep the common 0..7
 * entries inline for the hot fixed-function path and store sparse higher
 * indices here. The two masks are used only by state-block copies of a node.
 */
struct D8LightState {
    D8LightState *next;
    DWORD index;
    D3DLIGHT8 light;
    BOOL set;
    BOOL enabled;
    BOOL light_mask;
    BOOL enable_mask;
};

typedef struct D8StateSnapshot {
    BYTE render_mask[D8WG_MAX_RENDER_STATES];
    BYTE texture_stage_mask[D8WG_MAX_TEXTURE_STAGES]
                           [D8WG_MAX_TEXTURE_STAGE_STATES];
    BYTE texture_mask[D8WG_MAX_TEXTURE_STAGES];
    BYTE stream_mask[D8WG_MAX_STREAMS];
    BYTE transform_mask[D8WG_MAX_TRANSFORMS];
    BYTE light_mask[D8WG_MAX_LIGHTS];
    BYTE light_enable_mask[D8WG_MAX_LIGHTS];
    BOOL viewport_mask;
    BOOL material_mask;
    BOOL indices_mask;
    BOOL vertex_shader_mask;
    BOOL pixel_shader_mask;
    DWORD render_states[D8WG_MAX_RENDER_STATES];
    DWORD texture_stage_states[D8WG_MAX_TEXTURE_STAGES]
                                      [D8WG_MAX_TEXTURE_STAGE_STATES];
    IDirect3DBaseTexture8 *textures[D8WG_MAX_TEXTURE_STAGES];
    D8StreamBinding streams[D8WG_MAX_STREAMS];
    D8IndexBuffer *index_buffer;
    UINT base_vertex_index;
    DWORD vertex_shader;
    DWORD pixel_shader;
    D3DVIEWPORT8 viewport;
    D3DMATRIX transforms[D8WG_MAX_TRANSFORMS];
    D3DMATERIAL8 material;
    D3DLIGHT8 lights[D8WG_MAX_LIGHTS];
    BOOL light_set[D8WG_MAX_LIGHTS];
    BOOL light_enabled[D8WG_MAX_LIGHTS];
    D8LightState *extra_lights;
    BOOL all_lights_scope;
} D8StateSnapshot;

struct D8StateBlock {
    D8StateBlock *next;
    DWORD token;
    D3DSTATEBLOCKTYPE type;
    D8StateSnapshot state;
};

struct D8Direct3D {
    IDirect3D8 iface;
    LONG refcount;
};

struct D8Device {
    IDirect3DDevice8 iface;
    LONG refcount;
    LONG child_parent_refs;
    BOOL releasing_owned_refs;
    D8Direct3D *parent;
    uint32_t handle;
    D3DDEVICE_CREATION_PARAMETERS creation;
    D3DPRESENT_PARAMETERS present;
    D3DDISPLAYMODE display_mode;
    D3DVIEWPORT8 viewport;
    D3DMATRIX transforms[D8WG_MAX_TRANSFORMS];
    D3DMATERIAL8 material;
    D3DLIGHT8 lights[D8WG_MAX_LIGHTS];
    BOOL light_set[D8WG_MAX_LIGHTS];
    BOOL light_enabled[D8WG_MAX_LIGHTS];
    D8LightState *extra_lights;
    DWORD render_states[D8WG_MAX_RENDER_STATES];
    DWORD texture_stage_states[D8WG_MAX_TEXTURE_STAGES]
                                      [D8WG_MAX_TEXTURE_STAGE_STATES];
    D8StreamBinding streams[D8WG_MAX_STREAMS];
    D8IndexBuffer *index_buffer;
    IDirect3DBaseTexture8 *textures[D8WG_MAX_TEXTURE_STAGES];
    UINT base_vertex_index;
    DWORD vertex_shader;
    DWORD pixel_shader;
    BOOL in_scene;
    HWND tracked_window;
    WNDPROC original_window_proc;
    BOOL window_subclassed;
    BOOL window_unicode;
    D8ClientArea last_surface;
    /* D3D8 guarantees six user clip planes; the host implements them as
     * interpolated distances plus a fragment discard. */
    float clip_planes[D8WG_MAX_CLIP_PLANES][4];
    /* Palettes are device state in both APIs: the same P8 texture changes
     * appearance when the app swaps the current palette without re-uploading
     * a byte, so the table and the selection both have to reach the host. */
    PALETTEENTRY palettes[D8WG_MAX_PALETTES][256];
    BOOL palette_set[D8WG_MAX_PALETTES];
    UINT current_palette;
    BOOL has_last_surface;
    D8StateBlock *state_blocks;
    D8StateBlock *recording_state_block;
    DWORD next_state_block_token;
    D8VertexBuffer *vertex_buffers;
    D8IndexBuffer *index_buffers;
    D8Texture *texture_resources;
    D8CubeTexture *cube_resources;
    D8VolumeTexture *volume_resources;
    D8Shader *vertex_shaders;
    D8Shader *pixel_shaders;
    float vs_constants[D8WG_MAX_VS_CONSTANTS][4];
    float ps_constants[D8WG_MAX_PS_CONSTANTS][4];
    uint32_t reset_epoch;
    D8Texture *render_target_texture;
    UINT render_target_level;
    BOOL depth_surface_enabled;
    BYTE *front_shadow;
    UINT front_shadow_pitch;
    UINT additional_swap_chain_count;
    UINT implicit_surface_count;
    D8Surface *depth_surface;
};

struct D8VertexBuffer {
    IDirect3DVertexBuffer8 iface;
    LONG refcount;
    D8Device *device;
    uint32_t handle;
    BYTE *shadow;
    UINT length;
    DWORD usage;
    DWORD fvf;
    D3DPOOL pool;
    DWORD priority;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D8VertexBuffer *next_device_resource;
};

struct D8IndexBuffer {
    IDirect3DIndexBuffer8 iface;
    LONG refcount;
    D8Device *device;
    uint32_t handle;
    BYTE *shadow;
    UINT length;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D8IndexBuffer *next_device_resource;
};

struct D8Texture {
    IDirect3DTexture8 iface;
    LONG refcount;
    D8Device *device;
    uint32_t handle;
    UINT width;
    UINT height;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    D8TextureLevel *levels;
    D8Texture *next_device_resource;
    BOOL lockable_render_target;
    /* Only a render target can be multisampled; every other texture is
     * D3DMULTISAMPLE_NONE and the host allocates it with sampleCount 1. */
    D3DMULTISAMPLE_TYPE multisample_type;
};

/*
 * A cube texture is six mip chains that share one host resource; the level
 * array is face-major, so face F's level L is levels[F * level_count + L].
 * That is also the order D3DCUBEMAP_FACES enumerates, which is the `z` the
 * host indexes its six array layers by.
 */
struct D8CubeTexture {
    IDirect3DCubeTexture8 iface;
    LONG refcount;
    D8Device *device;
    uint32_t handle;
    UINT edge_length;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    D8TextureLevel *levels;          /* 6 * level_count, face-major */
    D8CubeTexture *next_device_resource;
};

struct D8VolumeTexture {
    IDirect3DVolumeTexture8 iface;
    LONG refcount;
    D8Device *device;
    uint32_t handle;
    UINT width;
    UINT height;
    UINT depth;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    D8TextureLevel *levels;
    D8VolumeTexture *next_device_resource;
};

/* IDirect3DVolume8, the volume analogue of IDirect3DSurface8. It is not a
 * D3DRTYPE_RESOURCE in D3D8 -- it has no SetPriority/PreLoad -- so it carries
 * a container pointer and little else. */
struct D8Volume {
    IDirect3DVolume8 iface;
    LONG refcount;
    D8VolumeTexture *texture;
    UINT level;
};

static void free_extra_lights(D8LightState **list);

/*
 * D3D8 vertex/pixel shaders are not COM objects: CreateVertexShader and
 * CreatePixelShader hand back a raw DWORD handle (bit 0 always set, which is
 * how SetVertexShader distinguishes a real shader handle from an FVF code),
 * and the app must call Delete{Vertex,Pixel}Shader explicitly. Keep a shadow
 * copy of the declaration/bytecode token streams so Get*Declaration/
 * Get*Function can answer from guest memory and so Reset can resubmit
 * CREATE_VERTEX_SHADER/CREATE_PIXEL_SHADER for the host executor to
 * re-translate under the new device epoch.
 */
struct D8Shader {
    D8Shader *next;
    uint32_t handle;
    /*
     * D3D8 bundles a vertex declaration into CreateVertexShader; D3D9 makes it
     * a separate object with its own lifetime. The guest allocates one D9WG
     * vertex-declaration handle per vertex shader and binds the pair together,
     * which keeps D3D8's "the declaration is part of the shader" semantics
     * exactly while speaking D3D9's split model on the wire.
     */
    uint32_t declaration_handle;    /* vertex shaders only; 0 for pixel */
    BOOL is_pixel_shader;
    DWORD *declaration_tokens;      /* vertex shaders only; NULL for pixel */
    UINT declaration_token_count;
    DWORD *code_tokens;             /* version token + body, excludes END */
    UINT code_token_count;
    UINT major_version;
    UINT minor_version;
    /* FNV-1a of exactly the bytecode blob sent to the host. The host shader
     * cache keys exclusively on these fields, so leaving them zero aliases
     * every D3D8 shader to the first one compiled in the session. */
    uint32_t hash_low;
    uint32_t hash_high;
    /* D3DVSD_CONST payload. D3D8 applies it when the shader is set, not when
     * it is created, so it is replayed by device_set_vertex_shader(). */
    UINT const_start;
    UINT const_vector_count;
    DWORD const_data[D8WG_MAX_VS_CONSTANTS * 4u];
};

struct D8Surface {
    IDirect3DSurface8 iface;
    LONG refcount;
    D8Texture *texture;
    /* Set instead of `texture` when this surface came from
     * GetCubeMapSurface; `face` then names which of the six it is. */
    D8CubeTexture *cube;
    UINT face;
    D8Device *device;
    UINT level;
    D3DSURFACE_DESC desc;
    BYTE *shadow;
    UINT row_pitch;
    RECT lock_rect;
    DWORD lock_flags;
    BOOL locked;
    BOOL backbuffer;
    BOOL depth_stencil;
    BOOL reset_blocker;
};

struct D8SwapChain {
    IDirect3DSwapChain8 iface;
    LONG refcount;
    D8Device *device;
    D3DPRESENT_PARAMETERS present;
};

static HANDLE g_transport = INVALID_HANDLE_VALUE;
static uint8_t *g_dma_buffer;
static uint32_t g_dma_capacity;
static uint32_t g_batch_bytes;
static uint32_t g_command_count;
static uint32_t g_frame_id = 1;
static uint32_t g_sequence = 1;
static uint32_t g_next_handle = D8WG_HANDLE_GENERATION_ONE;
static uint32_t g_session_id_low;
static uint32_t g_session_id_high;
static BOOL g_transport_failed;
static BOOL g_hello_emitted;
static CRITICAL_SECTION g_transport_lock;

static IDirect3D8Vtbl g_d3d_vtbl;
static IDirect3DDevice8Vtbl g_device_vtbl;
static IDirect3DVertexBuffer8Vtbl g_vb_vtbl;
static IDirect3DIndexBuffer8Vtbl g_ib_vtbl;
static IDirect3DTexture8Vtbl g_texture_vtbl;
static IDirect3DSurface8Vtbl g_surface_vtbl;
static IDirect3DCubeTexture8Vtbl g_cube_texture_vtbl;
static IDirect3DVolumeTexture8Vtbl g_volume_texture_vtbl;
static IDirect3DVolume8Vtbl g_volume_vtbl;
static IDirect3DSwapChain8Vtbl g_swapchain_vtbl;

static void state_block_release_references(D8StateBlock *block);
static BOOL recreate_device_resources(D8Device *device);
static BOOL device_has_reset_blockers(D8Device *device);
static void device_clear_bindings(D8Device *device);
static HRESULT WINAPI device_set_pixel_shader(IDirect3DDevice8 *iface,
        DWORD shader);
static void device_release_owned_references(D8Device *device);
/* Cube helpers used by the shared surface paths, which appear earlier in the
 * file than the cube implementation they belong to. */
static HRESULT cube_lock_face(D8CubeTexture *texture, D3DCUBEMAP_FACES face,
        UINT level, D3DLOCKED_RECT *locked_rect, const RECT *rect,
        DWORD flags);
static HRESULT cube_unlock_face(D8CubeTexture *texture, D3DCUBEMAP_FACES face,
        UINT level);
static HRESULT WINAPI cube_get_level_desc(IDirect3DCubeTexture8 *iface,
        UINT level, D3DSURFACE_DESC *desc);
static BOOL emit_cube_texture_create(D8Device *device, D8CubeTexture *texture);
static BOOL emit_volume_texture_create(D8Device *device,
        D8VolumeTexture *texture);
static BOOL emit_volume_level_update(D8VolumeTexture *texture, UINT level,
        const RECT *rect, UINT first_slice, UINT slice_count);
static D8TextureLevel *cube_level(D8CubeTexture *texture,
        D3DCUBEMAP_FACES face, UINT level);
/* Defined alongside the cube/volume implementations, below the emitters they
 * call; the device vtable names them before that point. */
static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice8 *iface,
        UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DCubeTexture8 **texture_out);
static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice8 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8 **texture_out);

static D8Direct3D *d3d_from_iface(IDirect3D8 *iface)
{
    return (D8Direct3D *)iface;
}

static D8Device *device_from_iface(IDirect3DDevice8 *iface)
{
    return (D8Device *)iface;
}

static D8VertexBuffer *vb_from_iface(IDirect3DVertexBuffer8 *iface)
{
    return (D8VertexBuffer *)iface;
}

static D8IndexBuffer *ib_from_iface(IDirect3DIndexBuffer8 *iface)
{
    return (D8IndexBuffer *)iface;
}

static D8Texture *texture_from_iface(IDirect3DTexture8 *iface)
{
    return (D8Texture *)iface;
}

static D8Surface *surface_from_iface(IDirect3DSurface8 *iface)
{
    return (D8Surface *)iface;
}

static D8CubeTexture *cube_from_iface(IDirect3DCubeTexture8 *iface)
{
    return (D8CubeTexture *)iface;
}

static D8VolumeTexture *volume_texture_from_iface(IDirect3DVolumeTexture8 *iface)
{
    return (D8VolumeTexture *)iface;
}

static D8Volume *volume_from_iface(IDirect3DVolume8 *iface)
{
    return (D8Volume *)iface;
}

static BOOL guid_equal(REFIID left, REFIID right)
{
    UINT index;
    if (!left || !right || left->Data1 != right->Data1
            || left->Data2 != right->Data2 || left->Data3 != right->Data3)
        return FALSE;
    for (index = 0; index < 8; ++index) {
        if (left->Data4[index] != right->Data4[index]) return FALSE;
    }
    return TRUE;
}

static BOOL iid_is_unknown(REFIID iid)
{
    static const IID unknown = { 0x00000000, 0x0000, 0x0000,
        { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    return guid_equal(iid, &unknown);
}

static void d8wg_log(const char *text)
{
    OutputDebugStringA(D8WG_LOG_PREFIX);
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
    D8WG_TRACE("LOG %s", text);
}

static uint32_t allocate_handle(void)
{
    uint32_t handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    if (!handle)
        handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    return handle;
}

/* Shader handles live in a namespace disjoint from allocate_handle()'s
 * buffer/texture counter (see D9WG_SHADER_HANDLE_BASE) and always carry bit 0
 * set, matching the real D3D8 convention that distinguishes a genuine shader
 * handle from a raw FVF token passed to SetVertexShader. */
static uint32_t g_next_shader_handle = D9WG_SHADER_HANDLE_BASE;

static uint32_t allocate_shader_handle(void)
{
    uint32_t handle = (uint32_t)InterlockedExchangeAdd(
            (LONG *)&g_next_shader_handle, 2);
    if (!handle) {
        handle = (uint32_t)InterlockedExchangeAdd(
                (LONG *)&g_next_shader_handle, 2);
    }
    return handle | 1u;
}

static uint8_t *batch_base(void)
{
    return g_dma_buffer + sizeof(V86GLDMADesc)
            + D8WG_VGL2_RECORD_HEADER_BYTES;
}

static uint32_t batch_capacity(void)
{
    /* The last D9WG_RESPONSE_REGION_BYTES of the mapping belong to the host:
     * it writes query results, readback pixels and its liveness heartbeat
     * there. Batches that ran into it would be overwritten mid-flight. */
    if (g_dma_capacity <= D9WG_RESPONSE_REGION_BYTES + sizeof(V86GLDMADesc)
            + D8WG_VGL2_RECORD_HEADER_BYTES)
        return 0;
    return g_dma_capacity - (uint32_t)sizeof(V86GLDMADesc)
            - D8WG_VGL2_RECORD_HEADER_BYTES - D9WG_RESPONSE_REGION_BYTES;
}

static void reset_batch_locked(void)
{
    D9WGBatchHeader *header;

    g_batch_bytes = sizeof(D9WGBatchHeader);
    g_command_count = 0;
    if (!g_dma_buffer || batch_capacity() < sizeof(D9WGBatchHeader))
        return;

    header = (D9WGBatchHeader *)batch_base();
    ZeroMemory(header, sizeof(*header));
    header->magic = D9WG_MAGIC;
    header->version_major = D9WG_VERSION_MAJOR;
    header->version_minor = D9WG_VERSION_MINOR;
    header->frame_id = g_frame_id;
    header->session_id_low = g_session_id_low;
    header->session_id_high = g_session_id_high;
}

static void close_transport_locked(void)
{
    DWORD returned = 0;

    if (g_transport != INVALID_HANDLE_VALUE) {
        if (g_dma_buffer) {
            DeviceIoControl(g_transport, V86GL_IOCTL_UNMAP_BUFFER,
                    NULL, 0, NULL, 0, &returned, NULL);
        }
        CloseHandle(g_transport);
    }
    g_transport = INVALID_HANDLE_VALUE;
    g_dma_buffer = NULL;
    g_dma_capacity = 0;
    g_batch_bytes = 0;
    g_command_count = 0;
}

static BOOL open_transport_locked(void)
{
    V86GLMapBuffer mapping;
    DWORD returned = 0;
#ifdef D8WG_DIAGNOSTIC_TRACE
    DWORD error;
#endif

    if (g_dma_buffer)
        return TRUE;
    if (g_transport_failed)
        return FALSE;

    g_transport = CreateFileA(V86GL_DEVICE_DOS_NAME,
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_transport == INVALID_HANDLE_VALUE) {
#ifdef D8WG_DIAGNOSTIC_TRACE
        error = GetLastError();
#endif
        g_transport_failed = TRUE;
        d8wg_log("could not open \\.\\v86gl");
        D8WG_TRACE("TRANSPORT OPEN_FAIL path=\\\\.\\v86gl "
                "win32=%lu; start v86gl.sys before launching the program",
                error);
        return FALSE;
    }

    ZeroMemory(&mapping, sizeof(mapping));
    if (!DeviceIoControl(g_transport, V86GL_IOCTL_MAP_BUFFER,
            NULL, 0, &mapping, sizeof(mapping), &returned, NULL)) {
#ifdef D8WG_DIAGNOSTIC_TRACE
        error = GetLastError();
#endif
        d8wg_log("v86gl MAP_BUFFER failed");
        D8WG_TRACE("TRANSPORT MAP_FAIL win32=%lu; win32=170 usually means "
                "another process owns the DMA mapping", error);
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }
    if (returned != sizeof(mapping) || !mapping.user_address
            || mapping.buffer_bytes < sizeof(V86GLDMADesc)
                    + D8WG_VGL2_RECORD_HEADER_BYTES
                    + sizeof(D9WGBatchHeader)
                    + sizeof(D9WGCommandHeader)
                    + D9WG_RESPONSE_REGION_BYTES) {
        d8wg_log("v86gl MAP_BUFFER failed");
        D8WG_TRACE("TRANSPORT MAP_INVALID returned=%lu expected=%lu "
                "address=%08lX bytes=%lu", returned,
                (DWORD)sizeof(mapping), mapping.user_address,
                mapping.buffer_bytes);
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }

    g_dma_buffer = (uint8_t *)(uintptr_t)mapping.user_address;
    g_dma_capacity = mapping.buffer_bytes;
    reset_batch_locked();
    D8WG_TRACE("TRANSPORT OPEN_OK address=%08lX bytes=%lu batch_capacity=%lu",
            mapping.user_address, mapping.buffer_bytes, batch_capacity());
    return TRUE;
}

static BOOL submit_batch_locked(BOOL present)
{
    V86GLDMADesc *descriptor;
    D9WGBatchHeader *batch;
    V86GLSubmit submit;
    uint8_t *outer;
    uint32_t outer_bytes;
    DWORD returned = 0;

    if (!open_transport_locked())
        return FALSE;
    if (g_command_count == 0)
        return TRUE;

    batch = (D9WGBatchHeader *)batch_base();
    batch->frame_id = g_frame_id;
    batch->flags = present ? D9WG_BATCH_FLAG_PRESENT : 0;
    batch->command_count = g_command_count;
    batch->command_bytes = g_batch_bytes - sizeof(*batch);
    batch->session_id_low = g_session_id_low;
    batch->session_id_high = g_session_id_high;

    outer = g_dma_buffer + sizeof(V86GLDMADesc);
    outer[0] = (uint8_t)(V86GL_CTRL_D3D9_BATCH & 0xFFu);
    outer[1] = (uint8_t)(V86GL_CTRL_D3D9_BATCH >> 8);
    outer[2] = 0xFF;
    outer[3] = 0xFF;
    outer[4] = (uint8_t)(g_batch_bytes & 0xFFu);
    outer[5] = (uint8_t)((g_batch_bytes >> 8) & 0xFFu);
    outer[6] = (uint8_t)((g_batch_bytes >> 16) & 0xFFu);
    outer[7] = (uint8_t)((g_batch_bytes >> 24) & 0xFFu);

    outer_bytes = D8WG_VGL2_RECORD_HEADER_BYTES + g_batch_bytes;
    descriptor = (V86GLDMADesc *)g_dma_buffer;
    descriptor->magic = V86GL_MAGIC;
    descriptor->version = V86GL_VERSION;
    /* WebGPU Present is an inner command.  Do not ask the GL bridge to swap. */
    descriptor->flags = 0;
    descriptor->frame_id = g_frame_id;
    descriptor->command_count = 1;
    descriptor->command_bytes = outer_bytes;
    descriptor->reserved0 = D9WG_MAGIC;
    descriptor->reserved1 = 0;

    submit.descriptor_bytes = (uint32_t)sizeof(*descriptor) + outer_bytes;
    submit.flags = 0;
    if (!DeviceIoControl(g_transport, V86GL_IOCTL_SUBMIT,
            &submit, sizeof(submit), NULL, 0, &returned, NULL)) {
#ifdef D8WG_DIAGNOSTIC_TRACE
        DWORD error = GetLastError();
#endif
        d8wg_log("D8WG batch submit failed");
        D8WG_TRACE("TRANSPORT SUBMIT_FAIL win32=%lu descriptor_bytes=%lu "
                "commands=%lu present=%lu", error, submit.descriptor_bytes,
                g_command_count, (DWORD)present);
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }

    if (present)
        ++g_frame_id;
    D8WG_TRACE("TRANSPORT SUBMIT_OK descriptor_bytes=%lu commands=%lu "
            "present=%lu", submit.descriptor_bytes, g_command_count,
            (DWORD)present);
    reset_batch_locked();
    return TRUE;
}

static BOOL reserve_command_locked(uint16_t opcode, uint32_t payload_bytes,
        uint32_t extra_bytes, D9WGCommandHeader **command_out,
        uint8_t **payload_out, uint8_t **extra_out)
{
    uint32_t raw_size;
    uint32_t record_size;
    D9WGCommandHeader *command;

    if (!open_transport_locked())
        return FALSE;
    if (payload_bytes > 0xFFFFFFFFu - sizeof(*command) - extra_bytes) {
        D8WG_TRACE("COMMAND REFUSED opcode=%u payload=%lu extra=%lu "
                "reason=size_overflow", opcode, payload_bytes, extra_bytes);
        return FALSE;
    }
    raw_size = (uint32_t)sizeof(*command) + payload_bytes + extra_bytes;
    record_size = D9WG_ALIGN8(raw_size);
    if (record_size > batch_capacity() - sizeof(D9WGBatchHeader)) {
        D8WG_TRACE("COMMAND REFUSED opcode=%u record=%lu capacity=%lu "
                "reason=record_too_large", opcode, record_size,
                batch_capacity());
        return FALSE;
    }
    if (g_batch_bytes + record_size > batch_capacity()
            && !submit_batch_locked(FALSE)) {
        D8WG_TRACE("COMMAND REFUSED opcode=%u record=%lu reason=flush_failed",
                opcode, record_size);
        return FALSE;
    }

    command = (D9WGCommandHeader *)(batch_base() + g_batch_bytes);
    ZeroMemory(command, record_size);
    command->opcode = opcode;
    command->size = record_size;
    command->sequence = g_sequence++;
    if (command_out)
        *command_out = command;
    if (payload_out)
        *payload_out = (uint8_t *)(command + 1);
    if (extra_out)
        *extra_out = (uint8_t *)(command + 1) + payload_bytes;
    g_batch_bytes += record_size;
    ++g_command_count;
    return TRUE;
}

static BOOL emit_command(uint16_t opcode, const void *payload,
        uint32_t payload_bytes)
{
    uint8_t *destination;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(opcode, payload_bytes, 0, NULL,
            &destination, NULL);
    if (result && payload_bytes)
        CopyMemory(destination, payload, payload_bytes);
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_buffer_update(uint32_t handle, uint32_t destination_offset,
        const void *data, uint32_t byte_count, uint32_t lock_flags)
{
    D9WGUpdateBuffer update;
    D9WGCommandHeader *command;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    ZeroMemory(&update, sizeof(update));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_BUFFER,
            sizeof(update), byte_count, &command, &payload, &blob);
    if (result) {
        update.resource_handle = handle;
        update.destination_offset = destination_offset;
        update.byte_count = byte_count;
        update.data_offset = (uint32_t)((uint8_t *)blob - batch_base());
        update.lock_flags = lock_flags;
        CopyMemory(payload, &update, sizeof(update));
        if (byte_count)
            CopyMemory(blob, data, byte_count);
        (void)command;
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* ---- Stage 6: D3D8 shader model 1.x bytecode validation ---- */

static BOOL shader_regtype_valid(DWORD register_type, BOOL is_pixel_shader)
{
    switch (register_type) {
    case D3DSPR_TEMP:
    case D3DSPR_INPUT:
    case D3DSPR_CONST:
    case D3DSPR_ADDR: /* == D3DSPR_TEXTURE; d3d8types.h aliases the value. */
        return TRUE;
    case D3DSPR_RASTOUT:
    case D3DSPR_ATTROUT:
    case D3DSPR_TEXCRDOUT:
        return !is_pixel_shader;
    default:
        return FALSE;
    }
}

/*
 * Returns FALSE for any opcode outside the supported instruction set for this
 * shader type/version. The set is the host translator's, not a smaller one of
 * this DLL's own: a legal instruction refused here is a shader the backend
 * could have run, and the app has no way to tell that apart from a driver
 * with no shader support at all. What stays refused is what the host also
 * refuses (`texdepth`, `texm3x2depth`, `texm3x3`) plus anything malformed, on
 * the parser safety rule "never guess at semantics for an instruction you do
 * not implement". On success,
 * *operand_words is the number of DWORD parameter tokens following the
 * opcode token (D3DSIO_DEF's trailing four are raw float32 immediates, not
 * register-encoded operands, but are still counted here).
 */
static BOOL shader_opcode_supported(WORD opcode, BOOL is_pixel_shader,
        UINT minor, UINT *operand_words)
{
    switch (opcode) {
    case D3DSIO_NOP:
        *operand_words = 0; return TRUE;
    case D3DSIO_MOV:
        *operand_words = 2; return TRUE;
    case D3DSIO_ADD:
    case D3DSIO_SUB:
    case D3DSIO_MUL:
    case D3DSIO_MIN:
    case D3DSIO_MAX:
    case D3DSIO_DP3:
    case D3DSIO_DP4:
        *operand_words = 3; return TRUE;
    case D3DSIO_MAD:
        *operand_words = 4; return TRUE;
    case D3DSIO_RCP:
    case D3DSIO_RSQ:
    case D3DSIO_EXP:
    case D3DSIO_LOG:
    case D3DSIO_LIT:
    case D3DSIO_FRC:
    case D3DSIO_EXPP:
    case D3DSIO_LOGP:
        if (is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_SLT:
    case D3DSIO_SGE:
    case D3DSIO_DST:
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_DEF:
        *operand_words = 5; return TRUE;
    case D3DSIO_M4x4:
    case D3DSIO_M4x3:
    case D3DSIO_M3x4:
    case D3DSIO_M3x3:
    case D3DSIO_M3x2:
        /* dst, the source vector, and the first row of the matrix. These are
         * how every vs_1_x transform is spelled -- 3DMark 2001 opens with
         * `m4x4 oPos, v0, c0` -- and the host translator expands each one into
         * consecutive dot products. Refusing them refused vertex shaders as a
         * feature. */
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_LRP:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CND:
        if (!is_pixel_shader || minor > 3u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CMP:
        if (!is_pixel_shader || minor < 2u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_TEXCOORD:
        /* ps_1_4 renamed this to `texcrd dst, src` and gave it a source
         * operand; 1.1-1.3 `texcoord dst` has none. Reading the wrong number
         * of words here does not just mis-validate one instruction, it
         * desyncs the walk over everything after it. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEX:
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEXKILL:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 1; return TRUE;
    case D3DSIO_PHASE:
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 0; return TRUE;
    case D3DSIO_BEM:
        /* ps_1_4's arithmetic form of the texbem displacement: `bem dst.rg,
         * src0, src1` applies the destination stage's D3DTSS_BUMPENVMAT*
         * matrix without sampling. 1.4-only -- earlier versions have only the
         * texture-addressing spelling. 3DMark 2001's Advanced Pixel Shader
         * test is a ps_1_4 shader built around it, so refusing it failed that
         * test at CreatePixelShader. */
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 3; return TRUE;
    /*
     * The ps_1_x texture-addressing family. The host translates all of these
     * (see the TEXBEM/TEXM3x* cases in d3d9_shader_pipeline.js's emit()), so
     * refusing them here hid environment bump mapping behind a validator that
     * had never been updated to match. TEXDEPTH, TEXM3x2DEPTH and TEXM3x3 stay
     * refused -- those are the three the host itself refuses -- and TEXM3x3DIFF
     * was never implemented by any hardware.
     */
    case D3DSIO_TEXBEM:
    case D3DSIO_TEXBEML:
    case D3DSIO_TEXREG2AR:
    case D3DSIO_TEXREG2GB:
    case D3DSIO_TEXREG2RGB:
    case D3DSIO_TEXDP3:
    case D3DSIO_TEXDP3TEX:
    case D3DSIO_TEXM3x2PAD:
    case D3DSIO_TEXM3x2TEX:
    case D3DSIO_TEXM3x3PAD:
    case D3DSIO_TEXM3x3TEX:
    case D3DSIO_TEXM3x3VSPEC:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_TEXM3x3SPEC:
        /* The one that also takes the eye-ray constant. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    default:
        return FALSE;
    }
}

/*
 * Walk one D3D8 SM1.x token stream (the app-supplied pFunction, starting
 * right after the version token) until D3DSIO_END. Rejects anything that
 * would read past `max_tokens`, any opcode outside shader_opcode_supported,
 * and any register-type field outside the recognized D3DSPR_* set, so a
 * corrupt or hostile token stream can never desync the parser or run
 * unbounded. On success *body_token_count is the token count from index 0
 * through (but excluding) the END token.
 */
static BOOL validate_shader_body(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel_shader, UINT minor, UINT *body_token_count)
{
    UINT offset = 0;
    while (offset < max_tokens) {
        DWORD token = tokens[offset];
        WORD opcode;
        UINT operand_words;

        if (token == (DWORD)D3DVS_END()) {
            *body_token_count = offset;
            return TRUE;
        }
        opcode = (WORD)(token & D3DSI_OPCODE_MASK);
        if (opcode == D3DSIO_COMMENT) {
            UINT comment_words = (UINT)((token & D3DSI_COMMENTSIZE_MASK)
                    >> D3DSI_COMMENTSIZE_SHIFT);
            if (offset + 1u + comment_words > max_tokens) {
                D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=truncated-comment",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode);
                return FALSE;
            }
            offset += 1u + comment_words;
            continue;
        }
        if (!shader_opcode_supported(opcode, is_pixel_shader, minor,
                &operand_words)) {
            D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=unsupported-opcode",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (offset + 1u + operand_words > max_tokens) {
            D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=truncated-operands",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (opcode != D3DSIO_NOP && opcode != D3DSIO_PHASE) {
            DWORD dst_regtype = tokens[offset + 1u] & D3DSP_REGTYPE_MASK;
            if (!shader_regtype_valid(dst_regtype, is_pixel_shader)) {
                D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=invalid-dst-regtype "
                        "regtype=%08lX",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode, dst_regtype);
                return FALSE;
            }
        }
        if (opcode != D3DSIO_DEF && opcode != D3DSIO_NOP
                && opcode != D3DSIO_PHASE) {
            UINT src_count = operand_words - 1u;
            UINT src;
            for (src = 0; src < src_count; ++src) {
                DWORD src_regtype =
                        tokens[offset + 2u + src] & D3DSP_REGTYPE_MASK;
                if (!shader_regtype_valid(src_regtype, is_pixel_shader)) {
                    D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                            "offset=%u opcode=%04X reason=invalid-src-regtype "
                            "source=%u regtype=%08lX",
                            is_pixel_shader ? "pixel" : "vertex", minor,
                            offset, (UINT)opcode, src, src_regtype);
                    return FALSE;
                }
            }
        }
        offset += 1u + operand_words;
    }
    D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
            "reason=missing-end", is_pixel_shader ? "pixel" : "vertex",
            minor, offset);
    return FALSE; /* ran past max_tokens without ever finding D3DSIO_END */
}

/* 64-bit FNV-1a over the raw little-endian token bytes. This is the same
 * byte-oriented implementation as d3d9proxy's shader_bytecode_hash(), and
 * therefore the same key hashTokens() in d3d9_shader_pipeline.js produces.
 * D3D8's stored blob excludes END, so hash exactly code_token_count tokens --
 * i.e. exactly what emit_create_{vertex,pixel}_shader uploads. */
static void shader_bytecode_hash(const DWORD *code, UINT token_count,
        uint32_t *low_out, uint32_t *high_out)
{
    uint32_t low = 0x84222325u;
    uint32_t high = 0xCBF29CE4u;
    UINT index;
    UINT byte_index;

    for (index = 0; index < token_count; ++index) {
        DWORD token = code[index];
        for (byte_index = 0; byte_index < 4; ++byte_index) {
            uint32_t l0, l1, h0, h1, r0, r1, r2, r3;
            low ^= (token >> (byte_index * 8)) & 0xFFu;
            l0 = low & 0xFFFFu;
            l1 = low >> 16;
            h0 = high & 0xFFFFu;
            h1 = high >> 16;
            r0 = l0 * 0x1B3u;
            r1 = l1 * 0x1B3u + (r0 >> 16);
            r2 = h0 * 0x1B3u + (r1 >> 16) + l0;
            r3 = h1 * 0x1B3u + (r2 >> 16) + l1;
            low = ((r1 & 0xFFFFu) << 16) | (r0 & 0xFFFFu);
            high = ((r3 & 0xFFFFu) << 16) | (r2 & 0xFFFFu);
        }
    }
    *low_out = low;
    *high_out = high;
}

/*
 * Walk one D3DVSD_* vertex declaration token stream until D3DVSD_END()
 * (0xFFFFFFFF), bounded by max_tokens. This only checks structural safety
 * (each token's type nibble is one of the seven recognized D3DVSD_TOKEN_*
 * values and END is reached within the bound); the host executor is
 * responsible for interpreting D3DVSD_REG entries into a vertex input
 * layout when it translates the paired vertex shader.
 */
static BOOL validate_vertex_declaration(const DWORD *tokens, UINT max_tokens,
        UINT *token_count)
{
    UINT offset = 0;
    while (offset < max_tokens) {
        DWORD token = tokens[offset];
        DWORD token_type;
        if (token == 0xFFFFFFFFu) {
            *token_count = offset;
            return TRUE;
        }
        token_type = (token >> D3DVSD_TOKENTYPESHIFT) & 0x7u;
        if (token_type > (DWORD)D3DVSD_TOKEN_END)
            return FALSE;
        ++offset;
    }
    return FALSE;
}

static D8Shader *find_shader(D8Shader *list, DWORD handle)
{
    D8Shader *shader;
    for (shader = list; shader; shader = shader->next) {
        if (shader->handle == handle) return shader;
    }
    return NULL;
}

static void free_shader_list(D8Shader *list)
{
    while (list) {
        D8Shader *next = list->next;
        HeapFree(GetProcessHeap(), 0, list->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, list->code_tokens);
        HeapFree(GetProcessHeap(), 0, list);
        list = next;
    }
}

/*
 * Converts a D3D8 vertex shader declaration into the D3DVERTEXELEMENT9 array
 * D3D9 (and therefore the host) works in.
 *
 * The two describe the same thing from opposite ends. D3D8 says "stream N's
 * next `type` bytes load into vertex register v`r`"; D3D9 says "stream N at
 * byte offset X carries a `usage`". The bridge between them is that vs_1_1
 * bytecode has no dcl_ statements, so the only meaning v`r` ever had is the
 * one D3D8's fixed register semantics assign it -- see
 * d3d8_vsd_register_usage() in d3d8_protocol.h.
 *
 * Returns FALSE for a declaration this path cannot represent, rather than
 * emitting a layout that would silently mis-fetch every vertex.
 */
static BOOL declaration_to_elements(const DWORD *tokens, UINT token_count,
        D9WGVertexElement *elements, UINT max_elements, UINT *element_count,
        DWORD *const_data, UINT max_const_vectors, UINT *const_start,
        UINT *const_vector_count)
{
    UINT offsets[D8WG_MAX_STREAMS];
    UINT stream = 0;
    UINT count = 0;
    UINT i;

    ZeroMemory(offsets, sizeof(offsets));
    *element_count = 0;
    *const_start = 0;
    *const_vector_count = 0;

    for (i = 0; i < token_count; ++i) {
        DWORD token = tokens[i];
        DWORD type = (token & D3D8VSD_TOKENTYPEMASK) >> D3D8VSD_TOKENTYPESHIFT;

        if (token == 0xFFFFFFFFu)
            break;
        switch (type) {
        case D3D8VSD_TOKEN_NOP:
            break;
        case D3D8VSD_TOKEN_STREAM:
            /* The tessellator variant drives an N-patch source, which this
             * path does not implement; see fill_caps(). */
            if (token & D3D8VSD_STREAM_TESSFLAG)
                return FALSE;
            stream = token & D3D8VSD_STREAMNUMBERMASK;
            if (stream >= D8WG_MAX_STREAMS)
                return FALSE;
            break;
        case D3D8VSD_TOKEN_STREAMDATA:
            if (token & D3D8VSD_SKIPFLAG) {
                UINT skip = (token & D3D8VSD_SKIPCOUNTMASK)
                        >> D3D8VSD_SKIPCOUNTSHIFT;
                offsets[stream] += skip * (UINT)sizeof(DWORD);
            } else {
                UINT reg = token & D3D8VSD_VERTEXREGMASK;
                UINT data_type = (token & D3D8VSD_DATATYPEMASK)
                        >> D3D8VSD_DATATYPESHIFT;
                UINT size = d3d8_vsdt_size(data_type);
                unsigned usage = 0;
                unsigned usage_index = 0;

                if (!size || count >= max_elements
                        || offsets[stream] > 0xFFFFu)
                    return FALSE;
                d3d8_vsd_register_usage(reg, &usage, &usage_index);
                elements[count].stream = (uint16_t)stream;
                elements[count].offset = (uint16_t)offsets[stream];
                elements[count].type =
                        (uint8_t)d3d8_vsdt_to_decltype(data_type);
                elements[count].method = (uint8_t)D3D9DECLMETHOD_DEFAULT;
                elements[count].usage = (uint8_t)usage;
                elements[count].usage_index = (uint8_t)usage_index;
                ++count;
                offsets[stream] += size;
            }
            break;
        case D3D8VSD_TOKEN_CONSTMEM: {
            /*
             * D3DVSD_CONST embeds constant data in the declaration. D3D8 loads
             * it when the shader is *set*, not when it is created, so it is
             * captured here and replayed by device_set_vertex_shader().
             */
            UINT vectors = (token & D3D8VSD_CONSTCOUNTMASK)
                    >> D3D8VSD_CONSTCOUNTSHIFT;
            UINT dwords = vectors * 4u;

            if (!vectors || vectors > max_const_vectors
                    || i + dwords >= token_count)
                return FALSE;
            *const_start = token & D3D8VSD_CONSTADDRESSMASK;
            *const_vector_count = vectors;
            CopyMemory(const_data, &tokens[i + 1], dwords * sizeof(DWORD));
            i += dwords;
            break;
        }
        case D3D8VSD_TOKEN_TESSELLATOR:
        case D3D8VSD_TOKEN_EXT:
        default:
            return FALSE;
        }
    }
    if (!count)
        return FALSE;
    *element_count = count;
    return TRUE;
}

/* Whether a D3D8 position field is one of the D3DFVF_XYZBn blended forms. */
static BOOL fvf_position_is_blended(DWORD fvf)
{
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    return position >= D3DFVF_XYZB1 && position <= D3DFVF_XYZB5;
}

/*
 * Whether the last DWORD of a D3DFVF_XYZBn block holds matrix indices rather
 * than another weight. D3DFVF_LASTBETA_UBYTE4 says so outright; XYZB5 says so
 * implicitly, because five float weights have no D3DDECLTYPE to be declared as
 * and the runtime has always read that last DWORD as indices. Getting this
 * wrong shifts the offset of every element that follows.
 */
static BOOL fvf_has_blend_indices(DWORD fvf)
{
    if (fvf & D3DFVF_LASTBETA_UBYTE4)
        return TRUE;
    return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZB5;
}

/*
 * Expands an FVF code into the D3DVERTEXELEMENT9 array the host works in.
 *
 * The host deliberately has no FVF decoder: a vertex layout reaches it in
 * exactly one shape no matter whether the app described it with an FVF, a
 * D3D8 vertex shader declaration, or (on the D3D9 path) a real vertex
 * declaration. This is the FVF spelling of that one shape.
 */
static BOOL fvf_to_elements(DWORD fvf, D9WGVertexElement *elements,
        UINT max_elements, UINT *element_count)
{
    static const uint8_t texcoord_type[4] = {
        (uint8_t)D3D9DECLTYPE_FLOAT2, (uint8_t)D3D9DECLTYPE_FLOAT3,
        (uint8_t)D3D9DECLTYPE_FLOAT4, (uint8_t)D3D9DECLTYPE_FLOAT1
    };
    static const UINT texcoord_bytes[4] = { 8u, 12u, 16u, 4u };
    UINT count = 0;
    UINT offset = 0;
    UINT tex_count;
    UINT i;

    if (fvf & D3DFVF_RESERVED0)
        return FALSE;

#define D8_PUSH_ELEMENT(element_type, element_usage, element_usage_index) \
    do { \
        if (count >= max_elements || offset > 0xFFFFu) return FALSE; \
        elements[count].stream = 0; \
        elements[count].offset = (uint16_t)offset; \
        elements[count].type = (uint8_t)(element_type); \
        elements[count].method = (uint8_t)D3D9DECLMETHOD_DEFAULT; \
        elements[count].usage = (uint8_t)(element_usage); \
        elements[count].usage_index = (uint8_t)(element_usage_index); \
        ++count; \
    } while (0)

    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZ) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_POSITION, 0);
        offset += 12;
    } else if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT4, D3D9DECLUSAGE_POSITIONT, 0);
        offset += 16;
    } else if (fvf_position_is_blended(fvf)) {
        DWORD blend_dwords = 1u + (((fvf & D3DFVF_POSITION_MASK)
                - D3DFVF_XYZB1) >> 1);
        BOOL has_indices = fvf_has_blend_indices(fvf);
        DWORD weight_count = blend_dwords - (has_indices ? 1u : 0u);

        D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_POSITION, 0);
        offset += 12;
        if (weight_count) {
            /* FLOAT1..FLOAT4 are D3DDECLTYPE 0..3, so the weight count maps
             * straight onto the enum. It cannot exceed 4: XYZB5 always spends
             * its last DWORD on indices (see fvf_has_blend_indices). */
            D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT1 + (weight_count - 1u),
                    D3D9DECLUSAGE_BLENDWEIGHT, 0);
            offset += weight_count * 4u;
        }
        if (has_indices) {
            D8_PUSH_ELEMENT(D3D9DECLTYPE_UBYTE4, D3D9DECLUSAGE_BLENDINDICES, 0);
            offset += 4;
        }
    }
    /* No position bits at all is legal and deliberately not an error: a vertex
     * shader may take its position from somewhere other than the stream. Only
     * an FVF describing no attribute whatsoever is refused, below. */
    if (fvf & D3DFVF_NORMAL) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_NORMAL, 0);
        offset += 12;
    }
    if (fvf & D3DFVF_PSIZE) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT1, D3D9DECLUSAGE_PSIZE, 0);
        offset += 4;
    }
    if (fvf & D3DFVF_DIFFUSE) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_D3DCOLOR, D3D9DECLUSAGE_COLOR, 0);
        offset += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        D8_PUSH_ELEMENT(D3D9DECLTYPE_D3DCOLOR, D3D9DECLUSAGE_COLOR, 1);
        offset += 4;
    }
    tex_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (tex_count > 8)
        return FALSE;
    for (i = 0; i < tex_count; ++i) {
        /* The two bits per set in the FVF's high half are
         * D3DFVF_TEXTUREFORMAT2/3/4/1 in that order -- the encoding is not the
         * component count, which is why these are tables rather than
         * arithmetic. */
        DWORD size_bits = (fvf >> (16u + i * 2u)) & 0x3u;
        D8_PUSH_ELEMENT(texcoord_type[size_bits], D3D9DECLUSAGE_TEXCOORD, i);
        offset += texcoord_bytes[size_bits];
    }
#undef D8_PUSH_ELEMENT

    if (!count)
        return FALSE;
    *element_count = count;
    return TRUE;
}

/*
 * D3D9 has no SetFVF-shaped wire command that the host decodes itself; the
 * expanded element array travels with it.
 */
static BOOL emit_set_fvf(D8Device *device, DWORD fvf)
{
    D9WGSetFVF command;
    D9WGVertexElement elements[D8WG_MAX_VERTEX_ELEMENTS];
    UINT element_count = 0;
    UINT element_bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!fvf_to_elements(fvf, elements, D8WG_MAX_VERTEX_ELEMENTS,
            &element_count))
        return FALSE;
    element_bytes = element_count * (UINT)sizeof(D9WGVertexElement);

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_FVF, sizeof(command),
            element_bytes, NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.fvf = fvf;
        command.element_count = element_count;
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, elements, element_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_create_vertex_shader(D8Device *device, D8Shader *shader)
{
    D9WGCreateVertexDeclaration declaration;
    D9WGCreateVertexShader command;
    D9WGVertexElement elements[D8WG_MAX_VERTEX_ELEMENTS];
    DWORD const_data[D8WG_MAX_VS_CONSTANTS * 4u];
    UINT element_count = 0;
    UINT const_start = 0;
    UINT const_vectors = 0;
    uint8_t *payload;
    uint8_t *element_blob;
    uint8_t *code_blob;
    UINT code_bytes = shader->code_token_count * (UINT)sizeof(DWORD);
    UINT element_bytes;
    BOOL result;

    if (!declaration_to_elements(shader->declaration_tokens,
            shader->declaration_token_count, elements,
            D8WG_MAX_VERTEX_ELEMENTS, &element_count,
            const_data, D8WG_MAX_VS_CONSTANTS, &const_start, &const_vectors))
        return FALSE;
    element_bytes = element_count * (UINT)sizeof(D9WGVertexElement);
    shader->const_start = const_start;
    shader->const_vector_count = const_vectors;
    if (const_vectors)
        CopyMemory(shader->const_data, const_data,
                const_vectors * 4u * sizeof(DWORD));

    ZeroMemory(&declaration, sizeof(declaration));
    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CREATE_VERTEX_DECLARATION,
            sizeof(declaration), element_bytes, NULL, &payload, &element_blob);
    if (result) {
        declaration.device_handle = device->handle;
        declaration.resource_handle = shader->declaration_handle;
        declaration.element_count = element_count;
        CopyMemory(payload, &declaration, sizeof(declaration));
        CopyMemory(element_blob, elements, element_bytes);
        result = reserve_command_locked(D9WG_OP_CREATE_VERTEX_SHADER,
                sizeof(command), code_bytes, NULL, &payload, &code_blob);
    }
    if (result) {
        command.device_handle = device->handle;
        command.resource_handle = shader->handle;
        command.instruction_token_count = shader->code_token_count;
        command.code_offset = (uint32_t)(code_blob - batch_base());
        command.bytecode_hash_low = shader->hash_low;
        command.bytecode_hash_high = shader->hash_high;
        CopyMemory(payload, &command, sizeof(command));
        if (code_bytes)
            CopyMemory(code_blob, shader->code_tokens, code_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_create_pixel_shader(D8Device *device, D8Shader *shader)
{
    D9WGCreatePixelShader command;
    uint8_t *payload;
    uint8_t *code_blob;
    UINT code_bytes = shader->code_token_count * (UINT)sizeof(DWORD);
    BOOL result;

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CREATE_PIXEL_SHADER,
            sizeof(command), code_bytes, NULL, &payload, &code_blob);
    if (result) {
        command.device_handle = device->handle;
        command.resource_handle = shader->handle;
        command.instruction_token_count = shader->code_token_count;
        command.code_offset = (uint32_t)(code_blob - batch_base());
        command.bytecode_hash_low = shader->hash_low;
        command.bytecode_hash_high = shader->hash_high;
        CopyMemory(payload, &command, sizeof(command));
        if (code_bytes)
            CopyMemory(code_blob, shader->code_tokens, code_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_set_shader(uint16_t opcode, D8Device *device,
        DWORD shader_handle)
{
    D9WGSetShader command;
    command.device_handle = device->handle;
    command.shader_handle = shader_handle;
    return emit_command(opcode, &command, sizeof(command));
}

static BOOL emit_set_shader_constant(uint16_t opcode, D8Device *device,
        UINT start_register, const float *data, UINT vector_count)
{
    D9WGSetShaderConstantF command;
    uint8_t *payload;
    uint8_t *blob;
    UINT data_bytes = vector_count * 16u;
    BOOL result;

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(opcode, sizeof(command), data_bytes,
            NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.start_register = start_register;
        command.vector_count = vector_count;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, data, data_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_destroy_shader(D8Shader *shader)
{
    D9WGDestroyResource destroy;
    BOOL result;

    destroy.resource_handle = shader->handle;
    destroy.resource_kind = shader->is_pixel_shader
            ? D9WG_RESOURCE_PIXEL_SHADER : D9WG_RESOURCE_VERTEX_SHADER;
    result = emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    /* A vertex shader owns the declaration resource created with it, so the
     * two are released together -- D3D8 exposes no way to outlive one. */
    if (!shader->is_pixel_shader && shader->declaration_handle) {
        destroy.resource_handle = shader->declaration_handle;
        destroy.resource_kind = D9WG_RESOURCE_VERTEX_DECLARATION;
        if (!emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy)))
            result = FALSE;
    }
    return result;
}

static BOOL emit_draw_primitive_up(const D9WGDrawPrimitiveUP *draw,
        const void *vertices)
{
    D9WGDrawPrimitiveUP payload_value = *draw;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_PRIMITIVE_UP,
            sizeof(payload_value), payload_value.vertex_bytes, NULL,
            &payload, &blob);
    if (result) {
        payload_value.vertex_data_offset =
                (uint32_t)(blob - batch_base());
        CopyMemory(payload, &payload_value, sizeof(payload_value));
        CopyMemory(blob, vertices, payload_value.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_draw_indexed_primitive_up(
        const D9WGDrawIndexedPrimitiveUP *draw, const void *indices,
        const void *vertices)
{
    D9WGDrawIndexedPrimitiveUP payload_value = *draw;
    uint32_t extra_bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (payload_value.index_bytes >
            0xFFFFFFFFu - payload_value.vertex_bytes)
        return FALSE;
    extra_bytes = payload_value.index_bytes + payload_value.vertex_bytes;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP,
            sizeof(payload_value), extra_bytes, NULL, &payload, &blob);
    if (result) {
        payload_value.index_data_offset =
                (uint32_t)(blob - batch_base());
        payload_value.vertex_data_offset = payload_value.index_data_offset
                + payload_value.index_bytes;
        CopyMemory(payload, &payload_value, sizeof(payload_value));
        CopyMemory(blob, indices, payload_value.index_bytes);
        CopyMemory(blob + payload_value.index_bytes, vertices,
                payload_value.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL multiply_u32(UINT left, UINT right, UINT *result)
{
    if (left && right > 0xFFFFFFFFu / left)
        return FALSE;
    *result = left * right;
    return TRUE;
}

/*
 * Block geometry for every texture format this DLL forwards.
 *
 * The set is drawn from what ../d3d9-webgpu/d3d9_executor.js actually decodes,
 * intersected with what D3D8 defines. The signed bump formats matter more than
 * their obscurity suggests: D3DFMT_V8U8 and friends are what
 * D3DTOP_BUMPENVMAP samples, so without them environment-bump content has no
 * format to live in.
 */
static BOOL texture_format_layout(D3DFORMAT format, UINT *block_width,
        UINT *block_height, UINT *block_bytes)
{
    *block_width = 1;
    *block_height = 1;
    switch ((DWORD)format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_G16R16:
    case D3DFMT_X8L8V8U8:  /* signed bump + luminance */
    case D3DFMT_Q8W8V8U8:  /* signed bump, four channels */
    case D3DFMT_V16U16:
    case D3DFMT_W11V11U10: /* signed 10/11/11, packed in one DWORD */
    case D3DFMT_A2W10V10U10:
    case D3DFMT_INDEX32:    /* probed as a texture by legacy caps scanners */
        *block_bytes = 4;
        return TRUE;
    case D3DFMT_R8G8B8:
        *block_bytes = 3;
        return TRUE;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_A8L8:
    case D3DFMT_A8P8:
    case D3DFMT_V8U8:      /* the classic EMBM bump map */
    case D3DFMT_L6V5U5:
    case D3DFMT_INDEX16:    /* probed as a texture by legacy caps scanners */
        *block_bytes = 2;
        return TRUE;
    case D3DFMT_L8:
    case D3DFMT_A8:
    case D3DFMT_P8:
    case D3DFMT_R3G3B2:
    case D3DFMT_A4L4:
        *block_bytes = 1;
        return TRUE;
    case D3DFMT_DXT1:
        *block_width = 4;
        *block_height = 4;
        *block_bytes = 8;
        return TRUE;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        *block_width = 4;
        *block_height = 4;
        *block_bytes = 16;
        return TRUE;
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
        /* 4:2:2 video: two horizontal texels share one 32-bit YUV block. */
        *block_width = 2;
        *block_bytes = 4;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL texture_level_layout(D3DFORMAT format, UINT width, UINT height,
        UINT *row_pitch, UINT *row_count, UINT *byte_count)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT columns;

    if (!texture_format_layout(format, &block_width, &block_height,
            &block_bytes))
        return FALSE;
    columns = (width + block_width - 1u) / block_width;
    *row_count = (height + block_height - 1u) / block_height;
    return multiply_u32(columns, block_bytes, row_pitch)
            && multiply_u32(*row_pitch, *row_count, byte_count);
}

/*
 * Fills in one mip level's geometry and allocates its shadow. `depth` is 1 for
 * a 2D level or a single cube face's level, and the slice count for a volume
 * level -- which is the only thing that distinguishes the three.
 */
static BOOL allocate_texture_level(D8TextureLevel *level_data,
        D3DFORMAT format, UINT width, UINT height, UINT depth)
{
    level_data->width = width;
    level_data->height = height;
    level_data->depth = depth;
    if (!texture_level_layout(format, width, height, &level_data->row_pitch,
            &level_data->row_count, &level_data->slice_pitch))
        return FALSE;
    if (!multiply_u32(level_data->slice_pitch, depth, &level_data->byte_count))
        return FALSE;
    level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            level_data->byte_count);
    return level_data->shadow != NULL;
}

static void write_surface_color(BYTE *destination, D3DFORMAT format,
        D3DCOLOR color)
{
    UINT a = (color >> 24) & 0xffu;
    UINT r = (color >> 16) & 0xffu;
    UINT g = (color >> 8) & 0xffu;
    UINT b = color & 0xffu;
    WORD packed;
    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        *(DWORD *)destination = color;
        break;
    case D3DFMT_R5G6B5:
        packed = (WORD)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        *(WORD *)destination = packed;
        break;
    case D3DFMT_X1R5G5B5:
        packed = (WORD)(0x8000u | (r >> 3) << 10 | (g >> 3) << 5
                | (b >> 3));
        *(WORD *)destination = packed;
        break;
    case D3DFMT_A1R5G5B5:
        packed = (WORD)((a >> 7) << 15 | (r >> 3) << 10
                | (g >> 3) << 5 | (b >> 3));
        *(WORD *)destination = packed;
        break;
    case D3DFMT_A4R4G4B4:
        packed = (WORD)((a >> 4) << 12 | (r >> 4) << 8
                | (g >> 4) << 4 | (b >> 4));
        *(WORD *)destination = packed;
        break;
    case D3DFMT_L8:
        *destination = (BYTE)((r * 77u + g * 150u + b * 29u) >> 8);
        break;
    case D3DFMT_A8:
        *destination = (BYTE)a;
        break;
    default:
        break;
    }
}

/*
 * Uploads one locked rectangle of one subresource.
 *
 * Deliberately takes the resource identity rather than a D8Texture, because 2D
 * textures, cube faces and volume slices differ only in which `z` they name:
 * a cube face is z = D3DCUBEMAP_FACES ordinal, a volume slice is z = slice.
 * One upload path for all three is what keeps their unlock semantics from
 * drifting apart.
 */
static BOOL emit_level_update(uint32_t handle, D3DFORMAT format,
        const D8TextureLevel *level_data, UINT level, UINT z,
        const RECT *rect)
{
    D9WGUpdateTexture update;
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT block_x;
    UINT block_y;
    UINT row_bytes;
    UINT row_count;
    UINT data_bytes;
    UINT row;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!texture_format_layout(format, &block_width, &block_height,
            &block_bytes))
        return FALSE;
    block_x = (UINT)rect->left / block_width;
    block_y = (UINT)rect->top / block_height;
    if (!multiply_u32(((UINT)(rect->right - rect->left)
            + block_width - 1u) / block_width, block_bytes, &row_bytes))
        return FALSE;
    row_count = ((UINT)(rect->bottom - rect->top)
            + block_height - 1u) / block_height;
    if (!multiply_u32(row_bytes, row_count, &data_bytes))
        return FALSE;

    ZeroMemory(&update, sizeof(update));
    update.resource_handle = handle;
    update.level = level;
    update.x = (uint32_t)rect->left;
    update.y = (uint32_t)rect->top;
    update.z = z;
    update.width = (uint32_t)(rect->right - rect->left);
    update.height = (uint32_t)(rect->bottom - rect->top);
    /* One slice per upload: a cube face and a volume slice are each a single
     * 2D plane, and depth 0 would name an empty region. */
    update.depth = 1;
    update.row_pitch = row_bytes;
    update.data_bytes = data_bytes;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_TEXTURE,
            sizeof(update), data_bytes, NULL, &payload, &blob);
    if (result) {
        update.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &update, sizeof(update));
        for (row = 0; row < row_count; ++row) {
            CopyMemory(blob + row * row_bytes,
                    level_data->shadow
                    + (block_y + row) * level_data->row_pitch
                    + block_x * block_bytes, row_bytes);
        }
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_texture_update(D8Texture *texture, UINT level,
        const RECT *rect)
{
    return emit_level_update(texture->handle, texture->format,
            &texture->levels[level], level, 0, rect);
}

static BOOL primitive_element_count(D3DPRIMITIVETYPE type,
        UINT primitive_count, UINT *element_count)
{
    switch (type) {
    case D3DPT_POINTLIST:
        *element_count = primitive_count;
        return TRUE;
    case D3DPT_LINELIST:
        return multiply_u32(primitive_count, 2u, element_count);
    case D3DPT_LINESTRIP:
        if (primitive_count == 0xFFFFFFFFu)
            return FALSE;
        *element_count = primitive_count + 1u;
        return TRUE;
    case D3DPT_TRIANGLELIST:
        return multiply_u32(primitive_count, 3u, element_count);
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        if (primitive_count > 0xFFFFFFFDu)
            return FALSE;
        *element_count = primitive_count + 2u;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL capture_surface(D8Device *device, HWND window,
        D8ClientArea *surface)
{
    RECT client;
    POINT origin;

    ZeroMemory(surface, sizeof(*surface));
    surface->device_handle = device->handle;
    surface->hwnd = (uint32_t)(uintptr_t)window;
    if (!window || !IsWindow(window) || IsIconic(window))
        return FALSE;
    if (!GetClientRect(window, &client))
        return FALSE;
    origin.x = 0;
    origin.y = 0;
    if (!ClientToScreen(window, &origin))
        return FALSE;
    surface->x = origin.x;
    surface->y = origin.y;
    surface->width = (uint32_t)(client.right - client.left);
    surface->height = (uint32_t)(client.bottom - client.top);
    return surface->width != 0 && surface->height != 0;
}

static BOOL same_surface(const D8ClientArea *left,
        const D8ClientArea *right)
{
    return left->device_handle == right->device_handle
            && left->hwnd == right->hwnd
            && left->x == right->x
            && left->y == right->y
            && left->width == right->width
            && left->height == right->height;
}

static BOOL emit_surface_update_and_flush(D8Device *device, HWND window,
        BOOL hidden, BOOL force)
{
    D9WGWindowState state;
    D8ClientArea surface;
    uint8_t *payload;
    BOOL result;

    if (hidden) {
        ZeroMemory(&surface, sizeof(surface));
        surface.device_handle = device->handle;
        surface.hwnd = (uint32_t)(uintptr_t)window;
    } else {
        capture_surface(device, window, &surface);
    }
    if (!force && device->has_last_surface
            && same_surface(&device->last_surface, &surface))
        return TRUE;

    EnterCriticalSection(&g_transport_lock);
    ZeroMemory(&state, sizeof(state));
    state.device_handle = surface.device_handle;
    state.hwnd = surface.hwnd;
    state.foreground_hwnd = (uint32_t)(uintptr_t)GetForegroundWindow();
    /*
     * The host both logs this and moves the overlay from it
     * (applyWindowStateGeometry), so the flags have to be real: with none set
     * it reads the window as gone and hides the canvas. The `hidden` branch
     * above deliberately leaves them clear, which is exactly that meaning.
     */
    if (!hidden && window && IsWindow(window)) {
        state.flags = D9WG_WINDOW_IS_WINDOW;
        if (IsWindowVisible(window))
            state.flags |= D9WG_WINDOW_VISIBLE;
        if (IsIconic(window))
            state.flags |= D9WG_WINDOW_ICONIC;
        if (GetForegroundWindow() == window)
            state.flags |= D9WG_WINDOW_FOREGROUND;
        if (!device->present.Windowed)
            state.flags |= D9WG_WINDOW_FULLSCREEN;
    }
    state.window_x = surface.x;
    state.window_y = surface.y;
    state.window_width = surface.width;
    state.window_height = surface.height;
    /* capture_surface() measures the client rect, so these are the same
     * rectangle -- the window rect would include the frame and place the
     * overlay over the title bar. */
    state.client_width = surface.width;
    state.client_height = surface.height;
    result = reserve_command_locked(D9WG_OP_WINDOW_STATE, sizeof(state), 0,
            NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &state, sizeof(state));
        result = submit_batch_locked(FALSE);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result) {
        device->last_surface = surface;
        device->has_last_surface = TRUE;
    }
    return result;
}

static BOOL emit_present_and_flush(D8Device *device, HWND override_window)
{
    D8ClientArea surface;
    D9WGPresent present;
    HWND window = override_window ? override_window : device->tracked_window;
    uint8_t *payload;
    BOOL result;

    capture_surface(device, window, &surface);
    present.device_handle = surface.device_handle;
    present.hwnd = surface.hwnd;
    present.x = surface.x;
    present.y = surface.y;
    present.width = surface.width;
    present.height = surface.height;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_PRESENT, sizeof(present), 0,
            NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &present, sizeof(present));
        result = submit_batch_locked(TRUE);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result && !override_window) {
        device->last_surface = surface;
        device->has_last_surface = TRUE;
    }
    return result;
}

static BOOL emit_device_destroy_and_flush(uint32_t device_handle)
{
    D9WGDestroyResource destroy;
    uint8_t *payload;
    BOOL result;

    destroy.resource_handle = device_handle;
    destroy.resource_kind = 0;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DESTROY_RESOURCE,
            sizeof(destroy), 0, NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &destroy, sizeof(destroy));
        result = submit_batch_locked(FALSE);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

#define D8WG_WINDOW_PROPERTY "D8WG.Device.20260801"

static LRESULT CALLBACK d8wg_window_proc(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    D8Device *device = (D8Device *)GetPropA(window, D8WG_WINDOW_PROPERTY);
    WNDPROC original = device ? device->original_window_proc : NULL;
    BOOL unicode = device ? device->window_unicode : FALSE;

    if (!original)
        return DefWindowProcA(window, message, wparam, lparam);

    if (message == WM_WINDOWPOSCHANGED || message == WM_MOVE
            || message == WM_SIZE || message == WM_SHOWWINDOW) {
        BOOL hidden = (message == WM_SHOWWINDOW && !wparam)
                || (message == WM_SIZE && wparam == SIZE_MINIMIZED);
        emit_surface_update_and_flush(device, window, hidden, FALSE);
    } else if (message == WM_NCDESTROY) {
        emit_surface_update_and_flush(device, window, TRUE, TRUE);
        RemovePropA(window, D8WG_WINDOW_PROPERTY);
        device->tracked_window = NULL;
        device->window_subclassed = FALSE;
    }

    return unicode
            ? CallWindowProcW(original, window, message, wparam, lparam)
            : CallWindowProcA(original, window, message, wparam, lparam);
}

static void detach_device_window(D8Device *device)
{
    HWND window = device->tracked_window;

    if (!device->window_subclassed || !window)
        return;
    if ((D8Device *)GetPropA(window, D8WG_WINDOW_PROPERTY) == device) {
        RemovePropA(window, D8WG_WINDOW_PROPERTY);
        LONG current = device->window_unicode
                ? GetWindowLongW(window, GWL_WNDPROC)
                : GetWindowLongA(window, GWL_WNDPROC);
        if ((WNDPROC)(LONG_PTR)current
                == d8wg_window_proc) {
            if (device->window_unicode) {
                SetWindowLongW(window, GWL_WNDPROC,
                        (LONG)(LONG_PTR)device->original_window_proc);
            } else {
                SetWindowLongA(window, GWL_WNDPROC,
                        (LONG)(LONG_PTR)device->original_window_proc);
            }
        }
    }
    device->window_subclassed = FALSE;
    device->original_window_proc = NULL;
    device->window_unicode = FALSE;
}

static void attach_device_window(D8Device *device, HWND window)
{
    LONG previous;

    device->tracked_window = window;
    if (!window || !IsWindow(window))
        return;
    if (GetPropA(window, D8WG_WINDOW_PROPERTY))
        return;
    device->window_unicode = IsWindowUnicode(window);
    previous = device->window_unicode
            ? GetWindowLongW(window, GWL_WNDPROC)
            : GetWindowLongA(window, GWL_WNDPROC);
    if (!previous)
        return;
    device->original_window_proc = (WNDPROC)(LONG_PTR)previous;
    if (!SetPropA(window, D8WG_WINDOW_PROPERTY, (HANDLE)device)) {
        device->original_window_proc = NULL;
        return;
    }
    SetLastError(0);
    previous = device->window_unicode
            ? SetWindowLongW(window, GWL_WNDPROC,
                    (LONG)(LONG_PTR)d8wg_window_proc)
            : SetWindowLongA(window, GWL_WNDPROC,
                    (LONG)(LONG_PTR)d8wg_window_proc);
    if (!previous && GetLastError()) {
        RemovePropA(window, D8WG_WINDOW_PROPERTY);
        device->original_window_proc = NULL;
        device->window_unicode = FALSE;
        return;
    }
    device->window_subclassed = TRUE;
}

static void emit_hello_once(void)
{
    D9WGHello hello;

    if (InterlockedCompareExchange((LONG *)&g_hello_emitted, TRUE, FALSE))
        return;
    hello.guest_pointer_bits = 32;
    hello.feature_bits = 0;
    hello.session_id_low = g_session_id_low;
    hello.session_id_high = g_session_id_high;
    emit_command(D9WG_OP_HELLO, &hello, sizeof(hello));
}

static void initialize_session_id(HINSTANCE instance)
{
    FILETIME time;
    LARGE_INTEGER counter;
    DWORD process_id = GetCurrentProcessId();
    DWORD thread_id = GetCurrentThreadId();

    GetSystemTimeAsFileTime(&time);
    if (!QueryPerformanceCounter(&counter)) {
        counter.LowPart = GetTickCount();
        counter.HighPart = process_id ^ thread_id;
    }
    g_session_id_low = time.dwLowDateTime ^ counter.LowPart ^ process_id
            ^ (uint32_t)(uintptr_t)instance;
    g_session_id_high = time.dwHighDateTime ^ counter.HighPart
            ^ GetTickCount() ^ (thread_id * 0x9E3779B9u);
    if (!g_session_id_low && !g_session_id_high)
        g_session_id_high = 0xD8A80001u;
}

static void fill_display_mode(D3DDISPLAYMODE *mode, UINT width, UINT height,
        D3DFORMAT format)
{
    mode->Width = width;
    mode->Height = height;
    mode->RefreshRate = 60;
    mode->Format = format;
}

static void fill_caps(D3DCAPS8 *caps)
{
    ZeroMemory(caps, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = D3DADAPTER_DEFAULT;
    /*
     * Every bit below names behaviour the D3D9 host actually implements. That
     * distinction matters more than it looks: a cap reported as 0 is not
     * "conservative" when the feature works -- it tells a title the device
     * cannot do something it can, and titles branch hard on these. 3DMark2001
     * skips whole feature tests, and engines fall back to DX7-era paths.
     *
     * Equally, nothing here may claim a feature this DLL still refuses at its
     * entry point -- see the "Not implemented" list in this directory's README
     * for what remains off, and why.
     */
    caps->Caps2 = D3DCAPS2_CANRENDERWINDOWED
            /* D3DPOOL_MANAGED resources are shadowed guest-side and rebuilt
             * across Reset, which is what managing them means. */
            | D3DCAPS2_CANMANAGERESOURCE
            /* D3DUSAGE_DYNAMIC survives CreateTexture/CreateVertexBuffer and
             * the lock paths honour DISCARD/NOOVERWRITE. */
            | D3DCAPS2_DYNAMICTEXTURES;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE
            | D3DPRESENT_INTERVAL_ONE;
    caps->DevCaps = D3DDEVCAPS_HWRASTERIZATION
            | D3DDEVCAPS_HWTRANSFORMANDLIGHT
            | D3DDEVCAPS_DRAWPRIMTLVERTEX
            | D3DDEVCAPS_EXECUTESYSTEMMEMORY
            | D3DDEVCAPS_EXECUTEVIDEOMEMORY
            | D3DDEVCAPS_TEXTURESYSTEMMEMORY
            | D3DDEVCAPS_TEXTUREVIDEOMEMORY
            /* Pre-transformed vertices draw from both a vertex buffer and a
             * DrawPrimitiveUP pointer. */
            | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY
            | D3DDEVCAPS_TLVERTEXVIDEOMEMORY
            /* Present queues the frame and returns; nothing has to drain
             * before the app may draw again. */
            | D3DDEVCAPS_CANRENDERAFTERFLIP
            /* Driver-model bits rather than features. Some engines read their
             * absence as "this is a DX7-era driver" and take a legacy path. */
            | D3DDEVCAPS_DRAWPRIMITIVES2
            | D3DDEVCAPS_DRAWPRIMITIVES2EX;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE
            | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW
            | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP
            /* D3DRS_ZWRITEENABLE drives the host's depthWriteEnabled. Without
             * MASKZ an app keeps depth writes on for the particle and decal
             * passes that exist to turn them off. */
            | D3DPMISCCAPS_MASKZ
            /* XYZRHW geometry goes through the same viewport and scissor as
             * everything else, so it is clipped rather than pre-clipped. */
            | D3DPMISCCAPS_CLIPTLVERTS
            /* D3DTA_TEMP as an argument and D3DTSS_RESULTARG selecting it are
             * both implemented by the host's blending cascade. */
            | D3DPMISCCAPS_TSSARGTEMP;
    caps->RasterCaps = D3DPRASTERCAPS_ZTEST
            /* D3DRS_ZBIAS is translated to D3D9's DEPTHBIAS and reaches the
             * host's depthBias; see emit_render_state(). */
            | D3DPRASTERCAPS_ZBIAS
            | D3DPRASTERCAPS_FOGVERTEX
            | D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_WFOG
            | D3DPRASTERCAPS_FOGRANGE
            /* WGSL interpolates varyings perspective-correct unless a stage is
             * declared @interpolate(linear), and none are. */
            | D3DPRASTERCAPS_COLORPERSPECTIVE
            /* D3DSAMP_MAXANISOTROPY becomes the WebGPU sampler's
             * maxAnisotropy; paired with MaxAnisotropy below. */
            | D3DPRASTERCAPS_ANISOTROPY;
    caps->ZCmpCaps = 0xFFu;
    caps->SrcBlendCaps = 0x1FFFu;
    caps->DestBlendCaps = 0x1FFFu;
    caps->AlphaCmpCaps = 0xFFu;
    caps->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO
            | D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT
            | D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT
            | D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR;
    caps->ShadeCaps = D3DPSHADECAPS_COLORFLATRGB
            | D3DPSHADECAPS_COLORGOURAUDRGB
            | D3DPSHADECAPS_SPECULARGOURAUDRGB
            | D3DPSHADECAPS_FOGGOURAUD
            | D3DPSHADECAPS_ALPHAFLATBLEND
            | D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA
            | D3DPTEXTURECAPS_MIPMAP
            | D3DPTEXTURECAPS_PERSPECTIVE
            /* The host decodes P8/A8P8 against the current palette at upload,
             * and SetPaletteEntries/SetCurrentTexturePalette both reach it.
             * peFlags carries the alpha, which is what ALPHAPALETTE means. */
            | D3DPTEXTURECAPS_ALPHAPALETTE
            /* D3DTTFF_PROJECTED is honoured: the host divides the texture
             * coordinate by its last component before sampling. */
            | D3DPTEXTURECAPS_PROJECTED
            /* Real IDirect3DCubeTexture8: six faces per level, uploaded as six
             * host array layers and sampled through a texture_cube<f32> by the
             * fixed-function cascade and by a translated pixel shader alike.
             * CUBEMAP_POW2 stays absent -- WebGPU has no such restriction, so
             * claiming one would only make apps pad for nothing. */
            | D3DPTEXTURECAPS_CUBEMAP
            | D3DPTEXTURECAPS_MIPCUBEMAP
            | D3DPTEXTURECAPS_VOLUMEMAP
            | D3DPTEXTURECAPS_MIPVOLUMEMAP;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MINFLINEAR
            | D3DPTFILTERCAPS_MAGFPOINT
            | D3DPTFILTERCAPS_MAGFLINEAR
            | D3DPTFILTERCAPS_MIPFPOINT
            | D3DPTFILTERCAPS_MIPFLINEAR
            | D3DPTFILTERCAPS_MINFANISOTROPIC
            | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    /* Cube and volume textures sample through the same filter path as 2D
     * ones, so advertising CUBEMAP/VOLUMEMAP above while reporting no cube or
     * volume filtering at all would contradict it. */
    caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
    caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP
            | D3DPTADDRESSCAPS_MIRROR
            | D3DPTADDRESSCAPS_CLAMP
            /* WebGPU has no border-colour sampler, so the host clamps for the
             * physical sample and substitutes D3DSAMP_BORDERCOLOR for every
             * coordinate outside the unit domain on a BORDER axis. That is an
             * implementation of the mode, not an approximation of it.
             * MIRRORONCE is the opposite case and stays absent: it has no
             * emulation and would silently fall back to clamp. */
            | D3DPTADDRESSCAPS_BORDER
            /* ADDRESSU/V/W are read and applied separately per stage. */
            | D3DPTADDRESSCAPS_INDEPENDENTUV;
    /* A volume's third axis goes through the same D3DSAMP_ADDRESSW the host
     * already applies, so its addressing modes are the 2D set. */
    caps->VolumeTextureAddressCaps = caps->TextureAddressCaps;
    /* D3DPT_LINELIST/LINESTRIP reach the host's draw path, and lines there are
     * textured, depth-tested, blended and fogged like triangles. */
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST
            | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE
            | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2
            | D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X
            | D3DTEXOPCAPS_MODULATE4X | D3DTEXOPCAPS_ADD
            | D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_ADDSIGNED2X
            | D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH
            | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHA
            | D3DTEXOPCAPS_BLENDFACTORALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHAPM
            | D3DTEXOPCAPS_BLENDCURRENTALPHA
            | D3DTEXOPCAPS_DOTPRODUCT3 | D3DTEXOPCAPS_MULTIPLYADD
            | D3DTEXOPCAPS_LERP
            /* Environment bump mapping: the host displaces the next stage's
             * coordinate by this stage's sampled (du, dv) through
             * D3DTSS_BUMPENVMAT00..11, and the luminance form additionally
             * modulates by BUMPENVLSCALE/BUMPENVLOFFSET. */
            | D3DTEXOPCAPS_BUMPENVMAP
            | D3DTEXOPCAPS_BUMPENVMAPLUMINANCE;
    /* WebGPU guarantees maxTextureDimension2D >= 8192 everywhere, and the host
     * creates its device with the default limits. */
    caps->MaxTextureWidth = 8192;
    caps->MaxTextureHeight = 8192;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 8192;
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1.0e10f;
    /* The host expands points into camera-facing quads, so a point size beyond
     * one pixel is real rather than clamped away by WebGPU's 1px points. */
    caps->MaxPointSize = 256.0f;
    caps->MaxPrimitiveCount = 0xFFFFFu;
    caps->MaxVertexIndex = 0xFFFFFFu;
    /* SetStreamSource forwards every stream index, and the host lays out one
     * vertex buffer per declared stream. */
    caps->MaxStreams = D8WG_MAX_STREAMS;
    /* Advisory, and nothing in this DLL or the protocol clamps a stride. 255
     * was the DX7-era figure and it rejects ordinary skinned vertex formats. */
    caps->MaxStreamStride = 65535;
    caps->VertexShaderVersion = (DWORD)D3DVS_VERSION(1, 1);
    caps->MaxVertexShaderConst = D8WG_MAX_VS_CONSTANTS;
    caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(1, 4);
    /* The largest absolute value a ps_1_x register holds. 8.0 is what SM1.4
     * hardware reported; anything smaller makes an engine pre-scale. */
    caps->MaxPixelShaderValue = 8.0f;
    /* Eight texture coordinate sets, and D3DFVF_PSIZE is bound to the host's
     * point-size input in place of D3DRS_POINTSIZE. */
    caps->FVFCaps = (8u & D3DFVFCAPS_TEXCOORDCOUNTMASK) | D3DFVFCAPS_PSIZE;
    caps->MaxTextureBlendStages = D8WG_MAX_TEXTURE_STAGES;
    caps->MaxSimultaneousTextures = D8WG_MAX_TEXTURE_STAGES;
    caps->MaxActiveLights = D8WG_MAX_LIGHTS;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN
            | D3DVTXPCAPS_MATERIALSOURCE7
            | D3DVTXPCAPS_DIRECTIONALLIGHTS
            | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
    /* Implemented through interpolated distances plus fragment discard,
     * because core WGSL has no user clip-distance builtin. */
    caps->MaxUserClipPlanes = D8WG_MAX_CLIP_PLANES;
    /*
     * Fixed-function vertex blending: the host builds the weighted sum of
     * D3DTS_WORLDMATRIX(0..3) in its generated vertex stage, and indexed
     * blending can name any of the 256 world matrices.
     *
     * D3DVTXPCAPS_TWEENING is deliberately not set alongside it: tweening
     * interpolates two vertex streams by D3DRS_TWEENFACTOR rather than
     * blending matrices, and nothing implements that. It merely shares the
     * D3DVERTEXBLENDFLAGS enum.
     */
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
}

static HRESULT WINAPI d3d_query_interface(IDirect3D8 *iface, REFIID iid,
        void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3D8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3D8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI d3d_add_ref(IDirect3D8 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_from_iface(iface)->refcount);
}

static ULONG WINAPI d3d_release(IDirect3D8 *iface)
{
    D8Direct3D *d3d = d3d_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&d3d->refcount);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, d3d);
    return refs;
}

static HRESULT WINAPI d3d_register_software_device(IDirect3D8 *iface,
        void *initialize)
{
    (void)iface;
    (void)initialize;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}

static UINT WINAPI d3d_get_adapter_count(IDirect3D8 *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI d3d_get_adapter_identifier(IDirect3D8 *iface,
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8 *identifier)
{
    (void)iface;
    if (adapter || !identifier)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(identifier, sizeof(*identifier));
    lstrcpynA(identifier->Driver, "d3d8-webgpu", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description,
            "v86 Direct3D 8 WebGPU Adapter", sizeof(identifier->Description));
    identifier->VendorId = 0x1234;
    identifier->DeviceId = 0x5686;
    identifier->SubSysId = 0x56861234;
    identifier->Revision = 1;
    /* No real driver reports an all-zero DriverVersion, and a caller that
     * sanity-checks the struct -- or prints it, as 3DMark 2001's system-info
     * report does -- has nothing to show for it. product.version.subversion.
     * build packed as two DWORDs, the way a driver reports it; 6.14.10.6764
     * is a plausible XP-era revision and is what ../d3d9proxy reports for the
     * same backend. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6764;
    /* Stable and non-zero, derived from the identity: apps cache this GUID to
     * notice that the driver changed under them, which an all-zero value
     * defeats. The D9WG frontend derives its own the same way, from a
     * different prefix, so the two never collide. */
    identifier->DeviceIdentifier.Data1 = 0xD8E60000u | identifier->DeviceId;
    identifier->DeviceIdentifier.Data2 = (WORD)identifier->VendorId;
    identifier->DeviceIdentifier.Data3 = (WORD)identifier->DeviceId;
    identifier->DeviceIdentifier.Data4[0] = 0x9A;
    identifier->DeviceIdentifier.Data4[1] = 0xB1;
    identifier->DeviceIdentifier.Data4[2] = 0xC2;
    identifier->DeviceIdentifier.Data4[3] = 0xD3;
    identifier->DeviceIdentifier.Data4[4] = 0xE4;
    identifier->DeviceIdentifier.Data4[5] = 0xF5;
    identifier->DeviceIdentifier.Data4[6] = 0x06;
    identifier->DeviceIdentifier.Data4[7] = 0x17;
    /* D3D8 computes the WHQL level only when the caller leaves
     * D3DENUM_NO_WHQL_LEVEL clear. 1 = signed but no date information, rather
     * than 0 = "not certified", which some titles read as a blacklisted
     * driver. */
    identifier->WHQLLevel = (flags & D3DENUM_NO_WHQL_LEVEL) ? 0 : 1;
    return D3D_OK;
}

static UINT WINAPI d3d_get_adapter_mode_count(IDirect3D8 *iface, UINT adapter)
{
    (void)iface;
    return adapter ? 0 : 9;
}

static HRESULT WINAPI d3d_enum_adapter_modes(IDirect3D8 *iface, UINT adapter,
        UINT index, D3DDISPLAYMODE *mode)
{
    static const struct {
        UINT width;
        UINT height;
        D3DFORMAT format;
    } modes[] = {
        { 640, 480, D3DFMT_X1R5G5B5 },
        { 640, 480, D3DFMT_R5G6B5 },
        { 640, 480, D3DFMT_X8R8G8B8 },
        { 800, 600, D3DFMT_X1R5G5B5 },
        { 800, 600, D3DFMT_R5G6B5 },
        { 800, 600, D3DFMT_X8R8G8B8 },
        { 1024, 768, D3DFMT_X1R5G5B5 },
        { 1024, 768, D3DFMT_R5G6B5 },
        { 1024, 768, D3DFMT_X8R8G8B8 }
    };
    (void)iface;
    if (adapter || !mode || index >= sizeof(modes) / sizeof(modes[0]))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_display_mode(mode, modes[index].width, modes[index].height,
            modes[index].format);
    return D3D_OK;
}

static HRESULT WINAPI d3d_get_adapter_display_mode(IDirect3D8 *iface,
        UINT adapter, D3DDISPLAYMODE *mode)
{
    (void)iface;
    if (adapter || !mode)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_display_mode(mode, 1024, 768, D3DFMT_X8R8G8B8);
    return D3D_OK;
}

/*
 * An off-screen plain surface (CreateImageSurface) is CPU memory the app
 * locks, so every uncompressed format can be one -- but only those: a
 * block-compressed or packed-pair surface has no single-texel row for a lock
 * to describe, which is the rule create_standalone_surface already enforces.
 * Answering this query at all matters because an app that asks before
 * creating took silence for "no such format".
 */
static BOOL supported_image_surface_format(D3DFORMAT format)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    return texture_format_layout(format, &block_width, &block_height,
            &block_bytes) && block_width == 1 && block_height == 1;
}

/*
 * Render-target formats. Wider than the two 32-bit ones this frontend started
 * with, because a 16-bit display mode is only usable when its own format is a
 * legal render target: 3DMark 2001 validates every mode it enumerates with
 * CheckDeviceFormat(mode, RENDERTARGET, SURFACE, mode) and drops the ones that
 * fail, so refusing R5G6B5 silently removed every 16-bit mode.
 *
 * The host stores all of them as `rgba8unorm` -- more precision than the
 * 16-bit formats describe, never less -- and packGPUReadbackRow() in
 * ../d3d9-webgpu/d3d9_executor.js re-packs exactly this set on readback, so a
 * lockable render target still hands back the bit layout that was asked for.
 */
static BOOL supported_render_target_format(D3DFORMAT format)
{
    return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8
            || format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5
            || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4;
}

/*
 * Depth-stencil formats. The host satisfies every one of these with a single
 * `depth24plus-stencil8` target: the guest can never read a depth surface
 * back, so the only thing an app can observe is that depth and stencil work,
 * not how many bits each was given. D3DFMT_D32 therefore gets 24 bits of
 * depth, D3DFMT_D15S1 and D3DFMT_D24X4S4 get 8 bits of stencil rather than 1
 * and 4, and D3DFMT_D24X8 gets a stencil buffer it did not ask for. Each is
 * more precision than the app requested, in the one direction that cannot
 * turn a correct scene into a wrong one.
 *
 * D3DFMT_D16_LOCKABLE is deliberately absent: its whole point is that
 * LockRect works on it, WebGPU cannot copy a depth24plus texture to a buffer
 * at all, and a lockable surface whose Lock fails is worse than a format the
 * app never picks. Plenty of real DX8 cards refused it too.
 */
static BOOL supported_depth_stencil_format(D3DFORMAT format)
{
    return format == D3DFMT_D16 || format == D3DFMT_D24S8
            || format == D3DFMT_D32 || format == D3DFMT_D15S1
            || format == D3DFMT_D24X8 || format == D3DFMT_D24X4S4;
}

/* A back buffer is a render target, and D3D8 lets an app pick a 16-bit one
 * against a 32-bit display mode or the reverse. Refusing the 15/16-bit
 * formats failed CreateDevice outright for a title that asks for the back
 * buffer its fullscreen mode implies. */
static BOOL supported_backbuffer_format(D3DFORMAT format)
{
    return supported_render_target_format(format);
}

static HRESULT WINAPI d3d_check_device_type(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    (void)iface;
    (void)windowed;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if ((display_format != D3DFMT_X8R8G8B8
            && display_format != D3DFMT_R5G6B5
            && display_format != D3DFMT_X1R5G5B5)
            || !supported_backbuffer_format(backbuffer_format))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    return D3D_OK;
}

/*
 * A format is creatable exactly when this DLL knows how to lay its levels out
 * and the host knows how to decode it -- which is the same set, by
 * construction: texture_format_layout() is derived from the host's decoder.
 * Keeping one table rather than two is what stops CheckDeviceFormat promising
 * a format CreateTexture then refuses.
 */
static BOOL supported_texture_format(D3DFORMAT format)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    return texture_format_layout(format, &block_width, &block_height,
            &block_bytes);
}

static HRESULT WINAPI d3d_check_device_format(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    (void)iface;
    (void)adapter_format;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (resource_type == D3DRTYPE_TEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_texture_format(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_texture_format(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format(format))
        return D3D_OK;
    /* Volumes have no block-compressed layout on this path, and are never
     * render targets -- device_create_volume_texture refuses both. */
    if (resource_type == D3DRTYPE_VOLUMETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && format != D3DFMT_DXT1 && format != D3DFMT_DXT2
            && format != D3DFMT_DXT3 && format != D3DFMT_DXT4
            && format != D3DFMT_DXT5
            && supported_texture_format(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_TEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format(format))
        return D3D_OK;
    /*
     * 3DMark2001 validates every enumerated display mode with this exact
     * query before it exposes the adapter:
     *
     *   CheckDeviceFormat(display_format, D3DUSAGE_RENDERTARGET,
     *           D3DRTYPE_SURFACE, display_format)
     *
     * A render-target surface is the object returned by CreateRenderTarget,
     * which this frontend backs with the same one-level target texture as the
     * texture case above. Omitting SURFACE here therefore hid a capability the
     * device really has and made 3DMark reject every 32-bit display mode as
     * non-renderable before CreateDevice was ever reached.
     */
    if (resource_type == D3DRTYPE_SURFACE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && (usage & D3DUSAGE_DEPTHSTENCIL)
            && supported_depth_stencil_format(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_image_surface_format(format))
        return D3D_OK;
    D8WG_TRACE("CHECK_FORMAT REFUSE adapter=%lu type=%lu adapter_fmt=%08lX "
            "usage=%08lX resource_type=%lu format=%08lX", adapter,
            (DWORD)type, (DWORD)adapter_format, usage, (DWORD)resource_type,
            (DWORD)format);
    return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
}

/*
 * WebGPU defines exactly two sample counts -- 1 and 4 -- so those are the only
 * two D3DMULTISAMPLE_TYPE values that can be honoured rather than approximated.
 * D3DMULTISAMPLE_2/8/16_SAMPLES have no representation, and rounding one of
 * them to 4 would silently give an app a different image than it asked for.
 */
static BOOL supported_multisample_type(D3DMULTISAMPLE_TYPE multisample)
{
    return multisample == D3DMULTISAMPLE_NONE
            || multisample == D3DMULTISAMPLE_4_SAMPLES;
}

static HRESULT WINAPI d3d_check_multisample(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT format, WINBOOL windowed,
        D3DMULTISAMPLE_TYPE multisample)
{
    (void)iface;
    (void)format;
    (void)windowed;
    return !adapter && type == D3DDEVTYPE_HAL
            && supported_multisample_type(multisample)
            ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_depth_stencil(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        D3DFORMAT render_format, D3DFORMAT depth_format)
{
    (void)iface;
    (void)adapter_format;
    (void)render_format;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    return supported_depth_stencil_format(depth_format)
            ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_get_device_caps(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DCAPS8 *caps)
{
    (void)iface;
    if (adapter || type != D3DDEVTYPE_HAL || !caps)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_caps(caps);
    return D3D_OK;
}

static HMONITOR WINAPI d3d_get_adapter_monitor(IDirect3D8 *iface,
        UINT adapter)
{
    (void)iface;
    if (adapter)
        return NULL;
    return MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
}

static void device_init_states(D8Device *device)
{
    UINT stage;
    UINT transform;
    ZeroMemory(device->render_states, sizeof(device->render_states));
    ZeroMemory(device->texture_stage_states,
            sizeof(device->texture_stage_states));
    device->render_states[D3DRS_ZENABLE] = D3DZB_TRUE;
    device->render_states[D3DRS_ZWRITEENABLE] = TRUE;
    device->render_states[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    device->render_states[D3DRS_CULLMODE] = D3DCULL_CCW;
    device->render_states[D3DRS_LIGHTING] = TRUE;
    device->render_states[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    device->render_states[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    device->render_states[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    device->render_states[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFFu;
    device->render_states[D3DRS_COLORWRITEENABLE] = 0xFu;
    device->render_states[D3DRS_FOGEND] = 0x3F800000u;
    device->render_states[D3DRS_FOGDENSITY] = 0x3F800000u;
    device->render_states[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
    device->render_states[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    device->render_states[D3DRS_COLORVERTEX] = TRUE;
    device->render_states[D3DRS_LOCALVIEWER] = TRUE;
    device->render_states[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    device->render_states[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    device->render_states[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    device->render_states[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    ZeroMemory(&device->material, sizeof(device->material));
    device->material.Diffuse.r = device->material.Diffuse.g =
            device->material.Diffuse.b = device->material.Diffuse.a = 1.0f;
    device->material.Ambient = device->material.Diffuse;
    free_extra_lights(&device->extra_lights);
    ZeroMemory(device->lights, sizeof(device->lights));
    ZeroMemory(device->light_set, sizeof(device->light_set));
    ZeroMemory(device->light_enabled, sizeof(device->light_enabled));
    for (transform = 0; transform < D8WG_MAX_TRANSFORMS; ++transform) {
        ZeroMemory(&device->transforms[transform], sizeof(D3DMATRIX));
        device->transforms[transform].m[0][0] = 1.0f;
        device->transforms[transform].m[1][1] = 1.0f;
        device->transforms[transform].m[2][2] = 1.0f;
        device->transforms[transform].m[3][3] = 1.0f;
    }
    for (stage = 0; stage < D8WG_MAX_TEXTURE_STAGES; ++stage) {
        device->texture_stage_states[stage][D3DTSS_COLOROP] =
                stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        device->texture_stage_states[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_ALPHAOP] =
                stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_TEXCOORDINDEX] = stage;
        device->texture_stage_states[stage][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        device->texture_stage_states[stage][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        device->texture_stage_states[stage][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
        device->texture_stage_states[stage][D3DTSS_MINFILTER] = D3DTEXF_POINT;
        device->texture_stage_states[stage][D3DTSS_MIPFILTER] = D3DTEXF_NONE;
        device->texture_stage_states[stage][D3DTSS_MAXANISOTROPY] = 1;
        device->texture_stage_states[stage][D3DTSS_RESULTARG] = D3DTA_CURRENT;
    }
}

static BOOL emit_vertex_buffer_create(D8Device *device,
        D8VertexBuffer *buffer)
{
    D9WGCreateBuffer command;
    command.device_handle = device->handle;
    command.resource_handle = buffer->handle;
    command.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
    command.byte_count = buffer->length;
    command.usage = buffer->usage;
    command.fvf = buffer->fvf;
    command.pool = buffer->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_BUFFER, &command, sizeof(command));
}

static BOOL emit_index_buffer_create(D8Device *device,
        D8IndexBuffer *buffer)
{
    D9WGCreateBuffer command;
    command.device_handle = device->handle;
    command.resource_handle = buffer->handle;
    command.resource_kind = D9WG_RESOURCE_BUFFER_INDEX;
    command.byte_count = buffer->length;
    command.usage = buffer->usage;
    command.fvf = buffer->format;
    command.pool = buffer->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_BUFFER, &command, sizeof(command));
}

static BOOL emit_texture_create(D8Device *device, D8Texture *texture)
{
    D9WGCreateTexture2D command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    /* CreateRenderTarget is the only entry point that can set this; see
     * supported_multisample_type() for which values reach here. Quality stays
     * 0: WebGPU has no quality levels and the host refuses a non-zero one. */
    command.multisample_type = texture->multisample_type;
    command.multisample_quality = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_2D, &command, sizeof(command));
}

static BOOL device_has_reset_blockers(D8Device *device)
{
    D8VertexBuffer *vb;
    D8IndexBuffer *ib;
    D8Texture *texture;
    UINT level;
    if (device->in_scene || device->recording_state_block
            || device->additional_swap_chain_count
            || device->implicit_surface_count)
        return TRUE;
    for (vb = device->vertex_buffers; vb; vb = vb->next_device_resource) {
        if (vb->pool == D3DPOOL_DEFAULT || vb->locked)
            return TRUE;
    }
    for (ib = device->index_buffers; ib; ib = ib->next_device_resource) {
        if (ib->pool == D3DPOOL_DEFAULT || ib->locked)
            return TRUE;
    }
    for (texture = device->texture_resources; texture;
            texture = texture->next_device_resource) {
        if (texture->pool == D3DPOOL_DEFAULT)
            return TRUE;
        for (level = 0; level < texture->level_count; ++level) {
            if (texture->levels[level].locked)
                return TRUE;
        }
    }
    return FALSE;
}

static void device_clear_bindings(D8Device *device)
{
    UINT index;
    for (index = 0; index < D8WG_MAX_STREAMS; ++index) {
        D8VertexBuffer *buffer = device->streams[index].buffer;
        device->streams[index].buffer = NULL;
        device->streams[index].stride = 0;
        if (buffer) IDirect3DVertexBuffer8_Release(&buffer->iface);
    }
    if (device->index_buffer) {
        D8IndexBuffer *buffer = device->index_buffer;
        device->index_buffer = NULL;
        device->base_vertex_index = 0;
        IDirect3DIndexBuffer8_Release(&buffer->iface);
    }
    for (index = 0; index < D8WG_MAX_TEXTURE_STAGES; ++index) {
        IDirect3DBaseTexture8 *texture = device->textures[index];
        device->textures[index] = NULL;
        if (texture) IDirect3DBaseTexture8_Release(texture);
    }
    device->vertex_shader = 0;
    device->pixel_shader = 0;
    if (device->render_target_texture) {
        D8Texture *texture = device->render_target_texture;
        device->render_target_texture = NULL;
        device->render_target_level = 0;
        IDirect3DTexture8_Release(&texture->iface);
    }
    if (device->depth_surface) {
        D8Surface *surface = device->depth_surface;
        device->depth_surface = NULL;
        IDirect3DSurface8_Release(&surface->iface);
    }
    device->depth_surface_enabled = device->present.EnableAutoDepthStencil;
}

/*
 * D3D8 resources retain their parent device, while device state retains the
 * currently bound resources.  Track the parent-side references separately so
 * that releasing the last public device reference can drop device-owned state
 * and break that otherwise-uncollectable COM cycle.  Externally held resources
 * continue to keep the device alive and may still return it from GetDevice().
 */
static void device_release_owned_references(D8Device *device)
{
    D8StateBlock *block;
    D8StateBlock *recording;

    device_clear_bindings(device);

    block = device->state_blocks;
    device->state_blocks = NULL;
    while (block) {
        D8StateBlock *next = block->next;
        state_block_release_references(block);
        HeapFree(GetProcessHeap(), 0, block);
        block = next;
    }
    recording = device->recording_state_block;
    device->recording_state_block = NULL;
    if (recording) {
        state_block_release_references(recording);
        HeapFree(GetProcessHeap(), 0, recording);
    }
}

static BOOL recreate_device_resources(D8Device *device)
{
    D8VertexBuffer *vb;
    D8IndexBuffer *ib;
    D8Texture *texture;
    UINT level;
    for (vb = device->vertex_buffers; vb; vb = vb->next_device_resource) {
        vb->handle = allocate_handle();
        if (!emit_vertex_buffer_create(device, vb)
                || !emit_buffer_update(vb->handle, 0, vb->shadow,
                        vb->length, 0))
            return FALSE;
    }
    for (ib = device->index_buffers; ib; ib = ib->next_device_resource) {
        ib->handle = allocate_handle();
        if (!emit_index_buffer_create(device, ib)
                || !emit_buffer_update(ib->handle, 0, ib->shadow,
                        ib->length, 0))
            return FALSE;
    }
    for (texture = device->texture_resources; texture;
            texture = texture->next_device_resource) {
        texture->handle = allocate_handle();
        if (!emit_texture_create(device, texture)) return FALSE;
        for (level = 0; level < texture->level_count; ++level) {
            RECT full;
            SetRect(&full, 0, 0, (int)texture->levels[level].width,
                    (int)texture->levels[level].height);
            if (!emit_texture_update(texture, level, &full)) return FALSE;
        }
    }
    {
        D8CubeTexture *cube;
        D8VolumeTexture *volume;
        UINT face;
        for (cube = device->cube_resources; cube;
                cube = cube->next_device_resource) {
            cube->handle = allocate_handle();
            if (!emit_cube_texture_create(device, cube)) return FALSE;
            for (face = 0; face < 6u; ++face) {
                for (level = 0; level < cube->level_count; ++level) {
                    D8TextureLevel *level_data =
                            cube_level(cube, (D3DCUBEMAP_FACES)face, level);
                    RECT full;
                    SetRect(&full, 0, 0, (int)level_data->width,
                            (int)level_data->height);
                    if (!emit_level_update(cube->handle, cube->format,
                            level_data, level, face, &full))
                        return FALSE;
                }
            }
        }
        for (volume = device->volume_resources; volume;
                volume = volume->next_device_resource) {
            volume->handle = allocate_handle();
            if (!emit_volume_texture_create(device, volume)) return FALSE;
            for (level = 0; level < volume->level_count; ++level) {
                D8TextureLevel *level_data = &volume->levels[level];
                RECT full;
                SetRect(&full, 0, 0, (int)level_data->width,
                        (int)level_data->height);
                if (!emit_volume_level_update(volume, level, &full, 0,
                        level_data->depth))
                    return FALSE;
            }
        }
    }
    {
        D8Shader *shader;
        /* Unlike buffers and textures -- whose handles are private and hidden
         * behind COM pointers, so Reset can freely renumber them -- a shader
         * handle is the opaque DWORD the application itself holds and will
         * pass back to SetVertexShader after Reset. Keep it stable and let
         * the host re-establish the same handle under the new device epoch. */
        for (shader = device->vertex_shaders; shader; shader = shader->next) {
            if (!emit_create_vertex_shader(device, shader)) return FALSE;
        }
        for (shader = device->pixel_shaders; shader; shader = shader->next) {
            if (!emit_create_pixel_shader(device, shader)) return FALSE;
        }
        /* Host-side constant registers live under the old device_handle and
         * do not survive Reset; resend the guest shadow banks so a shader
         * bound again after Reset sees the same constants it had before. */
        if (!emit_set_shader_constant(D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F,
                device, 0, &device->vs_constants[0][0],
                D8WG_MAX_VS_CONSTANTS))
            return FALSE;
        if (!emit_set_shader_constant(D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F,
                device, 0, &device->ps_constants[0][0], D8WG_MAX_PS_CONSTANTS))
            return FALSE;
    }
    return TRUE;
}

static HRESULT WINAPI d3d_create_device(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, HWND focus_window, DWORD behavior,
        D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice8 **device_out)
{
    D8Direct3D *d3d = d3d_from_iface(iface);
    D8Device *device;
    D9WGCreateDevice command;
    HWND window;
    RECT client;
    POINT origin;
    UINT front_row_count;
    UINT front_byte_count;

    if (!device_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = NULL;
    if (adapter || type != D3DDEVTYPE_HAL || !parameters)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!supported_multisample_type(parameters->MultiSampleType))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->EnableAutoDepthStencil
            && !supported_depth_stencil_format(
                parameters->AutoDepthStencilFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
            && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferWidth > 8192
            || parameters->BackBufferHeight > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* D3D8 permits 0..3 back buffers (0 meaning 1). The host always presents
     * through a single WebGPU surface, so extra back buffers are a
     * presentation detail we can satisfy with one target; only a request
     * beyond the D3D8 maximum is a genuine error. */
    if (parameters->BackBufferCount > 3)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    device = (D8Device *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*device));
    if (!device)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    device->iface.lpVtbl = &g_device_vtbl;
    device->refcount = 1;
    device->parent = d3d;
    IDirect3D8_AddRef(iface);
    device->handle = allocate_handle();
    device->present = *parameters;
    device->creation.AdapterOrdinal = adapter;
    device->creation.DeviceType = type;
    device->creation.hFocusWindow = focus_window;
    device->creation.BehaviorFlags = behavior;
    fill_display_mode(&device->display_mode,
            parameters->BackBufferWidth ? parameters->BackBufferWidth : 640,
            parameters->BackBufferHeight ? parameters->BackBufferHeight : 480,
            parameters->BackBufferFormat == D3DFMT_UNKNOWN
                    ? D3DFMT_X8R8G8B8 : parameters->BackBufferFormat);
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = device->display_mode.Width;
    device->viewport.Height = device->display_mode.Height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device_init_states(device);
    device->depth_surface_enabled = parameters->EnableAutoDepthStencil;
    if (!texture_level_layout(device->display_mode.Format,
            device->display_mode.Width, device->display_mode.Height,
            &device->front_shadow_pitch, &front_row_count,
            &front_byte_count)
            || front_row_count != device->display_mode.Height) {
        IDirect3D8_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    }
    device->front_shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, front_byte_count);
    if (!device->front_shadow) {
        IDirect3D8_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }

    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
            : focus_window;
    SetRect(&client, 0, 0, (int)device->display_mode.Width,
            (int)device->display_mode.Height);
    origin.x = 0;
    origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    command.device_handle = device->handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = parameters->BackBufferWidth
            ? parameters->BackBufferWidth : (uint32_t)(client.right - client.left);
    command.height = parameters->BackBufferHeight
            ? parameters->BackBufferHeight : (uint32_t)(client.bottom - client.top);
    if (!command.width)
        command.width = 640;
    if (!command.height)
        command.height = 480;
    command.backbuffer_format = device->display_mode.Format;
    command.windowed = parameters->Windowed;
    command.behavior_flags = behavior;
    command.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    command.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    /* Validated above against supported_multisample_type(). Quality stays 0:
     * WebGPU has no quality levels, and the host refuses a non-zero one. */
    command.multisample_type = parameters->MultiSampleType;
    command.multisample_quality = 0;
    if (!emit_command(D9WG_OP_CREATE_DEVICE, &command, sizeof(command))) {
        IDirect3D8_Release(iface);
        HeapFree(GetProcessHeap(), 0, device->front_shadow);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    attach_device_window(device, window);
    capture_surface(device, window, &device->last_surface);
    device->has_last_surface = TRUE;

    *device_out = &device->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_query_interface(IDirect3DDevice8 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DDevice8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DDevice8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI device_add_ref(IDirect3DDevice8 *iface)
{
    return (ULONG)InterlockedIncrement(&device_from_iface(iface)->refcount);
}

static ULONG WINAPI device_release(IDirect3DDevice8 *iface)
{
    D8Device *device = device_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&device->refcount);

    if (device->releasing_owned_refs)
        return refs;

    if ((LONG)refs == InterlockedCompareExchange(
            &device->child_parent_refs, 0, 0)) {
        device->releasing_owned_refs = TRUE;
        device_release_owned_references(device);
        device->releasing_owned_refs = FALSE;
        refs = (ULONG)InterlockedCompareExchange(&device->refcount, 0, 0);
    }
    if (!refs) {
        detach_device_window(device);
        emit_device_destroy_and_flush(device->handle);
        IDirect3D8_Release(&device->parent->iface);
        /* Shaders are not COM objects (no per-object refcount/Release), so
         * unlike vertex_buffers/index_buffers/texture_resources -- which are
         * always already empty here because each resource unlinks and frees
         * itself from its own Release() -- any shader the app never called
         * Delete{Vertex,Pixel}Shader on must be freed here. */
        free_shader_list(device->vertex_shaders);
        free_shader_list(device->pixel_shaders);
        free_extra_lights(&device->extra_lights);
        HeapFree(GetProcessHeap(), 0, device->front_shadow);
        HeapFree(GetProcessHeap(), 0, device);
    }
    return refs;
}

static void device_child_add_ref(D8Device *device)
{
    InterlockedIncrement(&device->child_parent_refs);
    IDirect3DDevice8_AddRef(&device->iface);
}

static void device_child_release(D8Device *device)
{
    InterlockedDecrement(&device->child_parent_refs);
    IDirect3DDevice8_Release(&device->iface);
}

static HRESULT WINAPI device_test_cooperative_level(IDirect3DDevice8 *iface)
{
    (void)iface;
    return D3D_OK;
}

static UINT WINAPI device_get_available_texture_mem(IDirect3DDevice8 *iface)
{
    (void)iface;
    return 128u * 1024u * 1024u;
}

static HRESULT WINAPI device_discard_bytes(IDirect3DDevice8 *iface, DWORD bytes)
{
    (void)iface;
    (void)bytes;
    return D3D_OK;
}

static HRESULT WINAPI device_get_direct3d(IDirect3DDevice8 *iface,
        IDirect3D8 **d3d_out)
{
    D8Device *device = device_from_iface(iface);
    if (!d3d_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *d3d_out = &device->parent->iface;
    IDirect3D8_AddRef(*d3d_out);
    return D3D_OK;
}

static HRESULT WINAPI device_get_caps(IDirect3DDevice8 *iface, D3DCAPS8 *caps)
{
    (void)iface;
    if (!caps)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_caps(caps);
    return D3D_OK;
}

static HRESULT WINAPI device_get_display_mode(IDirect3DDevice8 *iface,
        D3DDISPLAYMODE *mode)
{
    if (!mode)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *mode = device_from_iface(iface)->display_mode;
    return D3D_OK;
}

static HRESULT WINAPI device_get_creation_parameters(IDirect3DDevice8 *iface,
        D3DDEVICE_CREATION_PARAMETERS *parameters)
{
    if (!parameters)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *parameters = device_from_iface(iface)->creation;
    return D3D_OK;
}

static HRESULT WINAPI device_reset(IDirect3DDevice8 *iface,
        D3DPRESENT_PARAMETERS *parameters)
{
    D8Device *device = device_from_iface(iface);
    D9WGResetDevice reset;
    RECT client;
    POINT origin;
    HWND window;
    BYTE *new_front_shadow;
    UINT new_front_pitch;
    UINT new_front_rows;
    UINT new_front_bytes;

    if (!parameters || !supported_multisample_type(parameters->MultiSampleType)
            || parameters->BackBufferCount > 1
            || device_has_reset_blockers(device))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (parameters->EnableAutoDepthStencil
            && !supported_depth_stencil_format(
                parameters->AutoDepthStencilFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
            && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
            : device->creation.hFocusWindow;
    SetRect(&client, 0, 0, 640, 480);
    origin.x = origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    ZeroMemory(&reset, sizeof(reset));
    reset.old_device_handle = device->handle;
    reset.new_device_handle = allocate_handle();
    reset.hwnd = (uint32_t)(uintptr_t)window;
    reset.x = origin.x;
    reset.y = origin.y;
    reset.width = parameters->BackBufferWidth
            ? parameters->BackBufferWidth : (uint32_t)(client.right - client.left);
    reset.height = parameters->BackBufferHeight
            ? parameters->BackBufferHeight : (uint32_t)(client.bottom - client.top);
    if (!reset.width)
        reset.width = device->display_mode.Width;
    if (!reset.height)
        reset.height = device->display_mode.Height;
    reset.backbuffer_format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
            ? device->display_mode.Format : parameters->BackBufferFormat;
    reset.windowed = parameters->Windowed;
    reset.behavior_flags = device->creation.BehaviorFlags;
    reset.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    reset.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    /* Validated above against supported_multisample_type(). */
    reset.multisample_type = parameters->MultiSampleType;
    reset.multisample_quality = 0;
    if (reset.width > 8192 || reset.height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!texture_level_layout((D3DFORMAT)reset.backbuffer_format,
            reset.width, reset.height, &new_front_pitch, &new_front_rows,
            &new_front_bytes) || new_front_rows != reset.height)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    new_front_shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, new_front_bytes);
    if (!new_front_shadow)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    if (!emit_command(D9WG_OP_RESET, &reset, sizeof(reset)))
    {
        HeapFree(GetProcessHeap(), 0, new_front_shadow);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    device_clear_bindings(device);
    HeapFree(GetProcessHeap(), 0, device->front_shadow);
    device->front_shadow = new_front_shadow;
    device->front_shadow_pitch = new_front_pitch;
    device->handle = reset.new_device_handle;
    ++device->reset_epoch;
    device->present = *parameters;
    fill_display_mode(&device->display_mode, reset.width, reset.height,
            reset.backbuffer_format);
    device_init_states(device);
    device->viewport.X = device->viewport.Y = 0;
    device->viewport.Width = reset.width;
    device->viewport.Height = reset.height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device->depth_surface_enabled = parameters->EnableAutoDepthStencil;
    if (!recreate_device_resources(device))
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    if (window != device->tracked_window) {
        detach_device_window(device);
        attach_device_window(device, window);
    }
    capture_surface(device, window, &device->last_surface);
    device->has_last_surface = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI device_present(IDirect3DDevice8 *iface,
        const RECT *source, const RECT *destination, HWND override_window,
        const RGNDATA *dirty_region)
{
    (void)source;
    (void)destination;
    (void)dirty_region;
    return emit_present_and_flush(device_from_iface(iface), override_window)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_begin_scene(IDirect3DDevice8 *iface)
{
    D8Device *device = device_from_iface(iface);
    D9WGDeviceOnly command;
    if (device->in_scene)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    device->in_scene = TRUE;
    command.device_handle = device->handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_BEGIN_SCENE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_end_scene(IDirect3DDevice8 *iface)
{
    D8Device *device = device_from_iface(iface);
    D9WGDeviceOnly command;
    if (!device->in_scene)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    device->in_scene = FALSE;
    command.device_handle = device->handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_END_SCENE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_clear(IDirect3DDevice8 *iface, DWORD rect_count,
        const D3DRECT *rects, DWORD flags, D3DCOLOR color, float depth,
        DWORD stencil)
{
    D8Device *device = device_from_iface(iface);
    D9WGClear command;
    D9WGCommandHeader *header;
    uint8_t *payload;
    uint8_t *rect_data;
    uint32_t rect_bytes;
    BOOL result;

    if (rect_count && !rects)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (rect_count > 0xFFFFFFFFu / sizeof(*rects))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    rect_bytes = rect_count * sizeof(*rects);
    command.device_handle = device->handle;
    command.clear_flags = flags;
    command.color = color;
    command.depth = depth;
    command.stencil = stencil;
    command.rect_count = rect_count;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CLEAR, sizeof(command), rect_bytes,
            &header, &payload, &rect_data);
    if (result) {
        CopyMemory(payload, &command, sizeof(command));
        if (rect_bytes)
            CopyMemory(rect_data, rects, rect_bytes);
        (void)header;
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result && (flags & D3DCLEAR_TARGET)) {
        BYTE *shadow = device->front_shadow;
        UINT pitch = device->front_shadow_pitch;
        UINT width = device->display_mode.Width;
        UINT height = device->display_mode.Height;
        D3DFORMAT format = device->display_mode.Format;
        UINT block_width;
        UINT block_height;
        UINT pixel_bytes;
        UINT rect_index;
        if (device->render_target_texture) {
            D8TextureLevel *level = &device->render_target_texture->levels[
                    device->render_target_level];
            shadow = level->shadow;
            pitch = level->row_pitch;
            width = level->width;
            height = level->height;
            format = device->render_target_texture->format;
        }
        if (!texture_format_layout(format, &block_width, &block_height,
                &pixel_bytes) || block_width != 1 || block_height != 1)
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
        for (rect_index = 0; rect_index < (rect_count ? rect_count : 1u);
                ++rect_index) {
            LONG left = rect_count ? rects[rect_index].x1 : 0;
            LONG top = rect_count ? rects[rect_index].y1 : 0;
            LONG right = rect_count ? rects[rect_index].x2 : (LONG)width;
            LONG bottom = rect_count ? rects[rect_index].y2 : (LONG)height;
            LONG y;
            if (left < 0) left = 0;
            if (top < 0) top = 0;
            if (right > (LONG)width) right = (LONG)width;
            if (bottom > (LONG)height) bottom = (LONG)height;
            for (y = top; y < bottom; ++y) {
                BYTE *row = shadow + y * pitch;
                LONG x;
                for (x = left; x < right; ++x)
                    write_surface_color(row + x * pixel_bytes, format, color);
            }
        }
    }
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

/*
 * Dimensions of the colour target currently bound, which is the surface a
 * viewport is clipped against -- not the display mode. An app may bind an
 * offscreen render target of any size, in either direction, and 3DMark 2001's
 * advanced pixel-shader test binds one larger than the 800x600 mode it runs
 * in. Same rule device_clear() already follows for its shadow copy.
 */
static void current_render_target_size(D8Device *device, UINT *width,
        UINT *height)
{
    if (device->render_target_texture) {
        D8TextureLevel *level = &device->render_target_texture->levels[
                device->render_target_level];
        *width = level->width;
        *height = level->height;
        return;
    }
    *width = device->present.BackBufferWidth ? device->present.BackBufferWidth
            : device->display_mode.Width;
    *height = device->present.BackBufferHeight
            ? device->present.BackBufferHeight : device->display_mode.Height;
}

static HRESULT WINAPI device_set_viewport(IDirect3DDevice8 *iface,
        const D3DVIEWPORT8 *viewport)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetViewport command;
    UINT target_width;
    UINT target_height;
    if (!viewport || !viewport->Width || !viewport->Height)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* Validating against the display mode instead of the bound target
     * rejected the viewport SetRenderTarget installs for an offscreen target
     * bigger than the screen, which reached the app as "Could not set render
     * target - D3DERR_INVALIDCALL". */
    current_render_target_size(device, &target_width, &target_height);
    if (viewport->X > target_width
            || viewport->Y > target_height
            || viewport->Width > target_width - viewport->X
            || viewport->Height > target_height - viewport->Y
            || viewport->MinZ < 0.0f || viewport->MaxZ > 1.0f
            || viewport->MinZ > viewport->MaxZ) {
        D8WG_TRACE("VIEWPORT REFUSE x=%lu y=%lu size=%lux%lu target=%ux%u",
                viewport->X, viewport->Y, viewport->Width, viewport->Height,
                target_width, target_height);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (device->recording_state_block)
        device->recording_state_block->state.viewport_mask = TRUE;
    if (device->viewport.X == viewport->X
            && device->viewport.Y == viewport->Y
            && device->viewport.Width == viewport->Width
            && device->viewport.Height == viewport->Height
            && device->viewport.MinZ == viewport->MinZ
            && device->viewport.MaxZ == viewport->MaxZ)
        return D3D_OK;
    device->viewport = *viewport;
    command.device_handle = device->handle;
    command.x = viewport->X;
    command.y = viewport->Y;
    command.width = viewport->Width;
    command.height = viewport->Height;
    command.min_z = viewport->MinZ;
    command.max_z = viewport->MaxZ;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_VIEWPORT, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_viewport(IDirect3DDevice8 *iface,
        D3DVIEWPORT8 *viewport)
{
    if (!viewport)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *viewport = device_from_iface(iface)->viewport;
    return D3D_OK;
}

/*
 * D3D8 -> D3D9 render state translation.
 *
 * Every render state the two APIs share has the same numeric value, so the
 * default case is a straight pass-through. Only two groups need work: the
 * states D3D9 deleted, and D3DRS_ZBIAS.
 *
 * The deleted ones are dropped rather than refused. The guest still shadows
 * them, so GetRenderState() answers what the app set and a state block
 * captures and restores them; there is simply nothing downstream that could
 * act on them. Refusing instead would fail SetRenderState() for a call every
 * real D3D8 runtime accepts.
 */
static BOOL emit_render_state(D8Device *device, D3DRENDERSTATETYPE state,
        DWORD value)
{
    D9WGSetRenderState command;

    command.device_handle = device->handle;
    command.reserved = 0;
    switch ((UINT)state) {
    case D3D8RS_LINEPATTERN:              /* no D3D9 equivalent */
    case D3D8RS_ZVISIBLE:                 /* never implemented by any driver */
    case D3D8RS_EDGEANTIALIAS:            /* superseded by multisampling */
    case D3D8RS_SOFTWAREVERTEXPROCESSING: /* a device method in D3D9 */
    case D3D8RS_PATCHSEGMENTS:            /* N-patch tessellation, see fill_caps */
    case D3D8RS_POSITIONORDER:
    case D3D8RS_NORMALORDER:
        return TRUE;
    case D3D8RS_ZBIAS: {
        /*
         * D3D8: an integer 0..16 pulling the fragment towards the viewer.
         * D3D9: a float added to the depth value, so the same intent is a
         * negative bias. The host reads this state as raw float bits.
         */
        float bias = (float)(value > 16u ? 16u : value)
                * D3D8_ZBIAS_TO_DEPTHBIAS_STEP;
        command.state = D3D9RS_DEPTHBIAS;
        CopyMemory(&command.value, &bias, sizeof(command.value));
        break;
    }
    default:
        command.state = (uint32_t)state;
        command.value = value;
        break;
    }
    return emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command));
}

/*
 * D3D8 configured samplers through SetTextureStageState; D3D9 split that into
 * SetSamplerState with its own numbering. The blending-cascade states kept
 * D3D8's numbers in D3D9 and pass straight through; the ten sampler ones are
 * re-addressed. See d3d8_stage_state_to_sampler_state() in d3d8_protocol.h.
 *
 * D3D8's stage index and D3D9's sampler index coincide for the fixed-function
 * stages, which is exactly the range a D3D8 app can reach.
 */
static BOOL emit_texture_stage_state(D8Device *device, DWORD stage,
        D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    unsigned sampler_state = d3d8_stage_state_to_sampler_state((unsigned)state);

    if (sampler_state) {
        D9WGSetSamplerState command;
        command.device_handle = device->handle;
        command.sampler = stage;
        command.state = sampler_state;
        command.value = value;
        return emit_command(D9WG_OP_SET_SAMPLER_STATE, &command,
                sizeof(command));
    }
    {
        D9WGSetTextureStageState command;
        command.device_handle = device->handle;
        command.stage = stage;
        command.state = (uint32_t)state;
        command.value = value;
        return emit_command(D9WG_OP_SET_TEXTURE_STAGE_STATE, &command,
                sizeof(command));
    }
}

static HRESULT WINAPI device_set_render_state(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D8Device *device = device_from_iface(iface);
    if ((UINT)state >= D8WG_MAX_RENDER_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.render_mask[state] = 1;
    if (device->render_states[state] == value)
        return D3D_OK;
    device->render_states[state] = value;
    return emit_render_state(device, state, value)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_render_state(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    if (!value || (UINT)state >= D8WG_MAX_RENDER_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->render_states[state];
    return D3D_OK;
}

static HRESULT WINAPI device_set_texture_stage_state(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D8Device *device = device_from_iface(iface);
    if (stage >= D8WG_MAX_TEXTURE_STAGES
            || (UINT)state >= D8WG_MAX_TEXTURE_STAGE_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.texture_stage_mask[stage][state]
                = 1;
    if (device->texture_stage_states[stage][state] == value)
        return D3D_OK;
    device->texture_stage_states[stage][state] = value;
    return emit_texture_stage_state(device, stage, state, value)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_texture_stage_state(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value)
{
    if (!value || stage >= D8WG_MAX_TEXTURE_STAGES
            || (UINT)state >= D8WG_MAX_TEXTURE_STAGE_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->texture_stage_states[stage][state];
    return D3D_OK;
}

static HRESULT WINAPI device_create_vertex_buffer(IDirect3DDevice8 *iface,
        UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
        IDirect3DVertexBuffer8 **buffer_out)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *buffer;
    if (!buffer_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer = (D8VertexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    buffer->iface.lpVtbl = &g_vb_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->fvf = fvf;
    buffer->pool = pool;

    if (!emit_vertex_buffer_create(device, buffer)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    buffer->next_device_resource = device->vertex_buffers;
    device->vertex_buffers = buffer;
    *buffer_out = &buffer->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_create_index_buffer(IDirect3DDevice8 *iface,
        UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DIndexBuffer8 **buffer_out)
{
    D8Device *device = device_from_iface(iface);
    D8IndexBuffer *buffer;
    UINT index_size;

    if (!buffer_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (format == D3DFMT_INDEX16)
        index_size = 2;
    else if (format == D3DFMT_INDEX32)
        index_size = 4;
    else
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (length % index_size)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    buffer = (D8IndexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    buffer->iface.lpVtbl = &g_ib_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->format = format;
    buffer->pool = pool;

    if (!emit_index_buffer_create(device, buffer)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    buffer->next_device_resource = device->index_buffers;
    device->index_buffers = buffer;
    *buffer_out = &buffer->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_set_stream_source(IDirect3DDevice8 *iface,
        UINT stream, IDirect3DVertexBuffer8 *buffer_iface, UINT stride)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *buffer = buffer_iface ? vb_from_iface(buffer_iface) : NULL;
    D9WGSetStreamSource command;
    if (stream >= D8WG_MAX_STREAMS)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (buffer && (buffer_iface->lpVtbl != &g_vb_vtbl
            || buffer->device != device))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.stream_mask[stream] = 1;
    if (device->streams[stream].buffer == buffer
            && device->streams[stream].stride == stride)
        return D3D_OK;
    if (buffer)
        IDirect3DVertexBuffer8_AddRef(buffer_iface);
    if (device->streams[stream].buffer)
        IDirect3DVertexBuffer8_Release(
                &device->streams[stream].buffer->iface);
    device->streams[stream].buffer = buffer;
    device->streams[stream].stride = stride;
    command.device_handle = device->handle;
    command.stream = stream;
    command.buffer_handle = buffer ? buffer->handle : 0;
    command.stride = stride;
    /* D3D9's SetStreamSource takes a byte offset into the buffer; D3D8's does
     * not, so a D3D8 binding always starts at the first vertex. */
    command.offset_in_bytes = 0;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_STREAM_SOURCE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_stream_source(IDirect3DDevice8 *iface,
        UINT stream, IDirect3DVertexBuffer8 **buffer_out, UINT *stride_out)
{
    D8Device *device = device_from_iface(iface);
    if (stream >= D8WG_MAX_STREAMS || !buffer_out || !stride_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *stride_out = device->streams[stream].stride;
    *buffer_out = device->streams[stream].buffer
            ? &device->streams[stream].buffer->iface : NULL;
    if (*buffer_out)
        IDirect3DVertexBuffer8_AddRef(*buffer_out);
    return D3D_OK;
}

static HRESULT WINAPI device_set_indices(IDirect3DDevice8 *iface,
        IDirect3DIndexBuffer8 *buffer_iface, UINT base_vertex_index)
{
    D8Device *device = device_from_iface(iface);
    D8IndexBuffer *buffer = buffer_iface ? ib_from_iface(buffer_iface) : NULL;
    D9WGSetIndices command;

    if (base_vertex_index > 0x7FFFFFFFu
            || (buffer && (buffer_iface->lpVtbl != &g_ib_vtbl
                || buffer->device != device)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.indices_mask = TRUE;
    if (device->index_buffer == buffer
            && device->base_vertex_index == base_vertex_index)
        return D3D_OK;
    if (buffer)
        IDirect3DIndexBuffer8_AddRef(buffer_iface);
    if (device->index_buffer)
        IDirect3DIndexBuffer8_Release(&device->index_buffer->iface);
    device->index_buffer = buffer;
    device->base_vertex_index = base_vertex_index;
    command.device_handle = device->handle;
    command.buffer_handle = buffer ? buffer->handle : 0;
    /* base_vertex_index is not part of D3D9's SetIndices; it rides every
     * DrawIndexedPrimitive instead (see device_draw_indexed). */
    return emit_command(D9WG_OP_SET_INDICES, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_indices(IDirect3DDevice8 *iface,
        IDirect3DIndexBuffer8 **buffer_out, UINT *base_vertex_index_out)
{
    D8Device *device = device_from_iface(iface);
    if (!buffer_out || !base_vertex_index_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *buffer_out = device->index_buffer ? &device->index_buffer->iface : NULL;
    *base_vertex_index_out = device->base_vertex_index;
    if (*buffer_out)
        IDirect3DIndexBuffer8_AddRef(*buffer_out);
    return D3D_OK;
}

/*
 * Binds a D3D8 vertex shader: its declaration, its code, and any constants
 * D3DVSD_CONST embedded in the declaration.
 *
 * D3D8 applies declaration constants when the shader is set rather than when
 * it is created, so they are replayed here on every bind. That is what makes
 * a shader whose declaration seeds c0..cN behave the same on the second bind
 * as on the first, even after another shader has overwritten those registers.
 */
static BOOL emit_bind_vertex_shader(D8Device *device, D8Shader *shader)
{
    D9WGSetVertexDeclaration declaration;

    if (!shader)
        return FALSE;
    declaration.device_handle = device->handle;
    declaration.declaration_handle = shader->declaration_handle;
    if (!emit_command(D9WG_OP_SET_VERTEX_DECLARATION, &declaration,
            sizeof(declaration)))
        return FALSE;
    if (!emit_set_shader(D9WG_OP_SET_VERTEX_SHADER, device, shader->handle))
        return FALSE;
    if (shader->const_vector_count
            && !emit_set_shader_constant(D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F,
                    device, shader->const_start,
                    (const float *)shader->const_data,
                    shader->const_vector_count))
        return FALSE;
    return TRUE;
}

static HRESULT WINAPI device_set_vertex_shader(IDirect3DDevice8 *iface,
        DWORD handle)
{
    D8Device *device = device_from_iface(iface);
    /* D3D8 shader handles always carry bit 0; raw FVF tokens never do. */
    if (handle & 1u) {
        if (!find_shader(device->vertex_shaders, handle))
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        if (device->recording_state_block)
            device->recording_state_block->state.vertex_shader_mask = TRUE;
        if (device->vertex_shader == handle)
            return D3D_OK;
        device->vertex_shader = handle;
        return emit_bind_vertex_shader(device,
                find_shader(device->vertex_shaders, handle))
                ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
    }
    {
        if (device->recording_state_block)
            device->recording_state_block->state.vertex_shader_mask = TRUE;
        if (device->vertex_shader == handle)
            return D3D_OK;
        device->vertex_shader = handle;
        /*
         * D3D8 overloads SetVertexShader: an even value is an FVF and also
         * switches the device back to fixed-function vertex processing.
         * D9WG keeps the D3D9-shaped shader and declaration states separate,
         * so SET_FVF alone leaves the previously bound programmable shader
         * alive. 3DMark2001 alternates those two paths between scene passes;
         * the stale shader then reads inputs the FVF does not provide and the
         * host correctly drops the draw. Explicitly unbind it as part of the
         * D3D8-to-D3D9 translation.
         */
        return emit_set_shader(D9WG_OP_SET_VERTEX_SHADER, device, 0)
                && emit_set_fvf(device, handle)
                ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
    }
}

static HRESULT WINAPI device_get_vertex_shader(IDirect3DDevice8 *iface,
        DWORD *handle)
{
    if (!handle)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *handle = device_from_iface(iface)->vertex_shader;
    return D3D_OK;
}

static HRESULT WINAPI device_draw_primitive(IDirect3DDevice8 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT start_vertex,
        UINT primitive_count)
{
    D8Device *device = device_from_iface(iface);
    D9WGDrawPrimitive command;
    UINT vertex_count;
    UINT available_vertices;
    if (!device->streams[0].buffer || !device->streams[0].stride
            || device->streams[0].buffer->locked
            || !device->vertex_shader || !primitive_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    available_vertices = device->streams[0].buffer->length
            / device->streams[0].stride;
    if (start_vertex > available_vertices
            || vertex_count > available_vertices - start_vertex)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.start_vertex = start_vertex;
    command.primitive_count = primitive_count;
    return emit_command(D9WG_OP_DRAW_PRIMITIVE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_draw_indexed(IDirect3DDevice8 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT min_vertex_index,
        UINT vertex_count, UINT start_index, UINT primitive_count)
{
    D8Device *device = device_from_iface(iface);
    D9WGDrawIndexedPrimitive command;
    UINT index_count;
    UINT index_size;
    UINT available_indices;
    UINT available_vertices;
    UINT first_vertex;

    if (!device->streams[0].buffer || !device->streams[0].stride
            || device->streams[0].buffer->locked || !device->index_buffer
            || device->index_buffer->locked || !device->vertex_shader
            || !primitive_count || !vertex_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &index_count))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    index_size = device->index_buffer->format == D3DFMT_INDEX16 ? 2u : 4u;
    available_indices = device->index_buffer->length / index_size;
    if (start_index > available_indices
            || index_count > available_indices - start_index)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    available_vertices = device->streams[0].buffer->length
            / device->streams[0].stride;
    if (device->base_vertex_index > 0xFFFFFFFFu - min_vertex_index)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    first_vertex = device->base_vertex_index + min_vertex_index;
    if (first_vertex > available_vertices
            || vertex_count > available_vertices - first_vertex)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.base_vertex_index = (int32_t)device->base_vertex_index;
    command.min_vertex_index = min_vertex_index;
    command.vertex_count = vertex_count;
    command.start_index = start_index;
    command.primitive_count = primitive_count;
    return emit_command(D9WG_OP_DRAW_INDEXED_PRIMITIVE,
            &command, sizeof(command)) ? D3D_OK
            : D3DERR_DRIVERINTERNALERROR;
}

static void clear_stream_zero_after_up(D8Device *device)
{
    D8VertexBuffer *old = device->streams[0].buffer;
    device->streams[0].buffer = NULL;
    device->streams[0].stride = 0;
    if (old)
        IDirect3DVertexBuffer8_Release(&old->iface);
}

static void clear_indices_after_indexed_up(D8Device *device)
{
    D8IndexBuffer *old = device->index_buffer;
    device->index_buffer = NULL;
    device->base_vertex_index = 0;
    if (old)
        IDirect3DIndexBuffer8_Release(&old->iface);
}

static HRESULT WINAPI device_draw_up(IDirect3DDevice8 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *vertex_data, UINT stride)
{
    D8Device *device = device_from_iface(iface);
    D9WGDrawPrimitiveUP command;
    UINT vertex_count;
    UINT vertex_bytes;
    BOOL result;

    if (!vertex_data || !stride || !device->vertex_shader || !primitive_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count)
            || !multiply_u32(vertex_count, stride, &vertex_bytes))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.primitive_count = primitive_count;
    command.stride = stride;
    command.vertex_count = vertex_count;
    command.vertex_bytes = vertex_bytes;
    result = emit_draw_primitive_up(&command, vertex_data);
    if (result)
        clear_stream_zero_after_up(device);
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI device_draw_indexed_up(IDirect3DDevice8 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT min_vertex_index,
        UINT vertex_count, UINT primitive_count, const void *index_data,
        D3DFORMAT index_format, const void *vertex_data, UINT stride)
{
    D8Device *device = device_from_iface(iface);
    D9WGDrawIndexedPrimitiveUP command;
    UINT index_count;
    UINT index_size;
    UINT vertex_elements;
    BOOL result;

    if (!index_data || !vertex_data || !stride || !device->vertex_shader
            || !primitive_count || !vertex_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &index_count))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (index_format == D3DFMT_INDEX16)
        index_size = 2;
    else if (index_format == D3DFMT_INDEX32)
        index_size = 4;
    else
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (min_vertex_index > 0xFFFFFFFFu - vertex_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    vertex_elements = min_vertex_index + vertex_count;
    ZeroMemory(&command, sizeof(command));
    if (!multiply_u32(index_count, index_size, &command.index_bytes)
            || !multiply_u32(vertex_elements, stride,
                    &command.vertex_bytes))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.min_vertex_index = min_vertex_index;
    command.vertex_count = vertex_count;
    command.primitive_count = primitive_count;
    command.index_format = index_format;
    command.stride = stride;
    command.index_count = index_count;
    result = emit_draw_indexed_primitive_up(&command, index_data,
            vertex_data);
    if (result) {
        clear_stream_zero_after_up(device);
        clear_indices_after_indexed_up(device);
    }
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI device_validate(IDirect3DDevice8 *iface, DWORD *passes)
{
    (void)iface;
    if (!passes)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *passes = 1;
    return D3D_OK;
}

static HRESULT WINAPI vb_query_interface(IDirect3DVertexBuffer8 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DVertexBuffer8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DVertexBuffer8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI vb_add_ref(IDirect3DVertexBuffer8 *iface)
{
    return (ULONG)InterlockedIncrement(&vb_from_iface(iface)->refcount);
}

static ULONG WINAPI vb_release(IDirect3DVertexBuffer8 *iface)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D8VertexBuffer **link = &buffer->device->vertex_buffers;
        D9WGDestroyResource destroy;
        while (*link && *link != buffer)
            link = &(*link)->next_device_resource;
        if (*link) *link = buffer->next_device_resource;
        destroy.resource_handle = buffer->handle;
        destroy.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        device_child_release(buffer->device);
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    return refs;
}

static HRESULT WINAPI vb_get_device(IDirect3DVertexBuffer8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    if (!device_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &buffer->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI vb_set_private_data(IDirect3DVertexBuffer8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    (void)iface; (void)guid; (void)data; (void)size; (void)flags;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}

static HRESULT WINAPI vb_get_private_data(IDirect3DVertexBuffer8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    (void)iface; (void)guid; (void)data; (void)size;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static HRESULT WINAPI vb_free_private_data(IDirect3DVertexBuffer8 *iface,
        REFGUID guid)
{
    (void)iface; (void)guid;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static DWORD WINAPI vb_set_priority(IDirect3DVertexBuffer8 *iface,
        DWORD priority)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    DWORD old = buffer->priority;
    buffer->priority = priority;
    return old;
}

static DWORD WINAPI vb_get_priority(IDirect3DVertexBuffer8 *iface)
{
    return vb_from_iface(iface)->priority;
}

static void WINAPI vb_preload(IDirect3DVertexBuffer8 *iface)
{
    (void)iface;
}

static D3DRESOURCETYPE WINAPI vb_get_type(IDirect3DVertexBuffer8 *iface)
{
    (void)iface;
    return D3DRTYPE_VERTEXBUFFER;
}

static HRESULT WINAPI vb_lock(IDirect3DVertexBuffer8 *iface, UINT offset,
        UINT size, BYTE **data_out, DWORD flags)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & D3DLOCK_DISCARD) && (flags & D3DLOCK_NOOVERWRITE))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))
            && !(buffer->usage & D3DUSAGE_DYNAMIC))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & D3DLOCK_READONLY)
            && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD)
        ZeroMemory(buffer->shadow, buffer->length);
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI vb_unlock(IDirect3DVertexBuffer8 *iface)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY) ||
            emit_buffer_update(buffer->handle, buffer->lock_offset,
            buffer->shadow + buffer->lock_offset, buffer->lock_size,
            buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI vb_get_desc(IDirect3DVertexBuffer8 *iface,
        D3DVERTEXBUFFER_DESC *desc)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    if (!desc)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    desc->FVF = buffer->fvf;
    return D3D_OK;
}

static HRESULT WINAPI ib_query_interface(IDirect3DIndexBuffer8 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DIndexBuffer8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DIndexBuffer8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI ib_add_ref(IDirect3DIndexBuffer8 *iface)
{
    return (ULONG)InterlockedIncrement(&ib_from_iface(iface)->refcount);
}

static ULONG WINAPI ib_release(IDirect3DIndexBuffer8 *iface)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D8IndexBuffer **link = &buffer->device->index_buffers;
        D9WGDestroyResource destroy;
        while (*link && *link != buffer)
            link = &(*link)->next_device_resource;
        if (*link) *link = buffer->next_device_resource;
        destroy.resource_handle = buffer->handle;
        destroy.resource_kind = D9WG_RESOURCE_BUFFER_INDEX;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        device_child_release(buffer->device);
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    return refs;
}

static HRESULT WINAPI ib_get_device(IDirect3DIndexBuffer8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    if (!device_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &buffer->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI ib_set_private_data(IDirect3DIndexBuffer8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    (void)iface; (void)guid; (void)data; (void)size; (void)flags;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}

static HRESULT WINAPI ib_get_private_data(IDirect3DIndexBuffer8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    (void)iface; (void)guid; (void)data; (void)size;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static HRESULT WINAPI ib_free_private_data(IDirect3DIndexBuffer8 *iface,
        REFGUID guid)
{
    (void)iface; (void)guid;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static DWORD WINAPI ib_set_priority(IDirect3DIndexBuffer8 *iface,
        DWORD priority)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    DWORD old = buffer->priority;
    buffer->priority = priority;
    return old;
}

static DWORD WINAPI ib_get_priority(IDirect3DIndexBuffer8 *iface)
{
    return ib_from_iface(iface)->priority;
}

static void WINAPI ib_preload(IDirect3DIndexBuffer8 *iface)
{
    (void)iface;
}

static D3DRESOURCETYPE WINAPI ib_get_type(IDirect3DIndexBuffer8 *iface)
{
    (void)iface;
    return D3DRTYPE_INDEXBUFFER;
}

static HRESULT WINAPI ib_lock(IDirect3DIndexBuffer8 *iface, UINT offset,
        UINT size, BYTE **data_out, DWORD flags)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & D3DLOCK_DISCARD) && (flags & D3DLOCK_NOOVERWRITE))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))
            && !(buffer->usage & D3DUSAGE_DYNAMIC))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((flags & D3DLOCK_READONLY)
            && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD)
        ZeroMemory(buffer->shadow, buffer->length);
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI ib_unlock(IDirect3DIndexBuffer8 *iface)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY) ||
            emit_buffer_update(buffer->handle, buffer->lock_offset,
            buffer->shadow + buffer->lock_offset, buffer->lock_size,
            buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI ib_get_desc(IDirect3DIndexBuffer8 *iface,
        D3DINDEXBUFFER_DESC *desc)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    if (!desc)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = buffer->format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    return D3D_OK;
}

static UINT full_mip_level_count(UINT width, UINT height)
{
    UINT levels = 1;
    while (width > 1 || height > 1) {
        if (width > 1)
            width >>= 1;
        if (height > 1)
            height >>= 1;
        ++levels;
    }
    return levels;
}

static HRESULT WINAPI device_create_texture(IDirect3DDevice8 *iface,
        UINT width, UINT height, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    D8Texture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!width || !height || width > 4096 || height > 4096
            || !supported_texture_format(format)
            || (usage & D3DUSAGE_DEPTHSTENCIL)
            || (usage & D3DUSAGE_RENDERTARGET
                && (pool != D3DPOOL_DEFAULT || levels != 1
                    || !supported_render_target_format(format)))
            || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(width, height);
    if (!levels)
        levels = full_levels;
    if (levels > full_levels)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8Texture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    for (level = 0; level < levels; ++level) {
        D8TextureLevel *level_data = &texture->levels[level];
        if (!allocate_texture_level(level_data, format, level_width,
                level_height, 1u))
            goto allocation_failed;
        if (level_width > 1)
            level_width >>= 1;
        if (level_height > 1)
            level_height >>= 1;
    }

    if (!emit_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        if (texture->levels[level].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}

/*
 * A depth-stencil surface with a host resource of its own.
 *
 * D3D8's CreateDepthStencilSurface takes a size that need not match the
 * device's implicit depth buffer, and render-to-texture leans on that: an app
 * renders a reflection into a 1024x512 target and creates a depth surface to
 * match. Aliasing every depth surface onto the implicit buffer left the host
 * depth-testing such a pass against the 800x600 buffer negotiated at
 * CreateDevice; it saw the mismatch and dropped depth testing for the whole
 * pass, so nothing occluded anything -- 3DMark 2001's advanced pixel-shader
 * test reported it as "the bound depth surface is smaller than the render
 * target".
 *
 * The host already models a depth surface as a texture carrying
 * D3DUSAGE_DEPTHSTENCIL -- that is how the D3D9 frontend creates one -- so
 * this is the same CREATE_TEXTURE_2D every other resource uses. It never gets
 * a shadow: no D3D8 entry point can read depth back through this DLL, so
 * there is nothing to mirror guest-side. device_create_texture() keeps
 * refusing the usage, because an app reaching CreateTexture with it is asking
 * for a depth *texture* to sample, which this path does not provide.
 */
static HRESULT create_depth_stencil_texture(D8Device *device, UINT width,
        UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
        D8Texture **texture_out)
{
    D8Texture *texture;
    *texture_out = NULL;
    if (!width || !height || width > 8192 || height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    texture = (D8Texture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->level_count = 1;
    texture->usage = D3DUSAGE_DEPTHSTENCIL;
    texture->format = format;
    texture->pool = D3DPOOL_DEFAULT;
    texture->multisample_type = ms;
    texture->levels[0].width = width;
    texture->levels[0].height = height;
    texture->levels[0].depth = 1;
    texture->levels[0].row_pitch = width * 4u;
    texture->levels[0].row_count = height;
    device_child_add_ref(device);
    if (!emit_texture_create(device, texture)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, texture->levels);
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;
    *texture_out = texture;
    return D3D_OK;
}

/*
 * Locks one mip level's shadow. Shared by 2D textures, cube faces and volume
 * slices -- all three are a D8TextureLevel and differ only in which resource
 * and `z` the matching unlock uploads to.
 */
static HRESULT lock_texture_level(D8TextureLevel *level_data, D3DFORMAT format,
        BOOL lockable, D3DLOCKED_RECT *locked_rect, const RECT *rect,
        DWORD flags)
{
    RECT area;
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT block_x;
    UINT block_y;

    if (!locked_rect || !lockable)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (level_data->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (rect) {
        area = *rect;
    } else {
        SetRect(&area, 0, 0, (int)level_data->width,
                (int)level_data->height);
    }
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > level_data->width
            || (UINT)area.bottom > level_data->height
            || !texture_format_layout(format, &block_width,
                    &block_height, &block_bytes))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (block_width > 1
            && (((UINT)area.left % block_width)
                || ((UINT)area.top % block_height)
                || ((UINT)area.right != level_data->width
                    && (UINT)area.right % block_width)
                || ((UINT)area.bottom != level_data->height
                    && (UINT)area.bottom % block_height)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    block_x = (UINT)area.left / block_width;
    block_y = (UINT)area.top / block_height;
    level_data->lock_rect = area;
    level_data->lock_flags = flags;
    level_data->locked = TRUE;
    locked_rect->Pitch = (INT)level_data->row_pitch;
    locked_rect->pBits = level_data->shadow
            + block_y * level_data->row_pitch + block_x * block_bytes;
    return D3D_OK;
}

static HRESULT unlock_texture_level(uint32_t handle, D3DFORMAT format,
        D8TextureLevel *level_data, UINT level, UINT z)
{
    BOOL result = TRUE;

    if (!level_data->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!(level_data->lock_flags & D3DLOCK_READONLY))
        result = emit_level_update(handle, format, level_data, level, z,
                &level_data->lock_rect);
    level_data->locked = FALSE;
    level_data->lock_flags = 0;
    ZeroMemory(&level_data->lock_rect, sizeof(level_data->lock_rect));
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT texture_lock_level(D8Texture *texture, UINT level,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    if (level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* A depth-stencil level carries no shadow, and D3D8 only ever allows
     * locking one through D3DFMT_D16_LOCKABLE, which this DLL does not
     * advertise (see the README). */
    if (texture->usage & D3DUSAGE_DEPTHSTENCIL)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return lock_texture_level(&texture->levels[level], texture->format,
            !(texture->usage & D3DUSAGE_RENDERTARGET)
                || texture->lockable_render_target,
            locked_rect, rect, flags);
}

static HRESULT texture_unlock_level(D8Texture *texture, UINT level)
{
    if (level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return unlock_texture_level(texture->handle, texture->format,
            &texture->levels[level], level, 0);
}

static HRESULT WINAPI texture_query_interface(IDirect3DTexture8 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
            && !guid_equal(iid, &IID_IDirect3DTexture8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DTexture8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI texture_add_ref(IDirect3DTexture8 *iface)
{
    return (ULONG)InterlockedIncrement(&texture_from_iface(iface)->refcount);
}

static ULONG WINAPI texture_release(IDirect3DTexture8 *iface)
{
    D8Texture *texture = texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D8Texture **link = &texture->device->texture_resources;
        D9WGDestroyResource destroy;
        UINT level;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (level = 0; level < texture->level_count; ++level)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
    }
    return refs;
}

static HRESULT WINAPI texture_get_device(IDirect3DTexture8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8Texture *texture = texture_from_iface(iface);
    if (!device_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &texture->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI texture_set_private_data(IDirect3DTexture8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    (void)iface; (void)guid; (void)data; (void)size; (void)flags;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}

static HRESULT WINAPI texture_get_private_data(IDirect3DTexture8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    (void)iface; (void)guid; (void)data; (void)size;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static HRESULT WINAPI texture_free_private_data(IDirect3DTexture8 *iface,
        REFGUID guid)
{
    (void)iface; (void)guid;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static DWORD WINAPI texture_set_priority(IDirect3DTexture8 *iface,
        DWORD priority)
{
    D8Texture *texture = texture_from_iface(iface);
    DWORD old = texture->priority;
    texture->priority = priority;
    return old;
}

static DWORD WINAPI texture_get_priority(IDirect3DTexture8 *iface)
{
    return texture_from_iface(iface)->priority;
}

static void WINAPI texture_preload(IDirect3DTexture8 *iface)
{
    (void)iface;
}

static D3DRESOURCETYPE WINAPI texture_get_type(IDirect3DTexture8 *iface)
{
    (void)iface;
    return D3DRTYPE_TEXTURE;
}

static DWORD WINAPI texture_set_lod(IDirect3DTexture8 *iface, DWORD lod)
{
    D8Texture *texture = texture_from_iface(iface);
    DWORD old = texture->lod;
    if (lod >= texture->level_count)
        lod = texture->level_count - 1;
    texture->lod = lod;
    return old;
}

static DWORD WINAPI texture_get_lod(IDirect3DTexture8 *iface)
{
    return texture_from_iface(iface)->lod;
}

static DWORD WINAPI texture_get_level_count(IDirect3DTexture8 *iface)
{
    return texture_from_iface(iface)->level_count;
}

static HRESULT WINAPI texture_get_level_desc(IDirect3DTexture8 *iface,
        UINT level, D3DSURFACE_DESC *desc)
{
    D8Texture *texture = texture_from_iface(iface);
    D8TextureLevel *level_data;
    if (!desc || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->Size = level_data->byte_count;
    desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    return D3D_OK;
}

static HRESULT WINAPI texture_get_surface_level(IDirect3DTexture8 *iface,
        UINT level, IDirect3DSurface8 **surface_out)
{
    D8Texture *texture = texture_from_iface(iface);
    D8Surface *surface;
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    surface = (D8Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->texture = texture;
    surface->device = texture->device;
    surface->level = level;
    /* What SetRenderTarget tests to tell a colour target from a depth one. */
    surface->depth_stencil = (texture->usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    IDirect3DTexture8_AddRef(iface);
    *surface_out = &surface->iface;
    return D3D_OK;
}

static HRESULT WINAPI texture_lock_rect(IDirect3DTexture8 *iface, UINT level,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    return texture_lock_level(texture_from_iface(iface), level, locked_rect,
            rect, flags);
}

static HRESULT WINAPI texture_unlock_rect(IDirect3DTexture8 *iface,
        UINT level)
{
    return texture_unlock_level(texture_from_iface(iface), level);
}

static HRESULT WINAPI texture_add_dirty_rect(IDirect3DTexture8 *iface,
        const RECT *rect)
{
    D8Texture *texture = texture_from_iface(iface);
    if (rect && (rect->left < 0 || rect->top < 0
            || rect->right <= rect->left || rect->bottom <= rect->top
            || (UINT)rect->right > texture->width
            || (UINT)rect->bottom > texture->height))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return D3D_OK;
}

static HRESULT WINAPI surface_query_interface(IDirect3DSurface8 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DSurface8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DSurface8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI surface_add_ref(IDirect3DSurface8 *iface)
{
    return (ULONG)InterlockedIncrement(&surface_from_iface(iface)->refcount);
}

static ULONG WINAPI surface_release(IDirect3DSurface8 *iface)
{
    D8Surface *surface = surface_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&surface->refcount);
    if (!refs) {
        if (surface->reset_blocker && surface->device
                && surface->device->implicit_surface_count)
            --surface->device->implicit_surface_count;
        if (surface->texture)
            IDirect3DTexture8_Release(&surface->texture->iface);
        else if (surface->cube)
            IDirect3DCubeTexture8_Release(&surface->cube->iface);
        else if (surface->device)
            device_child_release(surface->device);
        HeapFree(GetProcessHeap(), 0, surface->shadow);
        HeapFree(GetProcessHeap(), 0, surface);
    }
    return refs;
}

static HRESULT WINAPI surface_get_device(IDirect3DSurface8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8Surface *surface = surface_from_iface(iface);
    if (!device_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = surface->texture ? &surface->texture->device->iface
            : &surface->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI surface_set_private_data(IDirect3DSurface8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    (void)iface; (void)guid; (void)data; (void)size; (void)flags;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}

static HRESULT WINAPI surface_get_private_data(IDirect3DSurface8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    (void)iface; (void)guid; (void)data; (void)size;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static HRESULT WINAPI surface_free_private_data(IDirect3DSurface8 *iface,
        REFGUID guid)
{
    (void)iface; (void)guid;
    return D8WG_TRACE_ERROR(D3DERR_NOTFOUND);
}

static HRESULT WINAPI surface_get_container(IDirect3DSurface8 *iface,
        REFIID iid, void **container)
{
    D8Surface *surface = surface_from_iface(iface);
    if (!container)
        return D8WG_TRACE_ERROR(E_POINTER);
    *container = NULL;
    if (surface->texture) {
        if (!iid || (!iid_is_unknown(iid)
                && !guid_equal(iid, &IID_IDirect3DResource8)
                && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
                && !guid_equal(iid, &IID_IDirect3DTexture8)))
            return D8WG_TRACE_ERROR(E_NOINTERFACE);
        *container = &surface->texture->iface;
        IDirect3DTexture8_AddRef(&surface->texture->iface);
    } else if (surface->cube) {
        if (!iid || (!iid_is_unknown(iid)
                && !guid_equal(iid, &IID_IDirect3DResource8)
                && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
                && !guid_equal(iid, &IID_IDirect3DCubeTexture8)))
            return D8WG_TRACE_ERROR(E_NOINTERFACE);
        *container = &surface->cube->iface;
        IDirect3DCubeTexture8_AddRef(&surface->cube->iface);
    } else {
        if (!iid || (!iid_is_unknown(iid)
                && !guid_equal(iid, &IID_IDirect3DDevice8)))
            return D8WG_TRACE_ERROR(E_NOINTERFACE);
        *container = &surface->device->iface;
        IDirect3DDevice8_AddRef(&surface->device->iface);
    }
    return S_OK;
}

static HRESULT WINAPI surface_get_desc(IDirect3DSurface8 *iface,
        D3DSURFACE_DESC *desc)
{
    D8Surface *surface = surface_from_iface(iface);
    if (!desc) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (surface->texture)
        return texture_get_level_desc(&surface->texture->iface,
                surface->level, desc);
    if (surface->cube)
        return cube_get_level_desc(&surface->cube->iface, surface->level,
                desc);
    *desc = surface->desc;
    return D3D_OK;
}

static HRESULT WINAPI surface_lock_rect(IDirect3DSurface8 *iface,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    D8Surface *surface = surface_from_iface(iface);
    RECT area;
    if (surface->texture)
        return texture_lock_level(surface->texture, surface->level,
                locked_rect, rect, flags);
    if (surface->cube)
        return cube_lock_face(surface->cube,
                (D3DCUBEMAP_FACES)surface->face, surface->level,
                locked_rect, rect, flags);
    if (!locked_rect || !surface->shadow || surface->locked
            || surface->backbuffer || surface->depth_stencil)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (rect) area = *rect;
    else SetRect(&area, 0, 0, (int)surface->desc.Width,
            (int)surface->desc.Height);
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > surface->desc.Width
            || (UINT)area.bottom > surface->desc.Height)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    surface->locked = TRUE;
    surface->lock_rect = area;
    surface->lock_flags = flags;
    locked_rect->Pitch = surface->row_pitch;
    locked_rect->pBits = surface->shadow + area.top * surface->row_pitch
            + area.left * (surface->row_pitch / surface->desc.Width);
    return D3D_OK;
}

static HRESULT WINAPI surface_unlock_rect(IDirect3DSurface8 *iface)
{
    D8Surface *surface = surface_from_iface(iface);
    if (surface->texture)
        return texture_unlock_level(surface->texture, surface->level);
    if (surface->cube)
        return cube_unlock_face(surface->cube,
                (D3DCUBEMAP_FACES)surface->face, surface->level);
    if (!surface->locked) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    surface->locked = FALSE;
    surface->lock_flags = 0;
    ZeroMemory(&surface->lock_rect, sizeof(surface->lock_rect));
    return D3D_OK;
}

static HRESULT WINAPI device_get_texture(IDirect3DDevice8 *iface,
        DWORD stage, IDirect3DBaseTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    if (!texture_out || stage >= D8WG_MAX_TEXTURE_STAGES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = device->textures[stage];
    if (*texture_out)
        IDirect3DBaseTexture8_AddRef(*texture_out);
    return D3D_OK;
}

/*
 * Resolves any of the three IDirect3DBaseTexture8 implementations to the host
 * resource handle behind it, rejecting a pointer that is not one of ours or
 * belongs to another device.
 *
 * The vtable check is the type tag: D3D8 has no QueryInterface-free way to ask
 * what kind of base texture a pointer is, and trusting the caller here would
 * turn a wrong pointer into a wild read rather than D3DERR_INVALIDCALL.
 */
static BOOL resolve_base_texture(D8Device *device,
        IDirect3DBaseTexture8 *texture_iface, uint32_t *handle_out)
{
    if (!texture_iface) {
        *handle_out = 0;
        return TRUE;
    }
    if (texture_iface->lpVtbl == (IDirect3DBaseTexture8Vtbl *)&g_texture_vtbl) {
        D8Texture *texture = (D8Texture *)texture_iface;
        if (texture->device != device) return FALSE;
        *handle_out = texture->handle;
        return TRUE;
    }
    if (texture_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_cube_texture_vtbl) {
        D8CubeTexture *texture = (D8CubeTexture *)texture_iface;
        if (texture->device != device) return FALSE;
        *handle_out = texture->handle;
        return TRUE;
    }
    if (texture_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_volume_texture_vtbl) {
        D8VolumeTexture *texture = (D8VolumeTexture *)texture_iface;
        if (texture->device != device) return FALSE;
        *handle_out = texture->handle;
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI device_set_texture(IDirect3DDevice8 *iface,
        DWORD stage, IDirect3DBaseTexture8 *texture_iface)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetTexture command;
    uint32_t handle;

    if (stage >= D8WG_MAX_TEXTURE_STAGES
            || !resolve_base_texture(device, texture_iface, &handle))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.texture_mask[stage] = 1;
    if (device->textures[stage] == texture_iface)
        return D3D_OK;
    if (texture_iface)
        IDirect3DBaseTexture8_AddRef(texture_iface);
    if (device->textures[stage])
        IDirect3DBaseTexture8_Release(device->textures[stage]);
    device->textures[stage] = texture_iface;
    command.device_handle = device->handle;
    command.stage = stage;
    command.texture_handle = handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_TEXTURE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

/*
 * UpdateTexture for the cube and volume kinds. Both mirror the 2D path: copy
 * the source's shadow into the destination's and upload every subresource.
 * A mismatched pair -- one cube and one 2D texture, say -- reaches here and is
 * refused, which is what D3D8 does too.
 */
static HRESULT update_cube_texture(D8Device *device,
        IDirect3DCubeTexture8 *source_iface,
        IDirect3DCubeTexture8 *destination_iface)
{
    D8CubeTexture *source = (D8CubeTexture *)source_iface;
    D8CubeTexture *destination = (D8CubeTexture *)destination_iface;
    UINT face;
    UINT level;

    if (source->iface.lpVtbl != &g_cube_texture_vtbl
            || destination->iface.lpVtbl != &g_cube_texture_vtbl
            || source->device != device || destination->device != device
            || source->format != destination->format
            || source->edge_length != destination->edge_length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    for (face = 0; face < 6u; ++face) {
        for (level = 0; level < source->level_count
                && level < destination->level_count; ++level) {
            D8TextureLevel *from =
                    cube_level(source, (D3DCUBEMAP_FACES)face, level);
            D8TextureLevel *to =
                    cube_level(destination, (D3DCUBEMAP_FACES)face, level);
            RECT full;
            if (from->locked || to->locked)
                return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
            CopyMemory(to->shadow, from->shadow, to->byte_count);
            SetRect(&full, 0, 0, (int)to->width, (int)to->height);
            if (!emit_level_update(destination->handle, destination->format,
                    to, level, face, &full))
                return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
        }
    }
    return D3D_OK;
}

static HRESULT update_volume_texture(D8Device *device,
        IDirect3DVolumeTexture8 *source_iface,
        IDirect3DVolumeTexture8 *destination_iface)
{
    D8VolumeTexture *source = (D8VolumeTexture *)source_iface;
    D8VolumeTexture *destination = (D8VolumeTexture *)destination_iface;
    UINT level;

    if (source->iface.lpVtbl != &g_volume_texture_vtbl
            || destination->iface.lpVtbl != &g_volume_texture_vtbl
            || source->device != device || destination->device != device
            || source->format != destination->format
            || source->width != destination->width
            || source->height != destination->height
            || source->depth != destination->depth)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    for (level = 0; level < source->level_count
            && level < destination->level_count; ++level) {
        D8TextureLevel *to = &destination->levels[level];
        RECT full;
        if (source->levels[level].locked || to->locked)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        CopyMemory(to->shadow, source->levels[level].shadow, to->byte_count);
        SetRect(&full, 0, 0, (int)to->width, (int)to->height);
        if (!emit_volume_level_update(destination, level, &full, 0,
                to->depth))
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    return D3D_OK;
}

static HRESULT WINAPI device_update_texture(IDirect3DDevice8 *iface,
        IDirect3DBaseTexture8 *source_iface,
        IDirect3DBaseTexture8 *destination_iface)
{
    D8Device *device = device_from_iface(iface);
    D8Texture *source;
    D8Texture *destination;
    UINT level;

    if (!source_iface || !destination_iface)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* Both operands must be the same kind of base texture. The cube and
     * volume cases are handled first because the 2D path below reads fields
     * that only a D8Texture has. */
    if (source_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_cube_texture_vtbl
            || destination_iface->lpVtbl
                == (IDirect3DBaseTexture8Vtbl *)&g_cube_texture_vtbl)
        return update_cube_texture(device,
                (IDirect3DCubeTexture8 *)source_iface,
                (IDirect3DCubeTexture8 *)destination_iface);
    if (source_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_volume_texture_vtbl
            || destination_iface->lpVtbl
                == (IDirect3DBaseTexture8Vtbl *)&g_volume_texture_vtbl)
        return update_volume_texture(device,
                (IDirect3DVolumeTexture8 *)source_iface,
                (IDirect3DVolumeTexture8 *)destination_iface);
    source = (D8Texture *)source_iface;
    destination = (D8Texture *)destination_iface;
    if (source->iface.lpVtbl != &g_texture_vtbl
            || destination->iface.lpVtbl != &g_texture_vtbl
            || source->device != device || destination->device != device
            || source->format != destination->format
            || source->width != destination->width
            || source->height != destination->height)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    for (level = 0; level < source->level_count
            && level < destination->level_count; ++level) {
        RECT full;
        if (source->levels[level].locked || destination->levels[level].locked)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        CopyMemory(destination->levels[level].shadow,
                source->levels[level].shadow,
                destination->levels[level].byte_count);
        SetRect(&full, 0, 0, (int)destination->levels[level].width,
                (int)destination->levels[level].height);
        if (!emit_texture_update(destination, level, &full))
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    return D3D_OK;
}

static HRESULT create_standalone_surface(D8Device *device, UINT width,
        UINT height, D3DFORMAT format, DWORD usage, D3DPOOL pool,
        BOOL lockable, BOOL backbuffer, BOOL depth_stencil,
        BOOL reset_blocker, IDirect3DSurface8 **surface_out)
{
    D8Surface *surface;
    UINT block_width;
    UINT block_height;
    UINT pixel_bytes;
    UINT row_pitch;
    UINT row_count;
    UINT byte_count;
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (!width || !height || width > 8192 || height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!depth_stencil) {
        if (!texture_format_layout(format, &block_width, &block_height,
                &pixel_bytes)
                || (lockable && (block_width != 1 || block_height != 1))
                || !texture_level_layout(format, width, height,
                    &row_pitch, &row_count, &byte_count))
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (depth_stencil) {
        row_pitch = width * 4u;
        row_count = height;
        byte_count = row_pitch * row_count;
    }
    surface = (D8Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    if (lockable) {
        surface->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, byte_count);
        if (!surface->shadow) {
            HeapFree(GetProcessHeap(), 0, surface);
            return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        }
    }
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->device = device;
    device_child_add_ref(device);
    surface->row_pitch = row_pitch;
    surface->backbuffer = backbuffer;
    surface->depth_stencil = depth_stencil;
    surface->reset_blocker = reset_blocker;
    if (reset_blocker) ++device->implicit_surface_count;
    ZeroMemory(&surface->desc, sizeof(surface->desc));
    surface->desc.Format = format;
    surface->desc.Type = D3DRTYPE_SURFACE;
    surface->desc.Usage = usage;
    surface->desc.Pool = pool;
    surface->desc.Size = byte_count;
    surface->desc.MultiSampleType = D3DMULTISAMPLE_NONE;
    surface->desc.Width = width;
    surface->desc.Height = height;
    *surface_out = &surface->iface;
    return D3D_OK;
}

static HRESULT create_backbuffer_surface(D8Device *device,
        const D3DPRESENT_PARAMETERS *present, BOOL reset_blocker,
        IDirect3DSurface8 **surface_out)
{
    UINT width = present->BackBufferWidth ? present->BackBufferWidth
            : device->display_mode.Width;
    UINT height = present->BackBufferHeight ? present->BackBufferHeight
            : device->display_mode.Height;
    D3DFORMAT format = present->BackBufferFormat == D3DFMT_UNKNOWN
            ? device->display_mode.Format : present->BackBufferFormat;
    return create_standalone_surface(device, width, height, format,
            D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, FALSE, TRUE, FALSE,
            reset_blocker, surface_out);
}

static BYTE *surface_pixels(D8Surface *surface, UINT *pitch_out)
{
    if (surface->texture) {
        D8TextureLevel *level = &surface->texture->levels[surface->level];
        *pitch_out = level->row_pitch;
        return level->shadow;
    }
    if (surface->backbuffer) {
        *pitch_out = surface->device->front_shadow_pitch;
        return surface->device->front_shadow;
    }
    *pitch_out = surface->row_pitch;
    return surface->shadow;
}

static HRESULT copy_surface_rect(D8Surface *source, const RECT *source_rect,
        D8Surface *destination, const POINT *destination_point)
{
    D3DSURFACE_DESC source_desc;
    D3DSURFACE_DESC destination_desc;
    RECT area;
    POINT point;
    BYTE *source_data;
    BYTE *destination_data;
    UINT source_pitch;
    UINT destination_pitch;
    UINT block_width, block_height, pixel_bytes;
    UINT row;
    if (FAILED(surface_get_desc(&source->iface, &source_desc))
            || FAILED(surface_get_desc(&destination->iface,
                    &destination_desc))
            || (source_desc.Format != destination_desc.Format
                && !((source_desc.Format == D3DFMT_A8R8G8B8
                        || source_desc.Format == D3DFMT_X8R8G8B8)
                    && (destination_desc.Format == D3DFMT_A8R8G8B8
                        || destination_desc.Format == D3DFMT_X8R8G8B8)))
            || !texture_format_layout(source_desc.Format, &block_width,
                    &block_height, &pixel_bytes)
            || block_width != 1 || block_height != 1)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (source_rect) area = *source_rect;
    else SetRect(&area, 0, 0, (int)source_desc.Width,
            (int)source_desc.Height);
    if (destination_point) point = *destination_point;
    else point.x = point.y = 0;
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > source_desc.Width
            || (UINT)area.bottom > source_desc.Height || point.x < 0
            || point.y < 0
            || (UINT)(point.x + area.right - area.left)
                    > destination_desc.Width
            || (UINT)(point.y + area.bottom - area.top)
                    > destination_desc.Height)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    source_data = surface_pixels(source, &source_pitch);
    destination_data = surface_pixels(destination, &destination_pitch);
    if (!source_data || !destination_data) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    for (row = 0; row < (UINT)(area.bottom - area.top); ++row) {
        CopyMemory(destination_data + (point.y + row) * destination_pitch
                + point.x * pixel_bytes,
                source_data + (area.top + row) * source_pitch
                + area.left * pixel_bytes,
                (area.right - area.left) * pixel_bytes);
    }
    if (destination->texture) {
        RECT dirty;
        SetRect(&dirty, point.x, point.y,
                point.x + area.right - area.left,
                point.y + area.bottom - area.top);
        if (!emit_texture_update(destination->texture, destination->level,
                &dirty))
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    return D3D_OK;
}

/* Typed unsupported methods keep stdcall stack cleanup correct on 32-bit XP. */
#define DEV_STUB0(name) \
    static HRESULT WINAPI device_##name(IDirect3DDevice8 *iface) \
    { (void)iface; return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }
#define DEV_STUB(name, ...) \
    static HRESULT WINAPI device_##name(IDirect3DDevice8 *iface, __VA_ARGS__)

DEV_STUB(set_cursor_properties, UINT x, UINT y, IDirect3DSurface8 *surface)
{ (void)iface; (void)x; (void)y; (void)surface; return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }
static void WINAPI device_set_cursor_position(IDirect3DDevice8 *iface,
        UINT x, UINT y, DWORD flags)
{ (void)iface; (void)x; (void)y; (void)flags; }
static WINBOOL WINAPI device_show_cursor(IDirect3DDevice8 *iface, WINBOOL show)
{ (void)iface; (void)show; return FALSE; }

static HRESULT WINAPI swapchain_query_interface(IDirect3DSwapChain8 *iface,
        REFIID iid, void **object)
{
    if (!object) return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DSwapChain8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DSwapChain8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI swapchain_add_ref(IDirect3DSwapChain8 *iface)
{
    return (ULONG)InterlockedIncrement(&((D8SwapChain *)iface)->refcount);
}

static ULONG WINAPI swapchain_release(IDirect3DSwapChain8 *iface)
{
    D8SwapChain *chain = (D8SwapChain *)iface;
    ULONG refs = (ULONG)InterlockedDecrement(&chain->refcount);
    if (!refs) {
        if (chain->device->additional_swap_chain_count)
            --chain->device->additional_swap_chain_count;
        device_child_release(chain->device);
        HeapFree(GetProcessHeap(), 0, chain);
    }
    return refs;
}

static HRESULT WINAPI swapchain_present(IDirect3DSwapChain8 *iface,
        const RECT *source, const RECT *destination, HWND override_window,
        const RGNDATA *dirty_region)
{
    D8SwapChain *chain = (D8SwapChain *)iface;
    HWND window = override_window ? override_window : chain->present.hDeviceWindow;
    (void)source; (void)destination; (void)dirty_region;
    return emit_present_and_flush(chain->device, window)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI swapchain_get_backbuffer(IDirect3DSwapChain8 *iface,
        UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **surface_out)
{
    D8SwapChain *chain = (D8SwapChain *)iface;
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (index || type != D3DBACKBUFFER_TYPE_MONO)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return create_backbuffer_surface(chain->device, &chain->present, TRUE,
            surface_out);
}

static HRESULT WINAPI device_create_swapchain(IDirect3DDevice8 *iface,
        D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain8 **chain_out)
{
    D8Device *device = device_from_iface(iface);
    D8SwapChain *chain;
    if (!chain_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *chain_out = NULL;
    if (!parameters || !parameters->Windowed
            || !supported_multisample_type(parameters->MultiSampleType)
            || parameters->BackBufferCount > 1
            || parameters->BackBufferWidth > 8192
            || parameters->BackBufferHeight > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
            && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    chain = (D8SwapChain *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*chain));
    if (!chain) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    chain->iface.lpVtbl = &g_swapchain_vtbl;
    chain->refcount = 1;
    chain->device = device;
    chain->present = *parameters;
    device_child_add_ref(device);
    ++device->additional_swap_chain_count;
    *chain_out = &chain->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_get_backbuffer(IDirect3DDevice8 *iface, UINT index,
        D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **surface_out)
{
    D8Device *device = device_from_iface(iface);
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (index || type != D3DBACKBUFFER_TYPE_MONO)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return create_backbuffer_surface(device, &device->present, TRUE,
            surface_out);
}
DEV_STUB(get_raster_status, D3DRASTER_STATUS *s)
{ (void)iface; (void)s; return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }
static void WINAPI device_set_gamma(IDirect3DDevice8 *iface, DWORD flags,
        const D3DGAMMARAMP *ramp)
{ (void)iface; (void)flags; (void)ramp; }
static void WINAPI device_get_gamma(IDirect3DDevice8 *iface, D3DGAMMARAMP *ramp)
{ (void)iface; if (ramp) ZeroMemory(ramp, sizeof(*ramp)); }
static HRESULT WINAPI device_create_render_target(IDirect3DDevice8 *iface,
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
        WINBOOL lockable, IDirect3DSurface8 **surface_out)
{
    IDirect3DTexture8 *texture_iface = NULL;
    HRESULT hr;
    if (!surface_out || !supported_multisample_type(ms))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* A multisampled surface has no single value per pixel to hand back, so
     * D3D8 forbids locking one -- and so does this path, rather than returning
     * a lock that reads one arbitrary sample. */
    if (lockable && ms != D3DMULTISAMPLE_NONE)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    hr = device_create_texture(iface, width, height, 1,
            D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &texture_iface);
    if (FAILED(hr)) return D8WG_TRACE_ERROR(hr);
    texture_from_iface(texture_iface)->lockable_render_target = !!lockable;
    /* Set after creation and re-sent, because device_create_texture has no
     * multisample parameter of its own -- every other caller wants NONE. */
    if (ms != D3DMULTISAMPLE_NONE) {
        texture_from_iface(texture_iface)->multisample_type = ms;
        if (!emit_texture_create(device_from_iface(iface),
                texture_from_iface(texture_iface))) {
            IDirect3DTexture8_Release(texture_iface);
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
        }
    }
    hr = IDirect3DTexture8_GetSurfaceLevel(texture_iface, 0, surface_out);
    IDirect3DTexture8_Release(texture_iface);
    return D8WG_TRACE_ERROR(hr);
}

static HRESULT WINAPI device_create_depth_surface(IDirect3DDevice8 *iface,
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
        IDirect3DSurface8 **surface_out)
{
    D8Texture *texture = NULL;
    HRESULT hr;
    if (!surface_out || !supported_depth_stencil_format(format)
            || !supported_multisample_type(ms))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    hr = create_depth_stencil_texture(device_from_iface(iface), width, height,
            format, ms, &texture);
    if (FAILED(hr)) return hr;
    hr = IDirect3DTexture8_GetSurfaceLevel(&texture->iface, 0, surface_out);
    IDirect3DTexture8_Release(&texture->iface);
    return D8WG_TRACE_ERROR(hr);
}

static HRESULT WINAPI device_create_image_surface(IDirect3DDevice8 *iface,
        UINT width, UINT height, D3DFORMAT format,
        IDirect3DSurface8 **surface_out)
{
    if (!surface_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    return create_standalone_surface(device_from_iface(iface), width, height,
            format, 0, D3DPOOL_SYSTEMMEM, TRUE, FALSE, FALSE, FALSE,
            surface_out);
}

static HRESULT WINAPI device_copy_rects(IDirect3DDevice8 *iface,
        IDirect3DSurface8 *source_iface, const RECT *rects, UINT count,
        IDirect3DSurface8 *destination_iface, const POINT *points)
{
    D8Device *device = device_from_iface(iface);
    D8Surface *source;
    D8Surface *destination;
    UINT index;
    if (!source_iface || !destination_iface
            || source_iface->lpVtbl != &g_surface_vtbl
            || destination_iface->lpVtbl != &g_surface_vtbl
            || (count && (!rects || !points)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    source = surface_from_iface(source_iface);
    destination = surface_from_iface(destination_iface);
    if ((source->texture ? source->texture->device : source->device) != device
            || (destination->texture ? destination->texture->device
                    : destination->device) != device)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!count) return copy_surface_rect(source, NULL, destination, NULL);
    for (index = 0; index < count; ++index) {
        HRESULT hr = copy_surface_rect(source, &rects[index], destination,
                &points[index]);
        if (FAILED(hr)) return D8WG_TRACE_ERROR(hr);
    }
    return D3D_OK;
}

static HRESULT WINAPI device_get_front_buffer(IDirect3DDevice8 *iface,
        IDirect3DSurface8 *destination_iface)
{
    D8Device *device = device_from_iface(iface);
    IDirect3DSurface8 *source_iface = NULL;
    HRESULT hr;
    if (!destination_iface || destination_iface->lpVtbl != &g_surface_vtbl)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if ((surface_from_iface(destination_iface)->texture
            ? surface_from_iface(destination_iface)->texture->device
            : surface_from_iface(destination_iface)->device) != device)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    hr = create_backbuffer_surface(device, &device->present, FALSE,
            &source_iface);
    if (SUCCEEDED(hr)) {
        hr = copy_surface_rect(surface_from_iface(source_iface), NULL,
                surface_from_iface(destination_iface), NULL);
        IDirect3DSurface8_Release(source_iface);
    }
    return D8WG_TRACE_ERROR(hr);
}

static HRESULT WINAPI device_set_render_target(IDirect3DDevice8 *iface,
        IDirect3DSurface8 *render_target_iface,
        IDirect3DSurface8 *depth_iface)
{
    D8Device *device = device_from_iface(iface);
    D8Surface *render_target;
    D8Surface *depth = depth_iface ? surface_from_iface(depth_iface) : NULL;
    D8Texture *texture;
    D9WGSetRenderTarget command;
    D9WGSetDepthStencilSurfaceLevel depth_command;
    D3DVIEWPORT8 viewport;
    UINT render_target_width;
    UINT render_target_height;
    UINT depth_width;
    UINT depth_height;
    if (!render_target_iface
            || render_target_iface->lpVtbl != &g_surface_vtbl
            || (depth_iface && depth_iface->lpVtbl != &g_surface_vtbl))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    render_target = surface_from_iface(render_target_iface);
    if ((render_target->texture ? render_target->texture->device
            : render_target->device) != device || render_target->depth_stencil)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    texture = render_target->backbuffer ? NULL : render_target->texture;
    if (texture && !(texture->usage & D3DUSAGE_RENDERTARGET))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (depth && ((depth->texture ? depth->texture->device : depth->device)
                != device || !depth->depth_stencil)) {
        D8WG_TRACE("RENDER TARGET REFUSE reason=depth-surface "
                "depth_stencil=%d owner=%08lX device=%08lX",
                (int)depth->depth_stencil,
                (DWORD)(uintptr_t)(depth->texture ? depth->texture->device
                        : depth->device), (DWORD)(uintptr_t)device);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    render_target_width = render_target->texture
            ? render_target->texture->levels[render_target->level].width
            : render_target->desc.Width;
    render_target_height = render_target->texture
            ? render_target->texture->levels[render_target->level].height
            : render_target->desc.Height;
    depth_width = !depth ? 0u : depth->texture
            ? depth->texture->levels[depth->level].width : depth->desc.Width;
    depth_height = !depth ? 0u : depth->texture
            ? depth->texture->levels[depth->level].height : depth->desc.Height;
    /* D3D8 permits a depth/stencil surface larger than the colour target; it
     * only returns INVALIDCALL when depth is smaller. 3DMark 2001 keeps its
     * full-size depth surface while switching to a smaller offscreen target in
     * the advanced pixel-shader test, so exact equality rejects a legal bind. */
    if (depth && (depth_width < render_target_width
            || depth_height < render_target_height)) {
        D8WG_TRACE("RENDER TARGET REFUSE reason=depth-too-small rt=%ux%u "
                "depth=%ux%u", render_target_width, render_target_height,
                depth_width, depth_height);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (texture) IDirect3DTexture8_AddRef(&texture->iface);
    if (depth) IDirect3DSurface8_AddRef(&depth->iface);
    if (device->render_target_texture)
        IDirect3DTexture8_Release(&device->render_target_texture->iface);
    if (device->depth_surface)
        IDirect3DSurface8_Release(&device->depth_surface->iface);
    device->render_target_texture = texture;
    device->render_target_level = texture ? render_target->level : 0;
    device->depth_surface = depth;
    device->depth_surface_enabled = depth != NULL;
    /*
     * D3D8 binds colour and depth in one call; D3D9 splits them, so this
     * becomes two commands.
     *
     * Two kinds of depth surface reach here. One created by
     * CreateDepthStencilSurface owns a host texture and is bound by handle,
     * so a render-to-texture pass gets depth of its own size. The surface
     * GetDepthStencilSurface() hands back for the device's implicit buffer
     * has no texture, and names the auto depth-stencil the host negotiated at
     * CreateDevice.
     */
    command.device_handle = device->handle;
    command.target_index = 0;
    command.color_texture_handle = texture ? texture->handle : 0;
    command.color_level = texture ? render_target->level : 0;
    command.color_face = 0;
    if (!emit_command(D9WG_OP_SET_RENDER_TARGET, &command, sizeof(command)))
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);

    depth_command.device_handle = device->handle;
    depth_command.depth_texture_handle = !depth ? 0u
            : depth->texture ? depth->texture->handle
            : D9WG_AUTO_DEPTH_STENCIL_HANDLE;
    depth_command.depth_level = depth && depth->texture ? depth->level : 0u;
    depth_command.width = depth_width;
    depth_command.height = depth_height;
    if (!emit_command(D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL,
            &depth_command, sizeof(depth_command)))
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);

    /* IDirect3DDevice8::SetRenderTarget resets the viewport to the complete
     * new colour target. Keep both the guest shadow and WebGPU state in sync;
     * otherwise a later GetViewport reports the old size and XYZRHW passes
     * are mapped through the wrong pixel rectangle. */
    viewport.X = 0;
    viewport.Y = 0;
    viewport.Width = render_target_width;
    viewport.Height = render_target_height;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    return D8WG_TRACE_ERROR(device_set_viewport(iface, &viewport));
}

static HRESULT WINAPI device_get_render_target(IDirect3DDevice8 *iface,
        IDirect3DSurface8 **surface_out)
{
    D8Device *device = device_from_iface(iface);
    if (!surface_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (device->render_target_texture)
        return texture_get_surface_level(&device->render_target_texture->iface,
                device->render_target_level, surface_out);
    return create_backbuffer_surface(device, &device->present, TRUE,
            surface_out);
}

static HRESULT WINAPI device_get_depth_surface(IDirect3DDevice8 *iface,
        IDirect3DSurface8 **surface_out)
{
    D8Device *device = device_from_iface(iface);
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (!device->depth_surface_enabled)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->depth_surface) {
        *surface_out = &device->depth_surface->iface;
        IDirect3DSurface8_AddRef(*surface_out);
        return D3D_OK;
    }
    return create_standalone_surface(device, device->display_mode.Width,
            device->display_mode.Height,
            device->present.AutoDepthStencilFormat,
            D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, FALSE, FALSE, TRUE,
            TRUE, surface_out);
}
static BOOL transform_slot(D3DTRANSFORMSTATETYPE state, UINT *slot)
{
    UINT value = (UINT)state;
    if (state == D3DTS_VIEW) {
        *slot = D8WG_TRANSFORM_VIEW_SLOT;
        return TRUE;
    }
    if (state == D3DTS_PROJECTION) {
        *slot = D8WG_TRANSFORM_PROJECTION_SLOT;
        return TRUE;
    }
    if (value >= (UINT)D3DTS_TEXTURE0 && value <= (UINT)D3DTS_TEXTURE7) {
        *slot = D8WG_TRANSFORM_TEXTURE_SLOT + value - (UINT)D3DTS_TEXTURE0;
        return TRUE;
    }
    if (value >= (UINT)D3DTS_WORLD && value < (UINT)D3DTS_WORLD + 256u) {
        *slot = D8WG_TRANSFORM_WORLD_SLOT + value - (UINT)D3DTS_WORLD;
        return TRUE;
    }
    return FALSE;
}

static BOOL matrix_equal(const D3DMATRIX *left, const D3DMATRIX *right)
{
    UINT row;
    UINT column;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            if (left->m[row][column] != right->m[row][column])
                return FALSE;
        }
    }
    return TRUE;
}

static void matrix_multiply(D3DMATRIX *result, const D3DMATRIX *left,
        const D3DMATRIX *right)
{
    D3DMATRIX temporary;
    UINT row;
    UINT column;
    UINT inner;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (inner = 0; inner < 4; ++inner)
                value += left->m[row][inner] * right->m[inner][column];
            temporary.m[row][column] = value;
        }
    }
    *result = temporary;
}

static HRESULT emit_transform(D8Device *device,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D9WGSetTransform command;
    command.device_handle = device->handle;
    command.state = (UINT)state;
    CopyMemory(command.matrix, matrix, sizeof(command.matrix));
    return emit_command(D9WG_OP_SET_TRANSFORM, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_transform(IDirect3DDevice8 *iface,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D8Device *device = device_from_iface(iface);
    UINT slot;
    if (!matrix || !transform_slot(state, &slot))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.transform_mask[slot] = 1;
    if (matrix_equal(&device->transforms[slot], matrix))
        return D3D_OK;
    device->transforms[slot] = *matrix;
    return emit_transform(device, state, matrix);
}

static HRESULT WINAPI device_get_transform(IDirect3DDevice8 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    UINT slot;
    if (!matrix || !transform_slot(state, &slot))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *matrix = device_from_iface(iface)->transforms[slot];
    return D3D_OK;
}

static HRESULT WINAPI device_multiply_transform(IDirect3DDevice8 *iface,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D8Device *device = device_from_iface(iface);
    D3DMATRIX product;
    UINT slot;
    if (!matrix || !transform_slot(state, &slot))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.transform_mask[slot] = 1;
    matrix_multiply(&product, &device->transforms[slot], matrix);
    if (matrix_equal(&device->transforms[slot], &product))
        return D3D_OK;
    device->transforms[slot] = product;
    return emit_transform(device, state, &product);
}
static BOOL bytes_equal(const void *left, const void *right, UINT size)
{
    const BYTE *a = (const BYTE *)left;
    const BYTE *b = (const BYTE *)right;
    UINT index;
    for (index = 0; index < size; ++index) {
        if (a[index] != b[index]) return FALSE;
    }
    return TRUE;
}

static HRESULT WINAPI device_set_material(IDirect3DDevice8 *iface,
        const D3DMATERIAL8 *material)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetMaterial command;
    if (!material) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.material_mask = TRUE;
    if (bytes_equal(&device->material, material, sizeof(*material)))
        return D3D_OK;
    device->material = *material;
    command.device_handle = device->handle;
    CopyMemory(command.diffuse, &material->Diffuse, sizeof(command.diffuse));
    CopyMemory(command.ambient, &material->Ambient, sizeof(command.ambient));
    CopyMemory(command.specular, &material->Specular, sizeof(command.specular));
    CopyMemory(command.emissive, &material->Emissive, sizeof(command.emissive));
    command.power = material->Power;
    return emit_command(D9WG_OP_SET_MATERIAL, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_material(IDirect3DDevice8 *iface,
        D3DMATERIAL8 *material)
{
    if (!material) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *material = device_from_iface(iface)->material;
    return D3D_OK;
}

static D8LightState *find_extra_light(D8LightState *entry, DWORD index)
{
    while (entry && entry->index != index)
        entry = entry->next;
    return entry;
}

static D8LightState *get_extra_light(D8LightState **list, DWORD index,
        BOOL create)
{
    D8LightState *entry = find_extra_light(*list, index);
    if (entry || !create)
        return entry;
    entry = (D8LightState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*entry));
    if (!entry)
        return NULL;
    entry->index = index;
    entry->next = *list;
    *list = entry;
    return entry;
}

static void free_extra_lights(D8LightState **list)
{
    D8LightState *entry = *list;
    *list = NULL;
    while (entry) {
        D8LightState *next = entry->next;
        HeapFree(GetProcessHeap(), 0, entry);
        entry = next;
    }
}

static void initialize_default_light(D3DLIGHT8 *light)
{
    ZeroMemory(light, sizeof(*light));
    light->Type = D3DLIGHT_DIRECTIONAL;
    light->Diffuse.r = 1.0f;
    light->Diffuse.g = 1.0f;
    light->Diffuse.b = 1.0f;
    light->Direction.z = 1.0f;
}

static BOOL emit_light(D8Device *device, DWORD index,
        const D3DLIGHT8 *light)
{
    D9WGSetLight command;
    command.device_handle = device->handle;
    command.index = index;
    command.type = light->Type;
    CopyMemory(command.diffuse, &light->Diffuse, sizeof(command.diffuse));
    CopyMemory(command.specular, &light->Specular, sizeof(command.specular));
    CopyMemory(command.ambient, &light->Ambient, sizeof(command.ambient));
    CopyMemory(command.position, &light->Position, sizeof(command.position));
    CopyMemory(command.direction, &light->Direction, sizeof(command.direction));
    command.range = light->Range;
    command.falloff = light->Falloff;
    command.attenuation[0] = light->Attenuation0;
    command.attenuation[1] = light->Attenuation1;
    command.attenuation[2] = light->Attenuation2;
    command.theta = light->Theta;
    command.phi = light->Phi;
    return emit_command(D9WG_OP_SET_LIGHT, &command, sizeof(command));
}

static HRESULT WINAPI device_set_light(IDirect3DDevice8 *iface, DWORD index,
        const D3DLIGHT8 *light)
{
    D8Device *device = device_from_iface(iface);
    D8LightState *entry;
    D8LightState *recorded = NULL;

    if (!light || light->Type < D3DLIGHT_POINT
            || light->Type > D3DLIGHT_DIRECTIONAL)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (index < D8WG_MAX_LIGHTS) {
        if (device->recording_state_block)
            device->recording_state_block->state.light_mask[index] = 1;
        if (device->light_set[index]
                && bytes_equal(&device->lights[index], light, sizeof(*light)))
            return D3D_OK;
        device->lights[index] = *light;
        device->light_set[index] = TRUE;
    } else {
        entry = get_extra_light(&device->extra_lights, index, TRUE);
        if (!entry)
            return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        if (device->recording_state_block) {
            recorded = get_extra_light(
                    &device->recording_state_block->state.extra_lights,
                    index, TRUE);
            if (!recorded)
                return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
            recorded->light_mask = TRUE;
        }
        if (entry->set && bytes_equal(&entry->light, light, sizeof(*light)))
            return D3D_OK;
        entry->light = *light;
        entry->set = TRUE;
    }
    return emit_light(device, index, light)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_light(IDirect3DDevice8 *iface, DWORD index,
        D3DLIGHT8 *light)
{
    D8Device *device = device_from_iface(iface);
    D8LightState *entry;
    if (!light)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (index < D8WG_MAX_LIGHTS) {
        if (!device->light_set[index])
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        *light = device->lights[index];
    } else {
        entry = find_extra_light(device->extra_lights, index);
        if (!entry || !entry->set)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        *light = entry->light;
    }
    return D3D_OK;
}

static HRESULT WINAPI device_light_enable(IDirect3DDevice8 *iface,
        DWORD index, WINBOOL enable)
{
    D8Device *device = device_from_iface(iface);
    D8LightState *entry = NULL;
    D8LightState *recorded = NULL;
    D3DLIGHT8 *light;
    BOOL *set;
    BOOL *enabled;
    D9WGLightEnable command;

    if (index < D8WG_MAX_LIGHTS) {
        light = &device->lights[index];
        set = &device->light_set[index];
        enabled = &device->light_enabled[index];
        if (device->recording_state_block)
            device->recording_state_block->state.light_enable_mask[index] = 1;
    } else {
        entry = get_extra_light(&device->extra_lights, index, TRUE);
        if (!entry)
            return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        light = &entry->light;
        set = &entry->set;
        enabled = &entry->enabled;
        if (device->recording_state_block) {
            recorded = get_extra_light(
                    &device->recording_state_block->state.extra_lights,
                    index, TRUE);
            if (!recorded)
                return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
            recorded->enable_mask = TRUE;
        }
    }
    /* Direct3D creates a default directional light when LightEnable names an
     * index that SetLight has never assigned. This applies even when the call
     * disables that new light. */
    if (!*set) {
        initialize_default_light(light);
        *set = TRUE;
        if (!emit_light(device, index, light))
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    enable = !!enable;
    if (*enabled == enable) return D3D_OK;
    *enabled = enable;
    command.device_handle = device->handle;
    command.index = index;
    command.enable = enable;
    command.reserved = 0;
    return emit_command(D9WG_OP_LIGHT_ENABLE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_light_enable(IDirect3DDevice8 *iface,
        DWORD index, WINBOOL *enable)
{
    D8Device *device = device_from_iface(iface);
    D8LightState *entry;
    if (!enable) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (index < D8WG_MAX_LIGHTS) {
        *enable = device->light_enabled[index];
    } else {
        entry = find_extra_light(device->extra_lights, index);
        *enable = entry ? entry->enabled : FALSE;
    }
    return D3D_OK;
}
static HRESULT WINAPI device_set_clip_plane(IDirect3DDevice8 *iface,
        DWORD index, const float *plane)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetClipPlane command;

    if (!plane || index >= D8WG_MAX_CLIP_PLANES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(device->clip_planes[index], plane,
            sizeof(device->clip_planes[index]));
    command.device_handle = device->handle;
    command.index = index;
    CopyMemory(command.plane, plane, sizeof(command.plane));
    return emit_command(D9WG_OP_SET_CLIP_PLANE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_clip_plane(IDirect3DDevice8 *iface,
        DWORD index, float *plane)
{
    D8Device *device = device_from_iface(iface);

    if (!plane || index >= D8WG_MAX_CLIP_PLANES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(plane, device->clip_planes[index],
            sizeof(device->clip_planes[index]));
    return D3D_OK;
}

static void state_block_set_scope(D8StateBlock *block,
        D3DSTATEBLOCKTYPE type)
{
    D8StateSnapshot *state = &block->state;
    if (type == D3DSBT_ALL || type == D3DSBT_PIXELSTATE) {
        FillMemory(state->render_mask, sizeof(state->render_mask), 1);
        FillMemory(state->texture_stage_mask,
                sizeof(state->texture_stage_mask), 1);
        FillMemory(state->texture_mask, sizeof(state->texture_mask), 1);
        state->pixel_shader_mask = TRUE;
    }
    if (type == D3DSBT_ALL || type == D3DSBT_VERTEXSTATE) {
        /* D3D8 has render states in both predefined state-block classes. */
        FillMemory(state->render_mask, sizeof(state->render_mask), 1);
        FillMemory(state->stream_mask, sizeof(state->stream_mask), 1);
        FillMemory(state->transform_mask, sizeof(state->transform_mask), 1);
        FillMemory(state->light_mask, sizeof(state->light_mask), 1);
        FillMemory(state->light_enable_mask,
                sizeof(state->light_enable_mask), 1);
        state->all_lights_scope = TRUE;
        state->viewport_mask = TRUE;
        state->material_mask = TRUE;
        state->indices_mask = TRUE;
        state->vertex_shader_mask = TRUE;
    }
}

static void state_block_release_references(D8StateBlock *block)
{
    UINT index;
    for (index = 0; index < D8WG_MAX_TEXTURE_STAGES; ++index) {
        if (block->state.textures[index]) {
            IDirect3DBaseTexture8_Release(block->state.textures[index]);
            block->state.textures[index] = NULL;
        }
    }
    for (index = 0; index < D8WG_MAX_STREAMS; ++index) {
        if (block->state.streams[index].buffer) {
            IDirect3DVertexBuffer8_Release(
                    &block->state.streams[index].buffer->iface);
            block->state.streams[index].buffer = NULL;
        }
    }
    if (block->state.index_buffer) {
        IDirect3DIndexBuffer8_Release(&block->state.index_buffer->iface);
        block->state.index_buffer = NULL;
    }
    free_extra_lights(&block->state.extra_lights);
}

static void state_block_capture(D8Device *device, D8StateBlock *block)
{
    D8StateSnapshot *state = &block->state;
    D8LightState *entry;
    UINT index;
    UINT stage;

    for (index = 0; index < D8WG_MAX_RENDER_STATES; ++index) {
        if (state->render_mask[index])
            state->render_states[index] = device->render_states[index];
    }
    for (stage = 0; stage < D8WG_MAX_TEXTURE_STAGES; ++stage) {
        for (index = 0; index < D8WG_MAX_TEXTURE_STAGE_STATES; ++index) {
            if (state->texture_stage_mask[stage][index])
                state->texture_stage_states[stage][index] =
                        device->texture_stage_states[stage][index];
        }
        if (state->texture_mask[stage]) {
            IDirect3DBaseTexture8 *texture = device->textures[stage];
            if (texture)
                IDirect3DBaseTexture8_AddRef(texture);
            if (state->textures[stage])
                IDirect3DBaseTexture8_Release(state->textures[stage]);
            state->textures[stage] = texture;
        }
    }
    for (index = 0; index < D8WG_MAX_STREAMS; ++index) {
        if (state->stream_mask[index]) {
            D8VertexBuffer *buffer = device->streams[index].buffer;
            if (buffer)
                IDirect3DVertexBuffer8_AddRef(&buffer->iface);
            if (state->streams[index].buffer)
                IDirect3DVertexBuffer8_Release(
                        &state->streams[index].buffer->iface);
            state->streams[index] = device->streams[index];
            state->streams[index].buffer = buffer;
        }
    }
    if (state->indices_mask) {
        D8IndexBuffer *buffer = device->index_buffer;
        if (buffer)
            IDirect3DIndexBuffer8_AddRef(&buffer->iface);
        if (state->index_buffer)
            IDirect3DIndexBuffer8_Release(&state->index_buffer->iface);
        state->index_buffer = buffer;
        state->base_vertex_index = device->base_vertex_index;
    }
    if (state->vertex_shader_mask)
        state->vertex_shader = device->vertex_shader;
    if (state->pixel_shader_mask)
        state->pixel_shader = device->pixel_shader;
    if (state->viewport_mask)
        state->viewport = device->viewport;
    if (state->material_mask)
        state->material = device->material;
    for (index = 0; index < D8WG_MAX_TRANSFORMS; ++index) {
        if (state->transform_mask[index])
            state->transforms[index] = device->transforms[index];
    }
    for (index = 0; index < D8WG_MAX_LIGHTS; ++index) {
        if (state->light_mask[index]) {
            state->lights[index] = device->lights[index];
            state->light_set[index] = device->light_set[index];
        }
        if (state->light_enable_mask[index])
            state->light_enabled[index] = device->light_enabled[index];
    }
    /* A predefined ALL/VERTEX block covers every light property set that
     * exists at capture time, including sparse indices above MaxActiveLights.
     * A BeginStateBlock recording already created only the entries explicitly
     * touched while recording. */
    if (state->all_lights_scope) {
        for (entry = device->extra_lights; entry; entry = entry->next) {
            D8LightState *captured = get_extra_light(&state->extra_lights,
                    entry->index, TRUE);
            if (!captured)
                continue;
            captured->light_mask = TRUE;
            captured->enable_mask = TRUE;
        }
    }
    for (entry = state->extra_lights; entry; entry = entry->next) {
        D8LightState *current = find_extra_light(device->extra_lights,
                entry->index);
        if (entry->light_mask) {
            entry->set = current ? current->set : FALSE;
            if (current && current->set)
                entry->light = current->light;
        }
        if (entry->enable_mask)
            entry->enabled = current ? current->enabled : FALSE;
    }
}

static D8StateBlock *device_find_state_block(D8Device *device, DWORD token,
        D8StateBlock ***link_out)
{
    D8StateBlock **link = &device->state_blocks;
    while (*link) {
        if ((*link)->token == token) {
            if (link_out) *link_out = link;
            return *link;
        }
        link = &(*link)->next;
    }
    return NULL;
}

static DWORD device_allocate_state_block_token(D8Device *device)
{
    DWORD token;
    do {
        token = ++device->next_state_block_token;
        if (!token) token = ++device->next_state_block_token;
    } while (device_find_state_block(device, token, NULL));
    return token;
}

static D3DTRANSFORMSTATETYPE transform_state_from_slot(UINT slot)
{
    if (slot == D8WG_TRANSFORM_VIEW_SLOT) return D3DTS_VIEW;
    if (slot == D8WG_TRANSFORM_PROJECTION_SLOT) return D3DTS_PROJECTION;
    if (slot >= D8WG_TRANSFORM_TEXTURE_SLOT
            && slot < D8WG_TRANSFORM_WORLD_SLOT)
        return (D3DTRANSFORMSTATETYPE)((UINT)D3DTS_TEXTURE0
                + slot - D8WG_TRANSFORM_TEXTURE_SLOT);
    return (D3DTRANSFORMSTATETYPE)((UINT)D3DTS_WORLD
            + slot - D8WG_TRANSFORM_WORLD_SLOT);
}

static HRESULT state_block_apply(D8Device *device, D8StateBlock *block)
{
    D8StateSnapshot *state = &block->state;
    D8LightState *light;
    HRESULT hr;
    UINT index;
    UINT stage;
#define APPLY_STATE(call) do { hr = (call); if (FAILED(hr)) return D8WG_TRACE_ERROR(hr); } while (0)
    for (index = 0; index < D8WG_MAX_RENDER_STATES; ++index) {
        if (state->render_mask[index])
            APPLY_STATE(device_set_render_state(&device->iface,
                    (D3DRENDERSTATETYPE)index, state->render_states[index]));
    }
    for (stage = 0; stage < D8WG_MAX_TEXTURE_STAGES; ++stage) {
        for (index = 0; index < D8WG_MAX_TEXTURE_STAGE_STATES; ++index) {
            if (state->texture_stage_mask[stage][index])
                APPLY_STATE(device_set_texture_stage_state(&device->iface,
                        stage, (D3DTEXTURESTAGESTATETYPE)index,
                        state->texture_stage_states[stage][index]));
        }
        if (state->texture_mask[stage])
            APPLY_STATE(device_set_texture(&device->iface, stage,
                    state->textures[stage]));
    }
    if (state->viewport_mask)
        APPLY_STATE(device_set_viewport(&device->iface, &state->viewport));
    if (state->material_mask)
        APPLY_STATE(device_set_material(&device->iface, &state->material));
    for (index = 0; index < D8WG_MAX_TRANSFORMS; ++index) {
        if (state->transform_mask[index])
            APPLY_STATE(device_set_transform(&device->iface,
                    transform_state_from_slot(index),
                    &state->transforms[index]));
    }
    for (index = 0; index < D8WG_MAX_LIGHTS; ++index) {
        if (state->light_mask[index] && state->light_set[index])
            APPLY_STATE(device_set_light(&device->iface, index,
                    &state->lights[index]));
        if (state->light_enable_mask[index])
            APPLY_STATE(device_light_enable(&device->iface, index,
                    state->light_enabled[index]));
    }
    for (light = state->extra_lights; light; light = light->next) {
        if (light->light_mask && light->set)
            APPLY_STATE(device_set_light(&device->iface, light->index,
                    &light->light));
        if (light->enable_mask)
            APPLY_STATE(device_light_enable(&device->iface, light->index,
                    light->enabled));
    }
    for (index = 0; index < D8WG_MAX_STREAMS; ++index) {
        if (state->stream_mask[index])
            APPLY_STATE(device_set_stream_source(&device->iface, index,
                    state->streams[index].buffer
                    ? &state->streams[index].buffer->iface : NULL,
                    state->streams[index].stride));
    }
    if (state->indices_mask)
        APPLY_STATE(device_set_indices(&device->iface,
                state->index_buffer ? &state->index_buffer->iface : NULL,
                state->base_vertex_index));
    if (state->vertex_shader_mask)
        APPLY_STATE(device_set_vertex_shader(&device->iface,
                state->vertex_shader));
    if (state->pixel_shader_mask)
        APPLY_STATE(device_set_pixel_shader(&device->iface,
                state->pixel_shader));
#undef APPLY_STATE
    return D3D_OK;
}

static HRESULT WINAPI device_begin_state_block(IDirect3DDevice8 *iface)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock *block;
    if (device->recording_state_block)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block = (D8StateBlock *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*block));
    if (!block) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    device->recording_state_block = block;
    return D3D_OK;
}

static HRESULT WINAPI device_end_state_block(IDirect3DDevice8 *iface,
        DWORD *token)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock *block = device->recording_state_block;
    if (!token || !block)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block->token = device_allocate_state_block_token(device);
    state_block_capture(device, block);
    block->next = device->state_blocks;
    device->state_blocks = block;
    device->recording_state_block = NULL;
    *token = block->token;
    return D3D_OK;
}

static HRESULT WINAPI device_apply_state_block(IDirect3DDevice8 *iface,
        DWORD token)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock *block;
    if (device->recording_state_block)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block = device_find_state_block(device, token, NULL);
    return D8WG_TRACE_ERROR(block ? state_block_apply(device, block)
            : D3DERR_INVALIDCALL);
}

static HRESULT WINAPI device_capture_state_block(IDirect3DDevice8 *iface,
        DWORD token)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock *block;
    if (device->recording_state_block)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block = device_find_state_block(device, token, NULL);
    if (!block) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    state_block_capture(device, block);
    return D3D_OK;
}

static HRESULT WINAPI device_delete_state_block(IDirect3DDevice8 *iface,
        DWORD token)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock **link;
    D8StateBlock *block;
    if (device->recording_state_block)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block = device_find_state_block(device, token, &link);
    if (!block) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *link = block->next;
    state_block_release_references(block);
    HeapFree(GetProcessHeap(), 0, block);
    return D3D_OK;
}

static HRESULT WINAPI device_create_state_block(IDirect3DDevice8 *iface,
        D3DSTATEBLOCKTYPE type, DWORD *token)
{
    D8Device *device = device_from_iface(iface);
    D8StateBlock *block;
    if (!token || device->recording_state_block
            || (type != D3DSBT_ALL && type != D3DSBT_PIXELSTATE
                && type != D3DSBT_VERTEXSTATE))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    block = (D8StateBlock *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*block));
    if (!block) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    block->type = type;
    block->token = device_allocate_state_block_token(device);
    state_block_set_scope(block, type);
    state_block_capture(device, block);
    block->next = device->state_blocks;
    device->state_blocks = block;
    *token = block->token;
    return D3D_OK;
}
DEV_STUB(set_clip_status, const D3DCLIPSTATUS8 *status)
{ (void)iface;(void)status; return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }
DEV_STUB(get_clip_status, D3DCLIPSTATUS8 *status)
{ (void)iface;(void)status; return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }
DEV_STUB(get_info, DWORD id, void *info, DWORD size)
{ (void)iface;(void)id;(void)info;(void)size; return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE); }
/*
 * Palettes. D3D8's PALETTEENTRY is peRed/peGreen/peBlue/peFlags; the host wants
 * 256 D3DCOLOR (A8R8G8B8) entries, with peFlags carrying the alpha -- which is
 * what D3DPTEXTURECAPS_ALPHAPALETTE means and what every P8 texture with
 * transparency relies on.
 */
static HRESULT WINAPI device_set_palette(IDirect3DDevice8 *iface, UINT index,
        const PALETTEENTRY *entries)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetPalette command;
    DWORD colors[256];
    uint8_t *payload;
    uint8_t *blob;
    UINT entry;
    BOOL result;

    if (!entries || index >= D8WG_MAX_PALETTES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(device->palettes[index], entries, sizeof(device->palettes[index]));
    device->palette_set[index] = TRUE;
    for (entry = 0; entry < 256u; ++entry) {
        colors[entry] = ((DWORD)entries[entry].peFlags << 24)
                | ((DWORD)entries[entry].peRed << 16)
                | ((DWORD)entries[entry].peGreen << 8)
                | (DWORD)entries[entry].peBlue;
    }

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_PALETTE, sizeof(command),
            sizeof(colors), NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.palette_index = index;
        command.entry_count = 256;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, colors, sizeof(colors));
    }
    LeaveCriticalSection(&g_transport_lock);
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI device_get_palette(IDirect3DDevice8 *iface, UINT index,
        PALETTEENTRY *entries)
{
    D8Device *device = device_from_iface(iface);

    if (!entries || index >= D8WG_MAX_PALETTES || !device->palette_set[index])
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(entries, device->palettes[index], sizeof(device->palettes[index]));
    return D3D_OK;
}

static HRESULT WINAPI device_set_current_palette(IDirect3DDevice8 *iface,
        UINT index)
{
    D8Device *device = device_from_iface(iface);
    D9WGSetCurrentTexturePalette command;

    if (index >= D8WG_MAX_PALETTES || !device->palette_set[index])
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    device->current_palette = index;
    command.device_handle = device->handle;
    command.palette_index = index;
    return emit_command(D9WG_OP_SET_CURRENT_TEXTURE_PALETTE, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_current_palette(IDirect3DDevice8 *iface,
        UINT *index)
{
    if (!index) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *index = device_from_iface(iface)->current_palette;
    return D3D_OK;
}
/* ---- ProcessVertices: software fixed-function transform ---- */

/*
 * D3D8's ProcessVertices runs the fixed-function vertex pipeline over stream 0
 * and writes the *transformed* result into an ordinary vertex buffer, which the
 * app then draws as pre-transformed geometry (or reads back).
 *
 * It has to happen guest-side. The whole point of the call is that the result
 * lands in guest-visible memory the app can Lock and read, and this stack's
 * host is asynchronous -- there is no synchronous GPU round trip that could
 * answer it. So this is a real software transform over the state the guest
 * already shadows for SetTransform/SetViewport/SetMaterial/SetLight.
 */
/*
 * This DLL links no C runtime (see build.sh), so libm's sqrtf is unavailable.
 * Newton-Raphson from the classic bit-level estimate converges to float
 * precision well within four iterations for the magnitudes vertex normals and
 * light distances actually take.
 */
static float float_sqrt(float value)
{
    union { float number; uint32_t bits; } convert;
    float guess;
    UINT iteration;

    if (!(value > 0.0f)) return 0.0f;
    convert.number = value;
    convert.bits = 0x1fbd1df5u + (convert.bits >> 1);
    guess = convert.number;
    for (iteration = 0; iteration < 4u; ++iteration)
        guess = 0.5f * (guess + value / guess);
    return guess;
}

static void transform_vector4(const D3DMATRIX *matrix, const float *in,
        float *out)
{
    UINT row;
    for (row = 0; row < 4; ++row) {
        out[row] = in[0] * matrix->m[0][row] + in[1] * matrix->m[1][row]
                + in[2] * matrix->m[2][row] + in[3] * matrix->m[3][row];
    }
}

static void multiply_matrix(const D3DMATRIX *left, const D3DMATRIX *right,
        D3DMATRIX *out)
{
    UINT row;
    UINT column;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            out->m[row][column] =
                    left->m[row][0] * right->m[0][column]
                    + left->m[row][1] * right->m[1][column]
                    + left->m[row][2] * right->m[2][column]
                    + left->m[row][3] * right->m[3][column];
        }
    }
}

/*
 * Byte offsets of the components an FVF describes, or -1 when absent. Only the
 * subset ProcessVertices can consume or produce is decoded; anything else makes
 * the caller refuse rather than mis-stride the buffer.
 */
typedef struct D8FVFLayout {
    int position;      /* XYZ or XYZRHW */
    BOOL pretransformed;
    int normal;
    int diffuse;
    int specular;
    int texcoord[8];
    UINT texcoord_size[8];
    UINT texcoord_count;
    UINT stride;
} D8FVFLayout;

static BOOL decode_fvf_layout(DWORD fvf, D8FVFLayout *layout)
{
    static const UINT texcoord_floats[4] = { 2u, 3u, 4u, 1u };
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    UINT offset = 0;
    UINT index;

    ZeroMemory(layout, sizeof(*layout));
    layout->position = -1;
    layout->normal = -1;
    layout->diffuse = -1;
    layout->specular = -1;
    for (index = 0; index < 8u; ++index)
        layout->texcoord[index] = -1;

    if (position == D3DFVF_XYZ) {
        layout->position = (int)offset;
        offset += 12;
    } else if (position == D3DFVF_XYZRHW) {
        layout->position = (int)offset;
        layout->pretransformed = TRUE;
        offset += 16;
    } else {
        /* Blended positions need the vertex-blend pipeline, which this
         * software path does not implement. */
        return FALSE;
    }
    if (fvf & D3DFVF_NORMAL) {
        if (layout->pretransformed) return FALSE;
        layout->normal = (int)offset;
        offset += 12;
    }
    if (fvf & D3DFVF_PSIZE) offset += 4;
    if (fvf & D3DFVF_DIFFUSE) {
        layout->diffuse = (int)offset;
        offset += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        layout->specular = (int)offset;
        offset += 4;
    }
    layout->texcoord_count = (fvf & D3DFVF_TEXCOUNT_MASK)
            >> D3DFVF_TEXCOUNT_SHIFT;
    if (layout->texcoord_count > 8u) return FALSE;
    for (index = 0; index < layout->texcoord_count; ++index) {
        UINT floats = texcoord_floats[(fvf >> (16u + index * 2u)) & 0x3u];
        layout->texcoord[index] = (int)offset;
        layout->texcoord_size[index] = floats;
        offset += floats * 4u;
    }
    layout->stride = offset;
    return TRUE;
}

static float saturate_float(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static D3DCOLOR pack_color(const float *rgba)
{
    return ((DWORD)(saturate_float(rgba[3]) * 255.0f + 0.5f) << 24)
            | ((DWORD)(saturate_float(rgba[0]) * 255.0f + 0.5f) << 16)
            | ((DWORD)(saturate_float(rgba[1]) * 255.0f + 0.5f) << 8)
            | (DWORD)(saturate_float(rgba[2]) * 255.0f + 0.5f);
}

/*
 * The fixed-function lighting D3D8 specifies, in view space, over the lights
 * the guest has shadowed. Directional, point and spot are all handled; the
 * result is the diffuse colour ProcessVertices writes.
 */
static void light_vertex(D8Device *device, const float *eye_position,
        const float *eye_normal, float *out_rgba)
{
    const D3DMATERIAL8 *material = &device->material;
    DWORD ambient_state = device->render_states[D3DRS_AMBIENT];
    float ambient[3];
    UINT index;

    ambient[0] = ((ambient_state >> 16) & 0xffu) / 255.0f;
    ambient[1] = ((ambient_state >> 8) & 0xffu) / 255.0f;
    ambient[2] = (ambient_state & 0xffu) / 255.0f;

    out_rgba[0] = material->Emissive.r + material->Ambient.r * ambient[0];
    out_rgba[1] = material->Emissive.g + material->Ambient.g * ambient[1];
    out_rgba[2] = material->Emissive.b + material->Ambient.b * ambient[2];
    out_rgba[3] = material->Diffuse.a;

    for (index = 0; index < D8WG_MAX_LIGHTS; ++index) {
        const D3DLIGHT8 *light;
        float to_light[3];
        float attenuation = 1.0f;
        float n_dot_l;
        float length;

        if (!device->light_set[index] || !device->light_enabled[index])
            continue;
        light = &device->lights[index];
        if (light->Type == D3DLIGHT_DIRECTIONAL) {
            float view_direction[4];
            float direction[4];
            direction[0] = light->Direction.x;
            direction[1] = light->Direction.y;
            direction[2] = light->Direction.z;
            direction[3] = 0.0f;
            transform_vector4(&device->transforms[D8WG_TRANSFORM_VIEW_SLOT],
                    direction, view_direction);
            length = float_sqrt(view_direction[0] * view_direction[0]
                    + view_direction[1] * view_direction[1]
                    + view_direction[2] * view_direction[2]);
            if (length < 1e-6f) continue;
            to_light[0] = -view_direction[0] / length;
            to_light[1] = -view_direction[1] / length;
            to_light[2] = -view_direction[2] / length;
        } else {
            float view_position[4];
            float position[4];
            position[0] = light->Position.x;
            position[1] = light->Position.y;
            position[2] = light->Position.z;
            position[3] = 1.0f;
            transform_vector4(&device->transforms[D8WG_TRANSFORM_VIEW_SLOT],
                    position, view_position);
            to_light[0] = view_position[0] - eye_position[0];
            to_light[1] = view_position[1] - eye_position[1];
            to_light[2] = view_position[2] - eye_position[2];
            length = float_sqrt(to_light[0] * to_light[0]
                    + to_light[1] * to_light[1] + to_light[2] * to_light[2]);
            if (length > light->Range) continue;
            if (length < 1e-6f) length = 1e-6f;
            to_light[0] /= length;
            to_light[1] /= length;
            to_light[2] /= length;
            attenuation = light->Attenuation0
                    + light->Attenuation1 * length
                    + light->Attenuation2 * length * length;
            attenuation = attenuation < 1e-6f ? 1.0f : 1.0f / attenuation;
        }
        n_dot_l = eye_normal[0] * to_light[0] + eye_normal[1] * to_light[1]
                + eye_normal[2] * to_light[2];
        if (n_dot_l < 0.0f) n_dot_l = 0.0f;
        out_rgba[0] += attenuation * (material->Ambient.r * light->Ambient.r
                + material->Diffuse.r * light->Diffuse.r * n_dot_l);
        out_rgba[1] += attenuation * (material->Ambient.g * light->Ambient.g
                + material->Diffuse.g * light->Diffuse.g * n_dot_l);
        out_rgba[2] += attenuation * (material->Ambient.b * light->Ambient.b
                + material->Diffuse.b * light->Diffuse.b * n_dot_l);
    }
}

static HRESULT WINAPI device_process_vertices(IDirect3DDevice8 *iface,
        UINT source_start_index, UINT destination_index, UINT vertex_count,
        IDirect3DVertexBuffer8 *destination_iface, DWORD flags)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *destination;
    D8VertexBuffer *source;
    D8FVFLayout source_layout;
    D8FVFLayout destination_layout;
    D3DMATRIX world_view;
    D3DMATRIX world_view_projection;
    BOOL lighting;
    UINT vertex;

    (void)flags;
    if (!destination_iface
            || destination_iface->lpVtbl != &g_vb_vtbl)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    destination = vb_from_iface(destination_iface);
    source = device->streams[0].buffer;
    if (destination->device != device || !source || destination->locked
            || source->locked || !vertex_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* A real shader means programmable vertex processing, which this software
     * path deliberately does not emulate -- running the fixed-function
     * pipeline instead would silently produce different geometry. */
    if (device->vertex_shader & 1u)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!decode_fvf_layout(device->vertex_shader, &source_layout)
            || !decode_fvf_layout(destination->fvf, &destination_layout))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* D3D8 requires the destination to be pre-transformed, and refuses to
     * transform something that already is. */
    if (source_layout.pretransformed || !destination_layout.pretransformed)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!device->streams[0].stride
            || device->streams[0].stride < source_layout.stride)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    {
        UINT source_needed;
        UINT destination_needed;
        if (!multiply_u32(source_start_index + vertex_count,
                    device->streams[0].stride, &source_needed)
                || !multiply_u32(destination_index + vertex_count,
                    destination_layout.stride, &destination_needed)
                || source_needed > source->length
                || destination_needed > destination->length)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }

    multiply_matrix(&device->transforms[D8WG_TRANSFORM_WORLD_SLOT],
            &device->transforms[D8WG_TRANSFORM_VIEW_SLOT], &world_view);
    multiply_matrix(&world_view,
            &device->transforms[D8WG_TRANSFORM_PROJECTION_SLOT],
            &world_view_projection);
    lighting = device->render_states[D3DRS_LIGHTING]
            && source_layout.normal >= 0;

    for (vertex = 0; vertex < vertex_count; ++vertex) {
        const BYTE *in = source->shadow
                + (source_start_index + vertex) * device->streams[0].stride;
        BYTE *out = destination->shadow
                + (destination_index + vertex) * destination_layout.stride;
        float position[4];
        float clip[4];
        float rhw;
        UINT index;

        position[0] = ((const float *)(in + source_layout.position))[0];
        position[1] = ((const float *)(in + source_layout.position))[1];
        position[2] = ((const float *)(in + source_layout.position))[2];
        position[3] = 1.0f;
        transform_vector4(&world_view_projection, position, clip);
        rhw = (clip[3] > 1e-6f || clip[3] < -1e-6f) ? 1.0f / clip[3] : 1.0f;
        /* Clip space -> the viewport's pixel rectangle, which is what makes
         * the result drawable as XYZRHW. */
        ((float *)(out + destination_layout.position))[0] =
                (clip[0] * rhw * 0.5f + 0.5f) * (float)device->viewport.Width
                + (float)device->viewport.X;
        ((float *)(out + destination_layout.position))[1] =
                (0.5f - clip[1] * rhw * 0.5f) * (float)device->viewport.Height
                + (float)device->viewport.Y;
        ((float *)(out + destination_layout.position))[2] =
                device->viewport.MinZ + clip[2] * rhw
                * (device->viewport.MaxZ - device->viewport.MinZ);
        ((float *)(out + destination_layout.position))[3] = rhw;

        if (destination_layout.diffuse >= 0) {
            if (lighting) {
                float eye_position[4];
                float normal[4];
                float eye_normal[4];
                float length;
                float lit[4];
                transform_vector4(&world_view, position, eye_position);
                normal[0] = ((const float *)(in + source_layout.normal))[0];
                normal[1] = ((const float *)(in + source_layout.normal))[1];
                normal[2] = ((const float *)(in + source_layout.normal))[2];
                normal[3] = 0.0f;
                transform_vector4(&world_view, normal, eye_normal);
                length = float_sqrt(eye_normal[0] * eye_normal[0]
                        + eye_normal[1] * eye_normal[1]
                        + eye_normal[2] * eye_normal[2]);
                if (length > 1e-6f) {
                    eye_normal[0] /= length;
                    eye_normal[1] /= length;
                    eye_normal[2] /= length;
                }
                light_vertex(device, eye_position, eye_normal, lit);
                *(D3DCOLOR *)(out + destination_layout.diffuse) =
                        pack_color(lit);
            } else if (source_layout.diffuse >= 0) {
                *(D3DCOLOR *)(out + destination_layout.diffuse) =
                        *(const D3DCOLOR *)(in + source_layout.diffuse);
            } else {
                *(D3DCOLOR *)(out + destination_layout.diffuse) = 0xffffffffu;
            }
        }
        if (destination_layout.specular >= 0) {
            *(D3DCOLOR *)(out + destination_layout.specular) =
                    source_layout.specular >= 0
                    ? *(const D3DCOLOR *)(in + source_layout.specular) : 0;
        }
        for (index = 0; index < destination_layout.texcoord_count; ++index) {
            UINT floats = destination_layout.texcoord_size[index];
            UINT component;
            if (destination_layout.texcoord[index] < 0) continue;
            for (component = 0; component < floats; ++component) {
                float value = 0.0f;
                if (index < source_layout.texcoord_count
                        && source_layout.texcoord[index] >= 0
                        && component < source_layout.texcoord_size[index]) {
                    value = ((const float *)(in
                            + source_layout.texcoord[index]))[component];
                }
                ((float *)(out
                        + destination_layout.texcoord[index]))[component] =
                        value;
            }
        }
    }

    /* The transformed vertices live in the destination's guest shadow; the
     * host needs them too, since the app will draw straight from the buffer. */
    return emit_buffer_update(destination->handle,
            destination_index * destination_layout.stride,
            destination->shadow + destination_index * destination_layout.stride,
            vertex_count * destination_layout.stride, 0)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
/*
 * Stage 6: D3D8 shader model 1.x. CreateVertexShader/CreatePixelShader parse
 * and reject anything outside the supported instruction set up front (see
 * validate_shader_body); a shader that fails validation never gets a handle
 * and never reaches the D8WG protocol, matching "illegal bytecode is
 * rejected, not silently mistranslated."
 */
static HRESULT WINAPI device_create_vertex_shader(IDirect3DDevice8 *iface,
        const DWORD *declaration, const DWORD *function, DWORD *shader,
        DWORD usage)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    UINT decl_count = 0;
    UINT body_count = 0;
    DWORD version;
    UINT minor;
    (void)usage;

    if (shader) *shader = 0;
    if (!declaration || !function || !shader)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    version = function[0];
    minor = (UINT)D3DSHADER_VERSION_MINOR(version);
    /* A device that advertises vs_1_1 must also run vs_1_0: 1.1 only adds the
     * address register, so 1.0 is a strict subset and the runtime hands it
     * straight to the driver. Refusing it looks like a driver that cannot do
     * vertex shaders at all -- 3DMark 2001 assembles most of its shaders as
     * `vs.1.0`, does not check the HRESULT, and dereferences the handle it
     * never got. */
    if ((version & 0xFFFF0000u) != 0xFFFE0000u
            || (UINT)D3DSHADER_VERSION_MAJOR(version) != 1u || minor > 1u) {
        D8WG_TRACE("SHADER REFUSE kind=vertex stage=version version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (!validate_vertex_declaration(declaration, D8WG_MAX_SHADER_TOKENS,
            &decl_count)) {
        D8WG_TRACE("SHADER REFUSE kind=vertex stage=declaration "
                "version=%08lX", version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (!validate_shader_body(function + 1, D8WG_MAX_SHADER_TOKENS, FALSE,
            minor, &body_count)) {
        D8WG_TRACE("SHADER REFUSE kind=vertex stage=body version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }

    entry = (D8Shader *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*entry));
    if (!entry) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    entry->declaration_tokens = decl_count
            ? (DWORD *)HeapAlloc(GetProcessHeap(), 0,
                    decl_count * sizeof(DWORD))
            : NULL;
    entry->code_tokens = (DWORD *)HeapAlloc(GetProcessHeap(), 0,
            (body_count + 1u) * sizeof(DWORD));
    if ((decl_count && !entry->declaration_tokens) || !entry->code_tokens) {
        HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    if (decl_count)
        CopyMemory(entry->declaration_tokens, declaration,
                decl_count * sizeof(DWORD));
    entry->declaration_token_count = decl_count;
    entry->code_tokens[0] = function[0];
    if (body_count)
        CopyMemory(entry->code_tokens + 1, function + 1,
                body_count * sizeof(DWORD));
    entry->code_token_count = body_count + 1u;
    entry->is_pixel_shader = FALSE;
    entry->major_version = 1;
    entry->minor_version = minor;
    shader_bytecode_hash(entry->code_tokens, entry->code_token_count,
            &entry->hash_low, &entry->hash_high);
    entry->handle = allocate_shader_handle();
    /* The declaration is a separate D9WG resource with its own handle, drawn
     * from the ordinary resource namespace rather than the shader one. */
    entry->declaration_handle = allocate_handle();

    if (!emit_create_vertex_shader(device, entry)) {
        HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    D8WG_TRACE("SHADER CREATE kind=vertex version=%08lX tokens=%u "
            "handle=%08lX hash=%08lX%08lX", version,
            entry->code_token_count, entry->handle,
            entry->hash_high, entry->hash_low);
    entry->next = device->vertex_shaders;
    device->vertex_shaders = entry;
    *shader = entry->handle;
    return D3D_OK;
}

static HRESULT WINAPI device_delete_vertex_shader(IDirect3DDevice8 *iface,
        DWORD shader)
{
    D8Device *device = device_from_iface(iface);
    D8Shader **link = &device->vertex_shaders;
    D8Shader *entry;
    if (!(shader & 1u))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    while (*link && (*link)->handle != shader) link = &(*link)->next;
    entry = *link;
    if (!entry)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* Deleting the shader that is currently set is legal: the runtime unbinds
     * it and destroys it, which is what Wine's d3d8 does and what 3DMark 2001
     * relies on between test scenes. Refusing left the object bound, alive and
     * unreachable -- a leak the app has no way to notice. Unbinding restores
     * fixed-function vertex processing, exactly as SetVertexShader(0) does. */
    if (device->vertex_shader == shader)
        device_set_vertex_shader(iface, 0);
    *link = entry->next;
    emit_destroy_shader(entry);
    HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
    HeapFree(GetProcessHeap(), 0, entry->code_tokens);
    HeapFree(GetProcessHeap(), 0, entry);
    return D3D_OK;
}

static HRESULT WINAPI device_set_vs_constant(IDirect3DDevice8 *iface,
        DWORD reg, const void *data, DWORD count)
{
    D8Device *device = device_from_iface(iface);
    if (!count) return D3D_OK;  /* see device_set_ps_constant */
    if (!data || (uint64_t)reg + count > D8WG_MAX_VS_CONSTANTS)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(device->vs_constants[reg], data, count * 16u);
    return emit_set_shader_constant(D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F,
            device, reg, (const float *)data, count)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_vs_constant(IDirect3DDevice8 *iface,
        DWORD reg, void *data, DWORD count)
{
    D8Device *device = device_from_iface(iface);
    if (!count) return D3D_OK;
    if (!data || (uint64_t)reg + count > D8WG_MAX_VS_CONSTANTS)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(data, device->vs_constants[reg], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_vs_decl(IDirect3DDevice8 *iface,
        DWORD shader, void *data, DWORD *size)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    DWORD needed;
    if (!(shader & 1u) || !size) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    entry = find_shader(device->vertex_shaders, shader);
    if (!entry) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    needed = (entry->declaration_token_count + 1u) * (DWORD)sizeof(DWORD);
    if (!data) { *size = needed; return D3D_OK; }
    if (*size < needed) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (entry->declaration_token_count)
        CopyMemory(data, entry->declaration_tokens,
                entry->declaration_token_count * sizeof(DWORD));
    ((DWORD *)data)[entry->declaration_token_count] = 0xFFFFFFFFu;
    *size = needed;
    return D3D_OK;
}

static HRESULT WINAPI device_get_vs_function(IDirect3DDevice8 *iface,
        DWORD shader, void *data, DWORD *size)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    DWORD needed;
    if (!(shader & 1u) || !size) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    entry = find_shader(device->vertex_shaders, shader);
    if (!entry) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    needed = (entry->code_token_count + 1u) * (DWORD)sizeof(DWORD);
    if (!data) { *size = needed; return D3D_OK; }
    if (*size < needed) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(data, entry->code_tokens,
            entry->code_token_count * sizeof(DWORD));
    ((DWORD *)data)[entry->code_token_count] = 0x0000FFFFu;
    *size = needed;
    return D3D_OK;
}

static HRESULT WINAPI device_create_pixel_shader(IDirect3DDevice8 *iface,
        const DWORD *function, DWORD *shader)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    UINT body_count = 0;
    UINT major;
    UINT minor;
    DWORD version;

    if (shader) *shader = 0;
    if (!function || !shader)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    version = function[0];
    major = (UINT)D3DSHADER_VERSION_MAJOR(version);
    minor = (UINT)D3DSHADER_VERSION_MINOR(version);
    /* ps_1_0 for the same reason vs_1_0 is accepted above: a ps_1_4 device
     * runs every earlier 1.x shader, and shader_opcode_supported() already
     * gates `cmp` (1.2+), `cnd` (up to 1.3) and the 1.4 forms on this minor,
     * so 1.0 validates as the subset it is. */
    if ((version & 0xFFFF0000u) != 0xFFFF0000u || major != 1u || minor > 4u) {
        D8WG_TRACE("SHADER REFUSE kind=pixel stage=version version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (!validate_shader_body(function + 1, D8WG_MAX_SHADER_TOKENS, TRUE,
            minor, &body_count)) {
        D8WG_TRACE("SHADER REFUSE kind=pixel stage=body version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }

    entry = (D8Shader *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*entry));
    if (!entry) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    entry->code_tokens = (DWORD *)HeapAlloc(GetProcessHeap(), 0,
            (body_count + 1u) * sizeof(DWORD));
    if (!entry->code_tokens) {
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    entry->code_tokens[0] = version;
    if (body_count)
        CopyMemory(entry->code_tokens + 1, function + 1,
                body_count * sizeof(DWORD));
    entry->code_token_count = body_count + 1u;
    entry->is_pixel_shader = TRUE;
    entry->major_version = major;
    entry->minor_version = minor;
    shader_bytecode_hash(entry->code_tokens, entry->code_token_count,
            &entry->hash_low, &entry->hash_high);
    entry->handle = allocate_shader_handle();

    if (!emit_create_pixel_shader(device, entry)) {
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    D8WG_TRACE("SHADER CREATE kind=pixel version=%08lX tokens=%u "
            "handle=%08lX hash=%08lX%08lX", version,
            entry->code_token_count, entry->handle,
            entry->hash_high, entry->hash_low);
    entry->next = device->pixel_shaders;
    device->pixel_shaders = entry;
    *shader = entry->handle;
    return D3D_OK;
}

static HRESULT WINAPI device_set_pixel_shader(IDirect3DDevice8 *iface,
        DWORD shader)
{
    D8Device *device = device_from_iface(iface);
    if (shader && (!(shader & 1u)
            || !find_shader(device->pixel_shaders, shader)))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block)
        device->recording_state_block->state.pixel_shader_mask = TRUE;
    if (device->pixel_shader == shader)
        return D3D_OK;
    device->pixel_shader = shader;
    return emit_set_shader(D9WG_OP_SET_PIXEL_SHADER, device, shader)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_pixel_shader(IDirect3DDevice8 *iface,
        DWORD *shader)
{
    if (!shader) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *shader = device_from_iface(iface)->pixel_shader;
    return D3D_OK;
}

static HRESULT WINAPI device_delete_pixel_shader(IDirect3DDevice8 *iface,
        DWORD shader)
{
    D8Device *device = device_from_iface(iface);
    D8Shader **link = &device->pixel_shaders;
    D8Shader *entry;
    if (!(shader & 1u))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    while (*link && (*link)->handle != shader) link = &(*link)->next;
    entry = *link;
    if (!entry)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* Unbind first, for the reason device_delete_vertex_shader gives. */
    if (device->pixel_shader == shader)
        device_set_pixel_shader(iface, 0);
    *link = entry->next;
    emit_destroy_shader(entry);
    HeapFree(GetProcessHeap(), 0, entry->code_tokens);
    HeapFree(GetProcessHeap(), 0, entry);
    return D3D_OK;
}

static HRESULT WINAPI device_set_ps_constant(IDirect3DDevice8 *iface,
        DWORD reg, const void *data, DWORD count)
{
    D8Device *device = device_from_iface(iface);
    /* A zero-vector write is a no-op, and the pointer is never read -- so it
     * is checked before the pointer is, not after. 3DMark 2001 issues
     * SetPixelShaderConstant(0, NULL, 0) before most draws (1179 times in one
     * benchmark run); refusing it makes an app that checks the HRESULT
     * abandon a frame the driver never had a problem with. */
    if (!count) return D3D_OK;
    if (!data || (uint64_t)reg + count > D8WG_MAX_PS_CONSTANTS) {
        D8WG_TRACE("PS_CONSTANT REFUSE register=%lu count=%lu data=%08lX "
                "limit=%u", reg, count, (DWORD)(uintptr_t)data,
                D8WG_MAX_PS_CONSTANTS);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    CopyMemory(device->ps_constants[reg], data, count * 16u);
    return emit_set_shader_constant(D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F,
            device, reg, (const float *)data, count)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_ps_constant(IDirect3DDevice8 *iface,
        DWORD reg, void *data, DWORD count)
{
    D8Device *device = device_from_iface(iface);
    if (!count) return D3D_OK;
    if (!data || (uint64_t)reg + count > D8WG_MAX_PS_CONSTANTS)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(data, device->ps_constants[reg], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_ps_function(IDirect3DDevice8 *iface,
        DWORD shader, void *data, DWORD *size)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    DWORD needed;
    if (!(shader & 1u) || !size) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    entry = find_shader(device->pixel_shaders, shader);
    if (!entry) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    needed = (entry->code_token_count + 1u) * (DWORD)sizeof(DWORD);
    if (!data) { *size = needed; return D3D_OK; }
    if (*size < needed) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    CopyMemory(data, entry->code_tokens,
            entry->code_token_count * sizeof(DWORD));
    ((DWORD *)data)[entry->code_token_count] = 0x0000FFFFu;
    *size = needed;
    return D3D_OK;
}
/* ---- Higher-order surfaces ---- */

/*
 * D3D8's patch calls draw a parametric surface from control points in the
 * current stream, tessellated by a per-edge segment count.
 *
 * Only D3DORDER_LINEAR is implemented, and for that case the result is exact
 * rather than approximate: a linear-order patch *is* its control polygon, so
 * subdividing it by interpolation reproduces the surface a hardware
 * tessellator would. D3DORDER_CUBIC and D3DORDER_QUINTIC evaluate a Bezier or
 * B-spline basis over 4x4/6x6 control points and are refused -- drawing their
 * control hull instead would silently render a visibly different, flatter
 * surface, which is the failure mode this codebase avoids by refusing.
 *
 * Patch handles are not cached. D3D8 lets an app pass a non-zero handle so the
 * runtime can keep the tessellated result, which is an optimisation rather
 * than a behaviour: re-tessellating per call gives the same picture, and
 * DeletePatch then has nothing to free.
 */
static UINT patch_segment_count(const float *segments, UINT index)
{
    float value = segments ? segments[index] : 1.0f;
    if (!(value >= 1.0f)) return 1u;
    if (value > 64.0f) return 64u;
    return (UINT)value;
}

/* Writes `weight`-blended vertex `a`/`b` into `out`, component by component
 * according to the FVF. Colours interpolate per channel; everything else is
 * float. */
static void blend_vertices(const D8FVFLayout *layout, const BYTE *a,
        const BYTE *b, float weight, BYTE *out)
{
    UINT index;
    UINT component;
    const float inverse = 1.0f - weight;

    for (component = 0; component < 3u; ++component) {
        ((float *)(out + layout->position))[component] =
                ((const float *)(a + layout->position))[component] * inverse
                + ((const float *)(b + layout->position))[component] * weight;
    }
    if (layout->pretransformed) {
        ((float *)(out + layout->position))[3] =
                ((const float *)(a + layout->position))[3] * inverse
                + ((const float *)(b + layout->position))[3] * weight;
    }
    if (layout->normal >= 0) {
        for (component = 0; component < 3u; ++component) {
            ((float *)(out + layout->normal))[component] =
                    ((const float *)(a + layout->normal))[component] * inverse
                    + ((const float *)(b + layout->normal))[component] * weight;
        }
    }
    for (index = 0; index < 2u; ++index) {
        int offset = index ? layout->specular : layout->diffuse;
        DWORD left;
        DWORD right;
        DWORD result = 0;
        UINT channel;
        if (offset < 0) continue;
        left = *(const DWORD *)(a + offset);
        right = *(const DWORD *)(b + offset);
        for (channel = 0; channel < 4u; ++channel) {
            UINT shift = channel * 8u;
            float value = ((left >> shift) & 0xffu) * inverse
                    + ((right >> shift) & 0xffu) * weight;
            DWORD byte = (DWORD)(value + 0.5f);
            if (byte > 255u) byte = 255u;
            result |= byte << shift;
        }
        *(DWORD *)(out + offset) = result;
    }
    for (index = 0; index < layout->texcoord_count; ++index) {
        if (layout->texcoord[index] < 0) continue;
        for (component = 0; component < layout->texcoord_size[index];
                ++component) {
            ((float *)(out + layout->texcoord[index]))[component] =
                    ((const float *)(a + layout->texcoord[index]))[component]
                        * inverse
                    + ((const float *)(b + layout->texcoord[index]))[component]
                        * weight;
        }
    }
}

/*
 * Tessellates and draws a grid whose four corners are the given control
 * vertices, as a triangle list through DrawIndexedPrimitiveUP.
 */
static HRESULT draw_tessellated_quad(D8Device *device,
        const D8FVFLayout *layout, const BYTE *corner00, const BYTE *corner10,
        const BYTE *corner01, const BYTE *corner11, UINT segments_u,
        UINT segments_v)
{
    const UINT columns = segments_u + 1u;
    const UINT rows = segments_v + 1u;
    const UINT vertex_count = columns * rows;
    const UINT index_count = segments_u * segments_v * 6u;
    BYTE *vertices;
    WORD *indices;
    BYTE *edge_low;
    BYTE *edge_high;
    D9WGDrawIndexedPrimitiveUP draw;
    HRESULT result = D3DERR_DRIVERINTERNALERROR;
    UINT row;
    UINT column;

    if (vertex_count > 0xFFFFu)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    vertices = (BYTE *)HeapAlloc(GetProcessHeap(), 0,
            vertex_count * layout->stride);
    indices = (WORD *)HeapAlloc(GetProcessHeap(), 0,
            index_count * sizeof(WORD));
    edge_low = (BYTE *)HeapAlloc(GetProcessHeap(), 0, layout->stride);
    edge_high = (BYTE *)HeapAlloc(GetProcessHeap(), 0, layout->stride);
    if (!vertices || !indices || !edge_low || !edge_high) {
        result = E_OUTOFMEMORY;
        goto done;
    }
    for (row = 0; row < rows; ++row) {
        float v = segments_v ? (float)row / (float)segments_v : 0.0f;
        blend_vertices(layout, corner00, corner01, v, edge_low);
        blend_vertices(layout, corner10, corner11, v, edge_high);
        for (column = 0; column < columns; ++column) {
            float u = segments_u ? (float)column / (float)segments_u : 0.0f;
            blend_vertices(layout, edge_low, edge_high, u,
                    vertices + (row * columns + column) * layout->stride);
        }
    }
    for (row = 0; row < segments_v; ++row) {
        for (column = 0; column < segments_u; ++column) {
            WORD *quad = indices + (row * segments_u + column) * 6u;
            WORD base = (WORD)(row * columns + column);
            quad[0] = base;
            quad[1] = (WORD)(base + 1u);
            quad[2] = (WORD)(base + columns);
            quad[3] = (WORD)(base + 1u);
            quad[4] = (WORD)(base + columns + 1u);
            quad[5] = (WORD)(base + columns);
        }
    }

    ZeroMemory(&draw, sizeof(draw));
    draw.device_handle = device->handle;
    draw.primitive_type = D3DPT_TRIANGLELIST;
    draw.min_vertex_index = 0;
    draw.vertex_count = vertex_count;
    draw.primitive_count = index_count / 3u;
    draw.index_format = D3DFMT_INDEX16;
    draw.stride = layout->stride;
    draw.index_count = index_count;
    draw.index_bytes = index_count * (UINT)sizeof(WORD);
    draw.vertex_bytes = vertex_count * layout->stride;
    result = emit_draw_indexed_primitive_up(&draw, indices, vertices)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;

done:
    HeapFree(GetProcessHeap(), 0, vertices);
    HeapFree(GetProcessHeap(), 0, indices);
    HeapFree(GetProcessHeap(), 0, edge_low);
    HeapFree(GetProcessHeap(), 0, edge_high);
    return D8WG_TRACE_ERROR(result);
}

static HRESULT WINAPI device_draw_rect_patch(IDirect3DDevice8 *iface,
        UINT handle, const float *segments, const D3DRECTPATCH_INFO *info)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *source = device->streams[0].buffer;
    D8FVFLayout layout;
    UINT stride = device->streams[0].stride;
    UINT segments_u;
    UINT segments_v;
    UINT row_stride;
    const BYTE *base;
    UINT last_vertex;

    (void)handle;
    /* No patch is cached, so a call that names one without describing it has
     * nothing to redraw. */
    if (!info || !source || source->locked || !stride)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (info->Order != D3DORDER_LINEAR)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* A linear-order rectangle patch is exactly its four corner control
     * points; any other extent is a higher-order description. */
    if (info->Width != 2u || info->Height != 2u)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->vertex_shader & 1u
            || !decode_fvf_layout(device->vertex_shader, &layout)
            || stride < layout.stride)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    row_stride = info->Stride ? info->Stride : info->Width;
    segments_u = patch_segment_count(segments, 0);
    segments_v = patch_segment_count(segments, 1);
    last_vertex = info->StartVertexOffsetHeight + 1u;
    if (last_vertex > 0xFFFFFFFFu / (row_stride ? row_stride : 1u))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    {
        UINT needed;
        UINT corner = (info->StartVertexOffsetHeight + 1u) * row_stride
                + info->StartVertexOffsetWidth + 1u;
        if (!multiply_u32(corner + 1u, stride, &needed)
                || needed > source->length)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    base = source->shadow
            + (info->StartVertexOffsetHeight * row_stride
                + info->StartVertexOffsetWidth) * stride;
    return draw_tessellated_quad(device, &layout,
            base,
            base + stride,
            base + row_stride * stride,
            base + (row_stride + 1u) * stride,
            segments_u, segments_v);
}

static HRESULT WINAPI device_draw_tri_patch(IDirect3DDevice8 *iface,
        UINT handle, const float *segments, const D3DTRIPATCH_INFO *info)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *source = device->streams[0].buffer;
    D8FVFLayout layout;
    UINT stride = device->streams[0].stride;
    UINT segments_u;
    const BYTE *base;
    UINT needed;

    (void)handle;
    if (!info || !source || source->locked || !stride)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (info->Order != D3DORDER_LINEAR || info->NumVertices != 3u)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->vertex_shader & 1u
            || !decode_fvf_layout(device->vertex_shader, &layout)
            || stride < layout.stride)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!multiply_u32(info->StartVertexOffset + 3u, stride, &needed)
            || needed > source->length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    segments_u = patch_segment_count(segments, 0);
    base = source->shadow + info->StartVertexOffset * stride;
    /* A triangle is the degenerate quad whose third and fourth corners
     * coincide, so one tessellator serves both patch shapes. */
    return draw_tessellated_quad(device, &layout, base, base + stride,
            base + 2u * stride, base + 2u * stride, segments_u, segments_u);
}
static HRESULT WINAPI device_delete_patch(IDirect3DDevice8 *iface, UINT handle)
{
    /* Nothing is cached (see device_draw_rect_patch), so there is nothing to
     * free -- and failing a delete for a handle the app was allowed to pass to
     * DrawRectPatch would be a worse answer than succeeding at a no-op. */
    (void)iface;
    (void)handle;
    return D3D_OK;
}

static IDirect3D8Vtbl g_d3d_vtbl = {
    .QueryInterface = d3d_query_interface,
    .AddRef = d3d_add_ref,
    .Release = d3d_release,
    .RegisterSoftwareDevice = d3d_register_software_device,
    .GetAdapterCount = d3d_get_adapter_count,
    .GetAdapterIdentifier = d3d_get_adapter_identifier,
    .GetAdapterModeCount = d3d_get_adapter_mode_count,
    .EnumAdapterModes = d3d_enum_adapter_modes,
    .GetAdapterDisplayMode = d3d_get_adapter_display_mode,
    .CheckDeviceType = d3d_check_device_type,
    .CheckDeviceFormat = d3d_check_device_format,
    .CheckDeviceMultiSampleType = d3d_check_multisample,
    .CheckDepthStencilMatch = d3d_check_depth_stencil,
    .GetDeviceCaps = d3d_get_device_caps,
    .GetAdapterMonitor = d3d_get_adapter_monitor,
    .CreateDevice = d3d_create_device
};

static IDirect3DDevice8Vtbl g_device_vtbl = {
    .QueryInterface = device_query_interface,
    .AddRef = device_add_ref,
    .Release = device_release,
    .TestCooperativeLevel = device_test_cooperative_level,
    .GetAvailableTextureMem = device_get_available_texture_mem,
    .ResourceManagerDiscardBytes = device_discard_bytes,
    .GetDirect3D = device_get_direct3d,
    .GetDeviceCaps = device_get_caps,
    .GetDisplayMode = device_get_display_mode,
    .GetCreationParameters = device_get_creation_parameters,
    .SetCursorProperties = device_set_cursor_properties,
    .SetCursorPosition = device_set_cursor_position,
    .ShowCursor = device_show_cursor,
    .CreateAdditionalSwapChain = device_create_swapchain,
    .Reset = device_reset,
    .Present = device_present,
    .GetBackBuffer = device_get_backbuffer,
    .GetRasterStatus = device_get_raster_status,
    .SetGammaRamp = device_set_gamma,
    .GetGammaRamp = device_get_gamma,
    .CreateTexture = device_create_texture,
    .CreateVolumeTexture = device_create_volume_texture,
    .CreateCubeTexture = device_create_cube_texture,
    .CreateVertexBuffer = device_create_vertex_buffer,
    .CreateIndexBuffer = device_create_index_buffer,
    .CreateRenderTarget = device_create_render_target,
    .CreateDepthStencilSurface = device_create_depth_surface,
    .CreateImageSurface = device_create_image_surface,
    .CopyRects = device_copy_rects,
    .UpdateTexture = device_update_texture,
    .GetFrontBuffer = device_get_front_buffer,
    .SetRenderTarget = device_set_render_target,
    .GetRenderTarget = device_get_render_target,
    .GetDepthStencilSurface = device_get_depth_surface,
    .BeginScene = device_begin_scene,
    .EndScene = device_end_scene,
    .Clear = device_clear,
    .SetTransform = device_set_transform,
    .GetTransform = device_get_transform,
    .MultiplyTransform = device_multiply_transform,
    .SetViewport = device_set_viewport,
    .GetViewport = device_get_viewport,
    .SetMaterial = device_set_material,
    .GetMaterial = device_get_material,
    .SetLight = device_set_light,
    .GetLight = device_get_light,
    .LightEnable = device_light_enable,
    .GetLightEnable = device_get_light_enable,
    .SetClipPlane = device_set_clip_plane,
    .GetClipPlane = device_get_clip_plane,
    .SetRenderState = device_set_render_state,
    .GetRenderState = device_get_render_state,
    .BeginStateBlock = device_begin_state_block,
    .EndStateBlock = device_end_state_block,
    .ApplyStateBlock = device_apply_state_block,
    .CaptureStateBlock = device_capture_state_block,
    .DeleteStateBlock = device_delete_state_block,
    .CreateStateBlock = device_create_state_block,
    .SetClipStatus = device_set_clip_status,
    .GetClipStatus = device_get_clip_status,
    .GetTexture = device_get_texture,
    .SetTexture = device_set_texture,
    .GetTextureStageState = device_get_texture_stage_state,
    .SetTextureStageState = device_set_texture_stage_state,
    .ValidateDevice = device_validate,
    .GetInfo = device_get_info,
    .SetPaletteEntries = device_set_palette,
    .GetPaletteEntries = device_get_palette,
    .SetCurrentTexturePalette = device_set_current_palette,
    .GetCurrentTexturePalette = device_get_current_palette,
    .DrawPrimitive = device_draw_primitive,
    .DrawIndexedPrimitive = device_draw_indexed,
    .DrawPrimitiveUP = device_draw_up,
    .DrawIndexedPrimitiveUP = device_draw_indexed_up,
    .ProcessVertices = device_process_vertices,
    .CreateVertexShader = device_create_vertex_shader,
    .SetVertexShader = device_set_vertex_shader,
    .GetVertexShader = device_get_vertex_shader,
    .DeleteVertexShader = device_delete_vertex_shader,
    .SetVertexShaderConstant = device_set_vs_constant,
    .GetVertexShaderConstant = device_get_vs_constant,
    .GetVertexShaderDeclaration = device_get_vs_decl,
    .GetVertexShaderFunction = device_get_vs_function,
    .SetStreamSource = device_set_stream_source,
    .GetStreamSource = device_get_stream_source,
    .SetIndices = device_set_indices,
    .GetIndices = device_get_indices,
    .CreatePixelShader = device_create_pixel_shader,
    .SetPixelShader = device_set_pixel_shader,
    .GetPixelShader = device_get_pixel_shader,
    .DeletePixelShader = device_delete_pixel_shader,
    .SetPixelShaderConstant = device_set_ps_constant,
    .GetPixelShaderConstant = device_get_ps_constant,
    .GetPixelShaderFunction = device_get_ps_function,
    .DrawRectPatch = device_draw_rect_patch,
    .DrawTriPatch = device_draw_tri_patch,
    .DeletePatch = device_delete_patch
};

static IDirect3DVertexBuffer8Vtbl g_vb_vtbl = {
    .QueryInterface = vb_query_interface,
    .AddRef = vb_add_ref,
    .Release = vb_release,
    .GetDevice = vb_get_device,
    .SetPrivateData = vb_set_private_data,
    .GetPrivateData = vb_get_private_data,
    .FreePrivateData = vb_free_private_data,
    .SetPriority = vb_set_priority,
    .GetPriority = vb_get_priority,
    .PreLoad = vb_preload,
    .GetType = vb_get_type,
    .Lock = vb_lock,
    .Unlock = vb_unlock,
    .GetDesc = vb_get_desc
};

static IDirect3DIndexBuffer8Vtbl g_ib_vtbl = {
    .QueryInterface = ib_query_interface,
    .AddRef = ib_add_ref,
    .Release = ib_release,
    .GetDevice = ib_get_device,
    .SetPrivateData = ib_set_private_data,
    .GetPrivateData = ib_get_private_data,
    .FreePrivateData = ib_free_private_data,
    .SetPriority = ib_set_priority,
    .GetPriority = ib_get_priority,
    .PreLoad = ib_preload,
    .GetType = ib_get_type,
    .Lock = ib_lock,
    .Unlock = ib_unlock,
    .GetDesc = ib_get_desc
};


/* ------------------------------------------------------------------ *
 * Cube textures
 * ------------------------------------------------------------------ */

static BOOL emit_cube_texture_create(D8Device *device, D8CubeTexture *texture)
{
    D9WGCreateTextureCube command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.edge_length = texture->edge_length;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_CUBE, &command,
            sizeof(command));
}

/* Face-major: see the comment on D8CubeTexture. */
static D8TextureLevel *cube_level(D8CubeTexture *texture,
        D3DCUBEMAP_FACES face, UINT level)
{
    return &texture->levels[(UINT)face * texture->level_count + level];
}

static HRESULT cube_lock_face(D8CubeTexture *texture, D3DCUBEMAP_FACES face,
        UINT level, D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    if ((UINT)face >= 6u || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return lock_texture_level(cube_level(texture, face, level),
            texture->format, TRUE, locked_rect, rect, flags);
}

static HRESULT cube_unlock_face(D8CubeTexture *texture, D3DCUBEMAP_FACES face,
        UINT level)
{
    if ((UINT)face >= 6u || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return unlock_texture_level(texture->handle, texture->format,
            cube_level(texture, face, level), level, (UINT)face);
}

static HRESULT WINAPI cube_query_interface(IDirect3DCubeTexture8 *iface,
        REFIID iid, void **object)
{
    if (!object) return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
            && !guid_equal(iid, &IID_IDirect3DCubeTexture8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DCubeTexture8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI cube_add_ref(IDirect3DCubeTexture8 *iface)
{
    return (ULONG)InterlockedIncrement(&cube_from_iface(iface)->refcount);
}

static ULONG WINAPI cube_release(IDirect3DCubeTexture8 *iface)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D8CubeTexture **link = &texture->device->cube_resources;
        D9WGDestroyResource destroy;
        UINT index;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_CUBE;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (index = 0; index < texture->level_count * 6u; ++index)
            HeapFree(GetProcessHeap(), 0, texture->levels[index].shadow);
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
    }
    return refs;
}

static HRESULT WINAPI cube_get_device(IDirect3DCubeTexture8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    if (!device_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &texture->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI cube_set_private_data(IDirect3DCubeTexture8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }

static HRESULT WINAPI cube_get_private_data(IDirect3DCubeTexture8 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static HRESULT WINAPI cube_free_private_data(IDirect3DCubeTexture8 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static DWORD WINAPI cube_set_priority(IDirect3DCubeTexture8 *iface,
        DWORD priority)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    DWORD old = texture->priority;
    texture->priority = priority;
    return old;
}

static DWORD WINAPI cube_get_priority(IDirect3DCubeTexture8 *iface)
{ return cube_from_iface(iface)->priority; }

static void WINAPI cube_preload(IDirect3DCubeTexture8 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI cube_get_type(IDirect3DCubeTexture8 *iface)
{ (void)iface; return D3DRTYPE_CUBETEXTURE; }

static DWORD WINAPI cube_set_lod(IDirect3DCubeTexture8 *iface, DWORD lod)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    DWORD old = texture->lod;
    if (lod >= texture->level_count) lod = texture->level_count - 1;
    texture->lod = lod;
    return old;
}

static DWORD WINAPI cube_get_lod(IDirect3DCubeTexture8 *iface)
{ return cube_from_iface(iface)->lod; }

static DWORD WINAPI cube_get_level_count(IDirect3DCubeTexture8 *iface)
{ return cube_from_iface(iface)->level_count; }

static HRESULT WINAPI cube_get_level_desc(IDirect3DCubeTexture8 *iface,
        UINT level, D3DSURFACE_DESC *desc)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    D8TextureLevel *level_data;
    if (!desc || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = cube_level(texture, D3DCUBEMAP_FACE_POSITIVE_X, level);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->Size = level_data->byte_count;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    return D3D_OK;
}

static HRESULT WINAPI cube_get_surface(IDirect3DCubeTexture8 *iface,
        D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface8 **surface_out)
{
    D8CubeTexture *texture = cube_from_iface(iface);
    D8Surface *surface;
    if (!surface_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if ((UINT)face >= 6u || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    surface = (D8Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->cube = texture;
    surface->face = (UINT)face;
    surface->device = texture->device;
    surface->level = level;
    IDirect3DCubeTexture8_AddRef(iface);
    *surface_out = &surface->iface;
    return D3D_OK;
}

static HRESULT WINAPI cube_lock_rect(IDirect3DCubeTexture8 *iface,
        D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked_rect,
        const RECT *rect, DWORD flags)
{
    return cube_lock_face(cube_from_iface(iface), face, level, locked_rect,
            rect, flags);
}

static HRESULT WINAPI cube_unlock_rect(IDirect3DCubeTexture8 *iface,
        D3DCUBEMAP_FACES face, UINT level)
{
    return cube_unlock_face(cube_from_iface(iface), face, level);
}

static HRESULT WINAPI cube_add_dirty_rect(IDirect3DCubeTexture8 *iface,
        D3DCUBEMAP_FACES face, const RECT *rect)
{
    /* Every unlock uploads its locked rectangle already, so a dirty-rect hint
     * has nothing left to do; saying so is not the same as failing. */
    (void)iface; (void)face; (void)rect;
    return D3D_OK;
}

/* ------------------------------------------------------------------ *
 * Volume textures
 * ------------------------------------------------------------------ */

static BOOL emit_volume_texture_create(D8Device *device,
        D8VolumeTexture *texture)
{
    D9WGCreateTextureVolume command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.depth = texture->depth;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_VOLUME, &command,
            sizeof(command));
}

/*
 * A volume level's slices are uploaded one at a time, because D9WG's
 * UPDATE_TEXTURE addresses a single 2D plane at `z`. Sending the whole box as
 * one record would need a slice-pitch-aware host path that nothing else uses.
 */
static BOOL emit_volume_level_update(D8VolumeTexture *texture, UINT level,
        const RECT *rect, UINT first_slice, UINT slice_count)
{
    D8TextureLevel *level_data = &texture->levels[level];
    D8TextureLevel slice = *level_data;
    UINT index;

    slice.depth = 1;
    slice.byte_count = level_data->slice_pitch;
    for (index = 0; index < slice_count; ++index) {
        slice.shadow = level_data->shadow
                + (first_slice + index) * level_data->slice_pitch;
        if (!emit_level_update(texture->handle, texture->format, &slice,
                level, first_slice + index, rect))
            return FALSE;
    }
    return TRUE;
}

static HRESULT volume_lock_level(D8VolumeTexture *texture, UINT level,
        D3DLOCKED_BOX *locked_box, const D3DBOX *box, DWORD flags)
{
    D8TextureLevel *level_data;
    D3DLOCKED_RECT locked_rect;
    RECT area;
    UINT front;
    UINT back;
    HRESULT hr;

    if (!locked_box || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (box) {
        if (box->Right <= box->Left || box->Bottom <= box->Top
                || box->Back <= box->Front
                || box->Right > level_data->width
                || box->Bottom > level_data->height
                || box->Back > level_data->depth)
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        SetRect(&area, (int)box->Left, (int)box->Top, (int)box->Right,
                (int)box->Bottom);
        front = box->Front;
        back = box->Back;
    } else {
        SetRect(&area, 0, 0, (int)level_data->width, (int)level_data->height);
        front = 0;
        back = level_data->depth;
    }
    hr = lock_texture_level(level_data, texture->format, TRUE, &locked_rect,
            &area, flags);
    if (FAILED(hr)) return D8WG_TRACE_ERROR(hr);
    level_data->lock_z = front;
    level_data->lock_depth = back - front;
    locked_box->RowPitch = locked_rect.Pitch;
    locked_box->SlicePitch = (INT)level_data->slice_pitch;
    locked_box->pBits = (BYTE *)locked_rect.pBits
            + front * level_data->slice_pitch;
    return D3D_OK;
}

static HRESULT volume_unlock_level(D8VolumeTexture *texture, UINT level)
{
    D8TextureLevel *level_data;
    BOOL result = TRUE;

    if (level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (!level_data->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!(level_data->lock_flags & D3DLOCK_READONLY))
        result = emit_volume_level_update(texture, level,
                &level_data->lock_rect, level_data->lock_z,
                level_data->lock_depth);
    level_data->locked = FALSE;
    level_data->lock_flags = 0;
    level_data->lock_z = 0;
    level_data->lock_depth = 0;
    ZeroMemory(&level_data->lock_rect, sizeof(level_data->lock_rect));
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI volume_texture_query_interface(
        IDirect3DVolumeTexture8 *iface, REFIID iid, void **object)
{
    if (!object) return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
            && !guid_equal(iid, &IID_IDirect3DVolumeTexture8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DVolumeTexture8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI volume_texture_add_ref(IDirect3DVolumeTexture8 *iface)
{
    return (ULONG)InterlockedIncrement(
            &volume_texture_from_iface(iface)->refcount);
}

static ULONG WINAPI volume_texture_release(IDirect3DVolumeTexture8 *iface)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D8VolumeTexture **link = &texture->device->volume_resources;
        D9WGDestroyResource destroy;
        UINT level;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_VOLUME;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (level = 0; level < texture->level_count; ++level)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
    }
    return refs;
}

static HRESULT WINAPI volume_texture_get_device(IDirect3DVolumeTexture8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    if (!device_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &texture->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_set_private_data(
        IDirect3DVolumeTexture8 *iface, REFGUID guid, const void *data,
        DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }

static HRESULT WINAPI volume_texture_get_private_data(
        IDirect3DVolumeTexture8 *iface, REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static HRESULT WINAPI volume_texture_free_private_data(
        IDirect3DVolumeTexture8 *iface, REFGUID guid)
{ (void)iface; (void)guid; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static DWORD WINAPI volume_texture_set_priority(
        IDirect3DVolumeTexture8 *iface, DWORD priority)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    DWORD old = texture->priority;
    texture->priority = priority;
    return old;
}

static DWORD WINAPI volume_texture_get_priority(IDirect3DVolumeTexture8 *iface)
{ return volume_texture_from_iface(iface)->priority; }

static void WINAPI volume_texture_preload(IDirect3DVolumeTexture8 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI volume_texture_get_type(
        IDirect3DVolumeTexture8 *iface)
{ (void)iface; return D3DRTYPE_VOLUMETEXTURE; }

static DWORD WINAPI volume_texture_set_lod(IDirect3DVolumeTexture8 *iface,
        DWORD lod)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    DWORD old = texture->lod;
    if (lod >= texture->level_count) lod = texture->level_count - 1;
    texture->lod = lod;
    return old;
}

static DWORD WINAPI volume_texture_get_lod(IDirect3DVolumeTexture8 *iface)
{ return volume_texture_from_iface(iface)->lod; }

static DWORD WINAPI volume_texture_get_level_count(
        IDirect3DVolumeTexture8 *iface)
{ return volume_texture_from_iface(iface)->level_count; }

static HRESULT WINAPI volume_texture_get_level_desc(
        IDirect3DVolumeTexture8 *iface, UINT level, D3DVOLUME_DESC *desc)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    D8TextureLevel *level_data;
    if (!desc || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_VOLUME;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->Size = level_data->byte_count;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    desc->Depth = level_data->depth;
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_get_volume_level(
        IDirect3DVolumeTexture8 *iface, UINT level,
        IDirect3DVolume8 **volume_out)
{
    D8VolumeTexture *texture = volume_texture_from_iface(iface);
    D8Volume *volume;
    if (!volume_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *volume_out = NULL;
    if (level >= texture->level_count) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    volume = (D8Volume *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*volume));
    if (!volume) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    volume->iface.lpVtbl = &g_volume_vtbl;
    volume->refcount = 1;
    volume->texture = texture;
    volume->level = level;
    IDirect3DVolumeTexture8_AddRef(iface);
    *volume_out = &volume->iface;
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_lock_box(IDirect3DVolumeTexture8 *iface,
        UINT level, D3DLOCKED_BOX *locked_box, const D3DBOX *box, DWORD flags)
{
    return volume_lock_level(volume_texture_from_iface(iface), level,
            locked_box, box, flags);
}

static HRESULT WINAPI volume_texture_unlock_box(IDirect3DVolumeTexture8 *iface,
        UINT level)
{
    return volume_unlock_level(volume_texture_from_iface(iface), level);
}

static HRESULT WINAPI volume_texture_add_dirty_box(
        IDirect3DVolumeTexture8 *iface, const D3DBOX *box)
{ (void)iface; (void)box; return D3D_OK; }

/* ---- IDirect3DVolume8 ---- */

static HRESULT WINAPI volume_query_interface(IDirect3DVolume8 *iface,
        REFIID iid, void **object)
{
    if (!object) return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DVolume8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3DVolume8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI volume_add_ref(IDirect3DVolume8 *iface)
{ return (ULONG)InterlockedIncrement(&volume_from_iface(iface)->refcount); }

static ULONG WINAPI volume_release(IDirect3DVolume8 *iface)
{
    D8Volume *volume = volume_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&volume->refcount);
    if (!refs) {
        IDirect3DVolumeTexture8_Release(&volume->texture->iface);
        HeapFree(GetProcessHeap(), 0, volume);
    }
    return refs;
}

static HRESULT WINAPI volume_get_device(IDirect3DVolume8 *iface,
        IDirect3DDevice8 **device_out)
{
    D8Volume *volume = volume_from_iface(iface);
    if (!device_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = &volume->texture->device->iface;
    IDirect3DDevice8_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI volume_set_private_data(IDirect3DVolume8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL); }

static HRESULT WINAPI volume_get_private_data(IDirect3DVolume8 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static HRESULT WINAPI volume_free_private_data(IDirect3DVolume8 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return D8WG_TRACE_ERROR(D3DERR_NOTFOUND); }

static HRESULT WINAPI volume_get_container(IDirect3DVolume8 *iface,
        REFIID iid, void **container)
{
    D8Volume *volume = volume_from_iface(iface);
    if (!container) return D8WG_TRACE_ERROR(E_POINTER);
    *container = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource8)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture8)
            && !guid_equal(iid, &IID_IDirect3DVolumeTexture8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *container = &volume->texture->iface;
    IDirect3DVolumeTexture8_AddRef(&volume->texture->iface);
    return S_OK;
}

static HRESULT WINAPI volume_get_desc(IDirect3DVolume8 *iface,
        D3DVOLUME_DESC *desc)
{
    D8Volume *volume = volume_from_iface(iface);
    return volume_texture_get_level_desc(&volume->texture->iface,
            volume->level, desc);
}

static HRESULT WINAPI volume_lock_box(IDirect3DVolume8 *iface,
        D3DLOCKED_BOX *locked_box, const D3DBOX *box, DWORD flags)
{
    D8Volume *volume = volume_from_iface(iface);
    return volume_lock_level(volume->texture, volume->level, locked_box, box,
            flags);
}

static HRESULT WINAPI volume_unlock_box(IDirect3DVolume8 *iface)
{
    D8Volume *volume = volume_from_iface(iface);
    return volume_unlock_level(volume->texture, volume->level);
}


static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice8 *iface,
        UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DCubeTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    D8CubeTexture *texture;
    UINT full_levels;
    UINT level;
    UINT face;
    UINT level_edge;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!edge_length || edge_length > 4096
            || !supported_texture_format(format)
            || (usage & D3DUSAGE_DEPTHSTENCIL)
            || ((usage & D3DUSAGE_RENDERTARGET)
                && (pool != D3DPOOL_DEFAULT
                    || !supported_render_target_format(format)))
            || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(edge_length, edge_length);
    if (!levels) levels = full_levels;
    if (levels > full_levels) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8CubeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * 6u * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_cube_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->edge_length = edge_length;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    for (face = 0; face < 6u; ++face) {
        level_edge = edge_length;
        for (level = 0; level < levels; ++level) {
            if (!allocate_texture_level(
                    &texture->levels[face * levels + level], format,
                    level_edge, level_edge, 1u))
                goto allocation_failed;
            if (level_edge > 1) level_edge >>= 1;
        }
    }
    if (!emit_cube_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->cube_resources;
    device->cube_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels * 6u; ++level)
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}

static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice8 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    D8VolumeTexture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    UINT level_depth;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!width || !height || !depth || width > 2048 || height > 2048
            || depth > 2048
            || !supported_texture_format(format)
            /* A volume is never a render target or a depth buffer, and the
             * block-compressed formats have no 3D layout this path lays out. */
            || (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            || format == D3DFMT_DXT1 || format == D3DFMT_DXT2
            || format == D3DFMT_DXT3 || format == D3DFMT_DXT4
            || format == D3DFMT_DXT5
            || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(width,
            height > depth ? height : depth);
    if (!levels) levels = full_levels;
    if (levels > full_levels) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8VolumeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_volume_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    level_depth = depth;
    for (level = 0; level < levels; ++level) {
        if (!allocate_texture_level(&texture->levels[level], format,
                level_width, level_height, level_depth))
            goto allocation_failed;
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
        if (level_depth > 1) level_depth >>= 1;
    }
    if (!emit_volume_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->volume_resources;
    device->volume_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels; ++level)
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}

static IDirect3DTexture8Vtbl g_texture_vtbl = {
    .QueryInterface = texture_query_interface,
    .AddRef = texture_add_ref,
    .Release = texture_release,
    .GetDevice = texture_get_device,
    .SetPrivateData = texture_set_private_data,
    .GetPrivateData = texture_get_private_data,
    .FreePrivateData = texture_free_private_data,
    .SetPriority = texture_set_priority,
    .GetPriority = texture_get_priority,
    .PreLoad = texture_preload,
    .GetType = texture_get_type,
    .SetLOD = texture_set_lod,
    .GetLOD = texture_get_lod,
    .GetLevelCount = texture_get_level_count,
    .GetLevelDesc = texture_get_level_desc,
    .GetSurfaceLevel = texture_get_surface_level,
    .LockRect = texture_lock_rect,
    .UnlockRect = texture_unlock_rect,
    .AddDirtyRect = texture_add_dirty_rect
};


static IDirect3DCubeTexture8Vtbl g_cube_texture_vtbl = {
    .QueryInterface = cube_query_interface,
    .AddRef = cube_add_ref,
    .Release = cube_release,
    .GetDevice = cube_get_device,
    .SetPrivateData = cube_set_private_data,
    .GetPrivateData = cube_get_private_data,
    .FreePrivateData = cube_free_private_data,
    .SetPriority = cube_set_priority,
    .GetPriority = cube_get_priority,
    .PreLoad = cube_preload,
    .GetType = cube_get_type,
    .SetLOD = cube_set_lod,
    .GetLOD = cube_get_lod,
    .GetLevelCount = cube_get_level_count,
    .GetLevelDesc = cube_get_level_desc,
    .GetCubeMapSurface = cube_get_surface,
    .LockRect = cube_lock_rect,
    .UnlockRect = cube_unlock_rect,
    .AddDirtyRect = cube_add_dirty_rect
};

static IDirect3DVolumeTexture8Vtbl g_volume_texture_vtbl = {
    .QueryInterface = volume_texture_query_interface,
    .AddRef = volume_texture_add_ref,
    .Release = volume_texture_release,
    .GetDevice = volume_texture_get_device,
    .SetPrivateData = volume_texture_set_private_data,
    .GetPrivateData = volume_texture_get_private_data,
    .FreePrivateData = volume_texture_free_private_data,
    .SetPriority = volume_texture_set_priority,
    .GetPriority = volume_texture_get_priority,
    .PreLoad = volume_texture_preload,
    .GetType = volume_texture_get_type,
    .SetLOD = volume_texture_set_lod,
    .GetLOD = volume_texture_get_lod,
    .GetLevelCount = volume_texture_get_level_count,
    .GetLevelDesc = volume_texture_get_level_desc,
    .GetVolumeLevel = volume_texture_get_volume_level,
    .LockBox = volume_texture_lock_box,
    .UnlockBox = volume_texture_unlock_box,
    .AddDirtyBox = volume_texture_add_dirty_box
};

static IDirect3DVolume8Vtbl g_volume_vtbl = {
    .QueryInterface = volume_query_interface,
    .AddRef = volume_add_ref,
    .Release = volume_release,
    .GetDevice = volume_get_device,
    .SetPrivateData = volume_set_private_data,
    .GetPrivateData = volume_get_private_data,
    .FreePrivateData = volume_free_private_data,
    .GetContainer = volume_get_container,
    .GetDesc = volume_get_desc,
    .LockBox = volume_lock_box,
    .UnlockBox = volume_unlock_box
};

static IDirect3DSurface8Vtbl g_surface_vtbl = {
    .QueryInterface = surface_query_interface,
    .AddRef = surface_add_ref,
    .Release = surface_release,
    .GetDevice = surface_get_device,
    .SetPrivateData = surface_set_private_data,
    .GetPrivateData = surface_get_private_data,
    .FreePrivateData = surface_free_private_data,
    .GetContainer = surface_get_container,
    .GetDesc = surface_get_desc,
    .LockRect = surface_lock_rect,
    .UnlockRect = surface_unlock_rect
};

static IDirect3DSwapChain8Vtbl g_swapchain_vtbl = {
    .QueryInterface = swapchain_query_interface,
    .AddRef = swapchain_add_ref,
    .Release = swapchain_release,
    .Present = swapchain_present,
    .GetBackBuffer = swapchain_get_backbuffer
};

/*
 * Both SDK version tokens seen in the wild must be accepted, exactly as the
 * real d3d8.dll does. Titles compiled against the DirectX 8.0 SDK pass 120
 * and titles compiled against 8.1 pass 220 (D3D_SDK_VERSION); rejecting 120
 * makes Direct3DCreate8 return NULL at the very first call, which a game can
 * only report as a generic "unable to initialize DirectX".
 */
#define D3D_SDK_VERSION_DX80 120u
#define D3D_SDK_VERSION_DX81 220u

IDirect3D8 *WINAPI Direct3DCreate8(UINT sdk_version)
{
    D8Direct3D *d3d;
    BOOL transport_ready;

    D8WG_TRACE("CALL Direct3DCreate8 sdk=%lu", sdk_version);
    if (sdk_version != D3D_SDK_VERSION_DX80
            && sdk_version != D3D_SDK_VERSION_DX81) {
        D8WG_TRACE("FAIL Direct3DCreate8 sdk=%lu expected=%lu|%lu",
                sdk_version, D3D_SDK_VERSION_DX80, D3D_SDK_VERSION_DX81);
        return NULL;
    }
    EnterCriticalSection(&g_transport_lock);
    transport_ready = open_transport_locked();
    LeaveCriticalSection(&g_transport_lock);
    if (!transport_ready) {
        D8WG_TRACE("FAIL Direct3DCreate8 transport unavailable");
        return NULL;
    }

    d3d = (D8Direct3D *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*d3d));
    if (!d3d) {
        D8WG_TRACE("FAIL Direct3DCreate8 allocation win32_last=%lu",
                GetLastError());
        return NULL;
    }
    d3d->iface.lpVtbl = &g_d3d_vtbl;
    d3d->refcount = 1;
    emit_hello_once();
    D8WG_TRACE("OK Direct3DCreate8 object=%08lX",
            (DWORD)(uintptr_t)&d3d->iface);
    return &d3d->iface;
}

/*
 * Secondary d3d8.dll exports.
 *
 * These exist only so that a title which statically imports them can load at
 * all; omitting them makes the loader reject the DLL before Direct3DCreate8 is
 * ever reached. The shader validators accept everything: real validation
 * already happens in CreateVertexShader/CreatePixelShader, which parse the
 * token stream against the Stage 6 supported-instruction table and reject
 * anything they cannot translate.
 */
HRESULT WINAPI ValidateVertexShader(const DWORD *shader, const DWORD *declaration,
        const D3DCAPS8 *caps, WINBOOL return_error, char **errors)
{
    (void)shader;
    (void)declaration;
    (void)caps;
    (void)return_error;
    if (errors) *errors = NULL;
    return S_OK;
}

HRESULT WINAPI ValidatePixelShader(const DWORD *shader, const D3DCAPS8 *caps,
        WINBOOL return_error, char **errors)
{
    (void)shader;
    (void)caps;
    (void)return_error;
    if (errors) *errors = NULL;
    return S_OK;
}

void WINAPI DebugSetMute(void)
{
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
#ifdef D8WG_DIAGNOSTIC_TRACE
    DWORD pending_commands;
#else
    (void)reserved;
#endif
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        initialize_session_id(instance);
        InitializeCriticalSection(&g_transport_lock);
#ifdef D8WG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_attach(instance);
#endif
    } else if (reason == DLL_PROCESS_DETACH) {
#ifdef D8WG_DIAGNOSTIC_TRACE
        pending_commands = g_command_count;
#endif
        EnterCriticalSection(&g_transport_lock);
        if (g_command_count)
            submit_batch_locked(FALSE);
        close_transport_locked();
        LeaveCriticalSection(&g_transport_lock);
        DeleteCriticalSection(&g_transport_lock);
#ifdef D8WG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_detach(reserved, pending_commands);
#endif
    }
    return TRUE;
}
