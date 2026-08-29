#ifndef D9WG_PROTOCOL_H
#define D9WG_PROTOCOL_H

#include <stdint.h>

/*
 * D9WG is an independent protocol from D8WG: it
 * does not share opcode numbering, resource handle namespace, or payload
 * shapes with the D3D8 path, even though both ride the same VGL2 DMA
 * transport (v86gl.sys, 16 MiB ring, PCI BAR) via a different outer record
 * type. A guest process loads either d3d8.dll or d3d9.dll, never both.
 *
 * Version 1.7 adds the DirectDraw group (0x500) for ../ddrawproxy/ddraw.dll.
 * It is additive only -- no existing structure changes -- so d3d8.dll and
 * d3d9.dll do not need rebuilding for it.
 *
 * Version 1.3 deliberately breaks the old layout: it adds multisample fields,
 * volume textures, instancing, clip planes, GPU queries, and an asynchronous
 * host-to-guest response tail for queries and render-target readback. The
 * guest DLL and page executor are updated as one unit, so no compatibility
 * decoder for pre-1.3 batches is retained.
 */
#define V86GL_CTRL_D3D9_BATCH 0xFFE1u

#define D9WG_MAGIC 0x47573944u /* "D9WG" */
#define D9WG_VERSION_MAJOR 1u
#define D9WG_VERSION_MINOR 7u

/* The last four MiB of v86gl.sys's mapped DMA allocation are never used for
 * command batches.  The browser writes asynchronous query/readback results
 * back into this region by adding these offsets to the submitted descriptor's
 * physical base address. */
#define D9WG_RESPONSE_REGION_BYTES (4u * 1024u * 1024u)
#define D9WG_QUERY_SLOT_BYTES 16u
#define D9WG_QUERY_SLOT_COUNT 1024u
#define D9WG_QUERY_REGION_BYTES \
    (D9WG_QUERY_SLOT_BYTES * D9WG_QUERY_SLOT_COUNT)
#define D9WG_READBACK_REGION_OFFSET D9WG_QUERY_REGION_BYTES

/*
 * A liveness counter the host bumps once per batch it finishes, in the last
 * bytes of the response region.
 *
 * It exists because a readback is a *synchronous* request -- the guest spins
 * until the host answers -- while batch submission has no backpressure at all:
 * the PCI write returns immediately and the host works through a queue. A host
 * that has fallen thousands of batches behind (3DMark06's Image Quality test
 * renders ~3.7M draws before asking for its frame dump) answers correctly, just
 * far later than any wall-clock deadline the guest could pick. Timing out on
 * elapsed time therefore reports "readback failed" for a host that is merely
 * busy, which is a different fault with a different fix.
 *
 * Watching this instead makes the deadline mean what it should: give up only
 * when the host has stopped making progress at all.
 */
#define D9WG_HEARTBEAT_BYTES 16u
#define D9WG_HEARTBEAT_OFFSET \
    (D9WG_RESPONSE_REGION_BYTES - D9WG_HEARTBEAT_BYTES)

#define D9WG_RESPONSE_PENDING 0u
#define D9WG_RESPONSE_OK 1u
#define D9WG_RESPONSE_FAILED 2u

#define D9WG_BATCH_FLAG_PRESENT (1u << 0)

/*
 * D9WGHello.feature_bits. The host does not gate anything on these; they let
 * diagnostics report whether the active DLL selected the FFP, SM2, or default
 * SM3 caps profile without guessing from rendered behaviour.
 */
#define D9WG_FEATURE_SHADER_MODEL_2 (1u << 0)
#define D9WG_FEATURE_SHADER_MODEL_3 (1u << 1)

enum D9WGOpcode {
    D9WG_OP_HELLO = 1,
    D9WG_OP_CREATE_DEVICE = 2,
    D9WG_OP_RESET = 3,
    D9WG_OP_PRESENT = 4,
    D9WG_OP_CLEAR = 5,
    D9WG_OP_BEGIN_SCENE = 6,
    D9WG_OP_END_SCENE = 7,
    D9WG_OP_STRETCH_RECT = 8,        /* M3 */
    D9WG_OP_COLOR_FILL = 9,          /* M3 */
    D9WG_OP_UPDATE_SURFACE = 10,
    /*
     * Guest -> host diagnostics. The only direction this protocol ever carried
     * was commands, so a call the guest DLL refused was invisible everywhere
     * the developer can actually look: the browser console sees a clean stream
     * of valid commands, and the guest's own trace file lives inside a VM whose
     * filesystem is not reachable from the page. That gap repeatedly turned
     * "the picture is wrong" into guesswork, because the one fact that would
     * have ended it -- the app asked for something and was told no -- was
     * written nowhere the developer could read.
     *
     * Deliberately not a general logging channel: only refusals and failures
     * are sent, deduplicated in the guest so a per-frame failure costs one
     * message rather than one per frame.
     */
    D9WG_OP_GUEST_LOG = 11,
    D9WG_OP_READBACK_SURFACE = 12,
    /*
     * The process owning the batch session is going away.  Device destruction
     * is still sent at the normal COM lifetime boundary; this is the fallback
     * that releases anything retained when a process exits abnormally or
     * unloads the proxy with live objects.
     */
    D9WG_OP_SESSION_END = 13,

    D9WG_OP_CREATE_BUFFER = 0x100,
    D9WG_OP_UPDATE_BUFFER = 0x101,
    D9WG_OP_DESTROY_RESOURCE = 0x103,
    D9WG_OP_CREATE_TEXTURE_2D = 0x110,
    D9WG_OP_CREATE_TEXTURE_CUBE = 0x111,     /* M3 */
    D9WG_OP_CREATE_TEXTURE_VOLUME = 0x112,
    D9WG_OP_UPDATE_TEXTURE = 0x113,          /* M1; z = cube face / volume slice */
    D9WG_OP_CREATE_VERTEX_DECLARATION = 0x120,
    D9WG_OP_CREATE_VERTEX_SHADER = 0x121,     /* M2 */
    D9WG_OP_CREATE_PIXEL_SHADER = 0x122,      /* M2 */
    D9WG_OP_CREATE_QUERY = 0x123,
    D9WG_OP_CREATE_STATE_BLOCK = 0x124,       /* unused: state blocks are guest-side */

    D9WG_OP_SET_RENDER_STATE = 0x200,
    D9WG_OP_SET_SAMPLER_STATE = 0x201,       /* M2 */
    D9WG_OP_SET_TEXTURE_STAGE_STATE = 0x202,
    D9WG_OP_SET_TEXTURE = 0x203,
    D9WG_OP_SET_VIEWPORT = 0x204,
    D9WG_OP_SET_SCISSOR_RECT = 0x205,        /* M3 */
    D9WG_OP_SET_TRANSFORM = 0x206,
    D9WG_OP_SET_MATERIAL = 0x207,            /* M1 wire, consumed since M3 */
    D9WG_OP_SET_LIGHT = 0x208,               /* M1 wire, consumed since M3 */
    D9WG_OP_LIGHT_ENABLE = 0x209,            /* M1 wire, consumed since M3 */
    D9WG_OP_SET_STREAM_SOURCE = 0x20A,
    D9WG_OP_SET_STREAM_SOURCE_FREQ = 0x20B,  /* indexed instancing */
    D9WG_OP_SET_INDICES = 0x20C,
    D9WG_OP_SET_VERTEX_DECLARATION = 0x20D,
    D9WG_OP_SET_FVF = 0x20E,                 /* 兼容路径 */
    D9WG_OP_SET_RENDER_TARGET = 0x20F,       /* M3, up to four MRT slots */
    /* Retired v1.0 opcode. Protocol 1.3 executors do not decode it. */
    D9WG_OP_RESERVED_SET_DEPTH_STENCIL_SURFACE_V1_0 = 0x210,
    D9WG_OP_SET_VERTEX_SHADER = 0x211,       /* M2 */
    D9WG_OP_SET_PIXEL_SHADER = 0x212,        /* M2 */
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F = 0x213, /* M2 */
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_I = 0x214, /* M2 */
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_B = 0x215, /* M2 */
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F = 0x216,  /* M2 */
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_I = 0x217,  /* M2 */
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_B = 0x218,  /* M2 */
    D9WG_OP_SET_CLIP_PLANE = 0x219,
    /* M2: the D3D9 hardware cursor. A fullscreen game draws its pointer
     * through these rather than through GDI, so with them unimplemented the
     * pointer is simply invisible -- the guest's GDI cursor never reaches the
     * VGA framebuffer the site composites underneath, and the site hides the
     * browser cursor (`cursor: none`) on the assumption the guest draws it. */
    D9WG_OP_SET_CURSOR_PROPERTIES = 0x21A,
    D9WG_OP_SET_CURSOR_POSITION = 0x21B,
    D9WG_OP_SHOW_CURSOR = 0x21C,
    /* Diagnostic only: reports what the guest's window manager thinks of the
     * device window. The host draws its overlay unconditionally on top, so a
     * game whose window is minimised, hidden or simply not in the foreground
     * still *looks* perfectly rendered while every click goes somewhere else
     * entirely. Nothing about that is visible in the picture, which is why it
     * has to be reported rather than inferred. */
    D9WG_OP_WINDOW_STATE = 0x21D,
    /* Carries the GetSurfaceLevel subresource as well as the texture handle. */
    D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL = 0x21E,

    /* Protocol 1.4. D3D9 palettes are device state applied at sample time, not
     * baked into the texture, so both the table and which table is current have
     * to reach the host: the same P8 texture must change appearance when the
     * app swaps palettes without re-uploading a byte. */
    D9WG_OP_SET_PALETTE = 0x21F,
    D9WG_OP_SET_CURRENT_TEXTURE_PALETTE = 0x220,

    /* IDirect3DBaseTexture9::GenerateMipSubLevels. The *implicit* regeneration
     * D3DUSAGE_AUTOGENMIPMAP asks for needs no opcode: the host sees level 0
     * being written, by upload or by a render pass, and knows more precisely
     * than the guest does when the chain went stale. This carries only the
     * explicit call. */
    D9WG_OP_GENERATE_MIPS = 0x221,

    /* Protocol 1.5. IDirect3DBaseTexture9::SetLOD, the per-texture floor on
     * which mip level may be sampled. It is deliberately not folded into
     * D3DSAMP_MAXMIPLEVEL: that one is sampler state and applies to whatever
     * texture is bound to the stage, while this one travels with the texture
     * and outlives any particular binding. The host takes the more restrictive
     * of the two, which is what D3D9 does. */
    D9WG_OP_SET_TEXTURE_LOD = 0x222,

    /* Protocol 1.5. IDirect3DDevice9::SetGammaRamp.
     *
     * D3D9 applies this at scanout, after everything else, which is why it is
     * carried as its own command rather than folded into any render state: it
     * has to survive being set once at startup and never mentioned again, and
     * it applies to frames drawn long afterwards. The host applies it in the
     * present blit -- the last place a pixel passes through -- as a 256-entry
     * lookup, which is what the hardware does.
     *
     * Real D3D9 honours the ramp only on a fullscreen device, and silently
     * ignores it windowed. That distinction does not survive here: every frame
     * reaches the page through the same blit, so the ramp is always applied,
     * and a windowed title's brightness slider works where on hardware it
     * would not. */
    D9WG_OP_SET_GAMMA_RAMP = 0x223,

    /*
     * Protocol 1.6. IDirect3DDevice9::CreateAdditionalSwapChain and the
     * per-chain Present.
     *
     * An additional swap chain targets a *different* HWND, so the host needs a
     * second drawing surface for it -- the implicit chain's canvas is the one
     * the page composites over the device window and nothing else.
     *
     * The back buffer is deliberately an ordinary render-target texture with an
     * ordinary resource handle, not a second "handle zero" special case. That
     * makes every other path work unchanged: SetRenderTarget, StretchRect,
     * GetRenderTargetData and the whole render-pass builder already know how to
     * treat a texture as a target, and the chain only becomes special in the
     * one step that moves the finished image onto its canvas.
     */
    D9WG_OP_CREATE_SWAP_CHAIN = 0x224,
    D9WG_OP_DESTROY_SWAP_CHAIN = 0x225,
    D9WG_OP_PRESENT_SWAP_CHAIN = 0x226,

    D9WG_OP_DRAW_PRIMITIVE = 0x300,
    D9WG_OP_DRAW_INDEXED_PRIMITIVE = 0x301,
    D9WG_OP_DRAW_PRIMITIVE_UP = 0x302,
    D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP = 0x303,

    D9WG_OP_BEGIN_QUERY = 0x400,
    D9WG_OP_END_QUERY = 0x401,
    D9WG_OP_GET_QUERY_DATA = 0x402,

    /*
     * Protocol 1.7. The DirectDraw group, emitted by ../ddrawproxy/ddraw.dll.
     *
     * DirectDraw surfaces are ordinary D9WG textures -- they are created,
     * updated and destroyed by the 0x1xx opcodes like everything else -- and a
     * Direct3D 7 device renders straight into one of them, which is exactly
     * why DirectDraw does not get a protocol of its own. These five carry the
     * semantics D3D9 has no equivalent for: a blit that tests a colour key, a
     * palette that belongs to a surface rather than to the device, and the
     * display mode an exclusive-fullscreen title asked for.
     *
     * Flipping is deliberately absent. A flip chain is rotated guest-side and
     * the new front buffer is blitted into the swap-chain image before
     * PRESENT, which costs one full-screen copy per frame and saves the host
     * an entire lifecycle concept. Clip lists are likewise resolved guest-side
     * into per-rectangle blits.
     */
    D9WG_OP_DD_BLT = 0x500,
    D9WG_OP_DD_SET_COLOR_KEY = 0x501,
    D9WG_OP_DD_SET_SURFACE_PALETTE = 0x502,
    D9WG_OP_DD_SET_DISPLAY_MODE = 0x503,
    D9WG_OP_DD_UPDATE_OVERLAY = 0x504
};

#define D9WG_RESOURCE_BUFFER_VERTEX      1u
#define D9WG_RESOURCE_BUFFER_INDEX       2u
#define D9WG_RESOURCE_TEXTURE_2D         3u
#define D9WG_RESOURCE_TEXTURE_CUBE       4u
#define D9WG_RESOURCE_TEXTURE_VOLUME     5u
#define D9WG_RESOURCE_VERTEX_DECLARATION 6u
#define D9WG_RESOURCE_VERTEX_SHADER      7u
#define D9WG_RESOURCE_PIXEL_SHADER       8u
#define D9WG_RESOURCE_QUERY              9u
#define D9WG_RESOURCE_STATE_BLOCK        10u

/* Shader handles (once M2 starts allocating them) live in a namespace
 * disjoint from allocate_handle()'s buffer/texture/declaration counter, with
 * bit 0 always set, purely so a host-side resource table keyed by a single
 * flat handle space can never collide a shader handle with a buffer/texture/
 * declaration handle. Unlike D3D8, D3D9's SetVertexShader/SetFVF/
 * SetVertexDeclaration are three distinct COM methods with distinct
 * parameter types, so the guest never needs to disambiguate a shader handle
 * from an FVF token on the wire -- each has its own opcode. */
#define D9WG_SHADER_HANDLE_BASE 0x40000001u

/*
 * Protocol 1.7, D9WGCreateTexture2D.usage. Not a D3DUSAGE bit: D3DUSAGE never
 * defines bit 31, and this is a storage decision rather than anything the app
 * asked for.
 *
 * A P8 texture created with it is kept as an r8uint index texture resolved
 * through its palette at sample and present time, instead of the CPU-expanded
 * RGBA copy the D3D9 path uses. DirectDraw needs the indexed form for
 * correctness, not speed: a 2D title blits P8 into P8 all frame long, and a
 * surface holds indices, so a later palette change has to change the colour of
 * pixels that were blitted earlier. An RGBA copy cannot be re-indexed, so a
 * CPU-expanded destination freezes at whatever palette was current when the
 * sprite landed. Colour keying on such a surface compares indices too.
 */
#define D9WG_USAGE_DDRAW_INDEXED 0x80000000u

#pragma pack(push, 1)
typedef struct D9WGBatchHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t frame_id;
    uint32_t flags;
    uint32_t command_count;
    uint32_t command_bytes;
    uint32_t session_id_low;
    uint32_t session_id_high;
} D9WGBatchHeader; /* 32 bytes */

typedef struct D9WGCommandHeader {
    uint16_t opcode;
    uint16_t flags;
    uint32_t size;
    uint32_t sequence;
    uint32_t reserved;
} D9WGCommandHeader; /* 16 bytes */

typedef struct D9WGHello {
    uint32_t guest_pointer_bits;
    uint32_t feature_bits;
    uint32_t session_id_low;
    uint32_t session_id_high;
} D9WGHello;

typedef struct D9WGSessionEnd {
    uint32_t session_id_low;
    uint32_t session_id_high;
} D9WGSessionEnd;

typedef struct D9WGCreateDevice {
    uint32_t device_handle;
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t backbuffer_format;
    uint32_t windowed;
    uint32_t behavior_flags;
    uint32_t enable_auto_depth_stencil;
    uint32_t auto_depth_stencil_format;
    uint32_t multisample_type;
    uint32_t multisample_quality;
} D9WGCreateDevice;

typedef struct D9WGResetDevice {
    uint32_t old_device_handle;
    uint32_t new_device_handle;
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t backbuffer_format;
    uint32_t windowed;
    uint32_t behavior_flags;
    uint32_t enable_auto_depth_stencil;
    uint32_t auto_depth_stencil_format;
    uint32_t multisample_type;
    uint32_t multisample_quality;
} D9WGResetDevice;

typedef struct D9WGPresent {
    uint32_t device_handle;
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} D9WGPresent;

typedef struct D9WGClear {
    uint32_t device_handle;
    uint32_t clear_flags;
    uint32_t color;
    float depth;
    uint32_t stencil;
    uint32_t rect_count;
} D9WGClear;

typedef struct D9WGDeviceOnly {
    uint32_t device_handle;
    uint32_t reserved;
} D9WGDeviceOnly;

typedef struct D9WGCreateBuffer {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t resource_kind;
    uint32_t byte_count;
    uint32_t usage;
    uint32_t fvf; /* index buffers store D3DFORMAT (INDEX16/INDEX32) here */
    uint32_t pool;
    uint32_t reserved;
} D9WGCreateBuffer;

typedef struct D9WGUpdateBuffer {
    uint32_t resource_handle;
    uint32_t destination_offset;
    uint32_t byte_count;
    uint32_t data_offset;
    uint32_t lock_flags;
    uint32_t reserved;
} D9WGUpdateBuffer;

typedef struct D9WGCreateTexture2D {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t width;
    uint32_t height;
    uint32_t level_count;
    uint32_t format;
    uint32_t usage;
    uint32_t pool;
    uint32_t multisample_type;
    uint32_t multisample_quality;
} D9WGCreateTexture2D;

/* depth/slice_pitch are always 1/0 for a 2D texture; the fields exist now so
 * D9WG_OP_CREATE_TEXTURE_VOLUME's UpdateTexture traffic (M3) does not need a
 * new opcode. */
typedef struct D9WGUpdateTexture {
    uint32_t resource_handle;
    uint32_t level;
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t row_pitch;
    uint32_t slice_pitch;
    uint32_t data_bytes;
    uint32_t data_offset;
} D9WGUpdateTexture;

typedef struct D9WGDestroyResource {
    uint32_t resource_handle;
    uint32_t resource_kind;
} D9WGDestroyResource;

typedef struct D9WGSetRenderState {
    uint32_t device_handle;
    uint32_t state;
    uint32_t value;
    uint32_t reserved;
} D9WGSetRenderState;

typedef struct D9WGSetSamplerState {
    uint32_t device_handle;
    uint32_t sampler;
    uint32_t state;
    uint32_t value;
} D9WGSetSamplerState;

typedef struct D9WGSetTextureStageState {
    uint32_t device_handle;
    uint32_t stage;
    uint32_t state;
    uint32_t value;
} D9WGSetTextureStageState;

typedef struct D9WGSetTexture {
    uint32_t device_handle;
    uint32_t stage;
    uint32_t texture_handle;
    uint32_t reserved;
} D9WGSetTexture;

typedef struct D9WGSetViewport {
    uint32_t device_handle;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float min_z;
    float max_z;
    uint32_t reserved;
} D9WGSetViewport;

typedef struct D9WGSetTransform {
    uint32_t device_handle;
    uint32_t state; /* raw D3DTRANSFORMSTATETYPE value, not remapped */
    float matrix[16];
} D9WGSetTransform;

typedef struct D9WGSetMaterial {
    uint32_t device_handle;
    float diffuse[4];
    float ambient[4];
    float specular[4];
    float emissive[4];
    float power;
} D9WGSetMaterial;

typedef struct D9WGSetLight {
    uint32_t device_handle;
    uint32_t index;
    uint32_t type;
    float diffuse[4];
    float specular[4];
    float ambient[4];
    float position[3];
    float direction[3];
    float range;
    float falloff;
    float attenuation[3];
    float theta;
    float phi;
} D9WGSetLight;

typedef struct D9WGLightEnable {
    uint32_t device_handle;
    uint32_t index;
    uint32_t enable;
    uint32_t reserved;
} D9WGLightEnable;

/* offset_in_bytes is D3D9's SetStreamSource OffsetInBytes: the byte offset
 * of this binding's first vertex within the buffer. Packing several meshes
 * into one large vertex buffer and binding each at its own offset is a very
 * common engine pattern, so this is carried on the wire rather than
 * restricted to zero. */
typedef struct D9WGSetStreamSource {
    uint32_t device_handle;
    uint32_t stream;
    uint32_t buffer_handle;
    uint32_t stride;
    uint32_t offset_in_bytes;
    uint32_t reserved;
} D9WGSetStreamSource;

/* Raw D3D9 SetStreamSourceFreq value. Stream 0 carries
 * D3DSTREAMSOURCE_INDEXEDDATA|instance_count; instance streams carry
 * D3DSTREAMSOURCE_INSTANCEDATA|step_rate. */
typedef struct D9WGSetStreamSourceFreq {
    uint32_t device_handle;
    uint32_t stream;
    uint32_t divider;
} D9WGSetStreamSourceFreq;

/* D3D9's IDirect3DDevice9::SetIndices, unlike D3D8's, carries no base vertex
 * index -- that moved to a per-draw parameter (see D9WGDrawIndexedPrimitive).
 */
typedef struct D9WGSetIndices {
    uint32_t device_handle;
    uint32_t buffer_handle;
} D9WGSetIndices;

/* Per plan section 4.3, the host never decodes raw FVF bits: the guest
 * expands SetFVF(fvf) into the equivalent D3DVERTEXELEMENT9 array (see
 * fvf_to_declaration() in d3d9_proxy.c) and sends it here in the same shape
 * CREATE_VERTEX_DECLARATION uses, so the host has exactly one vertex-layout
 * code path regardless of which legacy/modern API produced it. `fvf` is
 * carried only for host-side logging/diagnostics, never interpreted. */
typedef struct D9WGSetFVF {
    uint32_t device_handle;
    uint32_t fvf;
    uint32_t element_count;
    uint32_t reserved;
    /* element_count 个 D9WGVertexElement 紧随其后 */
} D9WGSetFVF;

typedef struct D9WGSetVertexDeclaration {
    uint32_t device_handle;
    uint32_t declaration_handle;
} D9WGSetVertexDeclaration;

/* D3DVERTEXELEMENT9 has the identical 8-byte shape (Stream/Offset/Type/
 * Method/Usage/UsageIndex as WORD/WORD/BYTE/BYTE/BYTE/BYTE), so the guest
 * copies the app's array into this type without reinterpreting fields. */
typedef struct D9WGVertexElement {
    uint16_t stream;
    uint16_t offset;
    uint8_t  type;
    uint8_t  method;
    uint8_t  usage;
    uint8_t  usage_index;
} D9WGVertexElement; /* 8 bytes */

typedef struct D9WGCreateVertexDeclaration {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t element_count;
    uint32_t reserved;
    /* element_count 个 D9WGVertexElement 紧随其后, 不含 D3DDECL_END() 哨兵 */
} D9WGCreateVertexDeclaration;

/* M2: CREATE_VERTEX_SHADER / CREATE_PIXEL_SHADER. bytecode_hash lets host
 * dedupe compiled WGSL across CreateXShader calls with identical bytecode. */
typedef struct D9WGCreateVertexShader {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t instruction_token_count;
    uint32_t code_offset;
    uint32_t bytecode_hash_low;
    uint32_t bytecode_hash_high;
} D9WGCreateVertexShader;
/* D9WGCreatePixelShader is byte-for-byte identical. */
typedef D9WGCreateVertexShader D9WGCreatePixelShader;

typedef struct D9WGSetShader {
    uint32_t device_handle;
    uint32_t shader_handle; /* 0 = unbind (fixed function) */
} D9WGSetShader;

typedef struct D9WGSetShaderConstantF {
    uint32_t device_handle;
    uint32_t start_register;
    uint32_t vector_count; /* float4 count */
    uint32_t data_offset;  /* vector_count*16 bytes, relative to batch base */
} D9WGSetShaderConstantF;

typedef struct D9WGSetRenderTarget {
    uint32_t device_handle;
    uint32_t target_index;         /* 0..3, MRT (M4) */
    uint32_t color_texture_handle; /* 0 = 解除绑定该槽位 */
    uint32_t color_level;
    /* D3DCUBEMAP_FACES for a cube map face, 0 for a 2D surface. A cube render
     * target is bound one face at a time -- SetRenderTarget takes the surface
     * GetCubeMapSurface(face, level) returned -- so without this the host can
     * only ever address layer 0 and every face of a dynamic environment map
     * lands on top of the first one. */
    uint32_t color_face;
} D9WGSetRenderTarget;

/* 256 D3DCOLOR (A8R8G8B8) entries follow at data_offset. Always the full
 * table: D3D9's SetPaletteEntries replaces all 256 at once. */
typedef struct D9WGSetPalette {
    uint32_t device_handle;
    uint32_t palette_index;
    uint32_t entry_count;
    uint32_t data_offset;
} D9WGSetPalette;

typedef struct D9WGSetCurrentTexturePalette {
    uint32_t device_handle;
    uint32_t palette_index;
} D9WGSetCurrentTexturePalette;

typedef struct D9WGGenerateMips {
    uint32_t device_handle;
    uint32_t resource_handle;
} D9WGGenerateMips;

typedef struct D9WGSetTextureLOD {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t lod;
    uint32_t reserved;
} D9WGSetTextureLOD;

typedef struct D9WGCreateSwapChain {
    uint32_t device_handle;
    uint32_t swap_chain_handle;
    /* The render-target texture the chain draws into, created through the
     * ordinary CREATE_TEXTURE_2D path before this arrives. */
    uint32_t back_buffer_handle;
    uint32_t hwnd;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} D9WGCreateSwapChain;

typedef struct D9WGDestroySwapChain {
    uint32_t device_handle;
    uint32_t swap_chain_handle;
} D9WGDestroySwapChain;

/* The window rect is re-sent on every present for the same reason the implicit
 * chain's is: the guest has no window-move subclassing, so Present is the live
 * source of truth for where the canvas belongs. */
typedef struct D9WGPresentSwapChain {
    uint32_t device_handle;
    uint32_t swap_chain_handle;
    uint32_t hwnd;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
} D9WGPresentSwapChain;

/* 256 entries of red, then 256 green, then 256 blue -- the memory order of
 * D3DGAMMARAMP itself, so the guest copies it in one go. */
typedef struct D9WGSetGammaRamp {
    uint32_t device_handle;
    uint32_t swap_chain;
    uint32_t flags;
    uint32_t reserved;
    uint16_t ramp[768];
} D9WGSetGammaRamp;

typedef struct D9WGSetScissorRect {
    uint32_t device_handle;
    int32_t  left;
    int32_t  top;
    int32_t  right;
    int32_t  bottom;
} D9WGSetScissorRect;

/* The cursor bitmap, always expanded to A8R8G8B8 by the guest (D3D9 requires
 * that format for a cursor surface anyway), followed by width*height*4 bytes
 * at data_offset. */
typedef struct D9WGSetCursorProperties {
    uint32_t device_handle;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t width;
    uint32_t height;
    uint32_t data_bytes;
    uint32_t data_offset;
    uint32_t reserved;
} D9WGSetCursorProperties;

/* Client-relative pixel coordinates, i.e. already in back-buffer space. D3D9's
 * hardware cursor tracks the OS pointer on its own once ShowCursor(TRUE) is
 * set -- SetCursorPosition only warps it -- so the guest re-sends the live
 * position on every Present rather than only when the app calls that method. */
typedef struct D9WGSetCursorPosition {
    uint32_t device_handle;
    int32_t x;
    int32_t y;
    uint32_t flags;
} D9WGSetCursorPosition;

typedef struct D9WGShowCursor {
    uint32_t device_handle;
    uint32_t show;
} D9WGShowCursor;

#define D9WG_WINDOW_IS_WINDOW    (1u << 0)
#define D9WG_WINDOW_VISIBLE      (1u << 1)
#define D9WG_WINDOW_ICONIC       (1u << 2)
#define D9WG_WINDOW_FOREGROUND   (1u << 3)
#define D9WG_WINDOW_FULLSCREEN   (1u << 4)
/*
 * The guest has nothing to present for this device -- a DirectDraw title that
 * released its primary surface, say. The host takes the overlay down for it
 * exactly as it does for a hidden window, but the window itself may be up and
 * taking input normally, so the reports about visibility and foreground do not
 * apply and are not made. Reported alongside the real window state, never
 * instead of it.
 */
#define D9WG_WINDOW_NO_SURFACE   (1u << 5)

typedef struct D9WGWindowState {
    uint32_t device_handle;
    uint32_t hwnd;
    uint32_t foreground_hwnd;
    uint32_t flags;
    int32_t  window_x;
    int32_t  window_y;
    uint32_t window_width;
    uint32_t window_height;
    uint32_t client_width;
    uint32_t client_height;
} D9WGWindowState;

typedef struct D9WGCreateQuery {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t query_type;
    uint32_t response_offset; /* from the start of the DMA response region */
} D9WGCreateQuery;

/* Written host->guest.  Status is deliberately last: emulator.write_memory()
 * copies bytes in increasing order, so observing OK also observes the value. */
typedef struct D9WGQueryResponse {
    uint32_t request_id;
    uint32_t value_low;
    uint32_t value_high;
    volatile uint32_t status;
} D9WGQueryResponse;

typedef struct D9WGQueryIssue {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t response_offset;
    uint32_t request_id;
} D9WGQueryIssue;

typedef struct D9WGReadbackSurface {
    uint32_t device_handle;
    uint32_t texture_handle; /* zero names the current/back-buffer target */
    uint32_t level;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t first_row;
    uint32_t row_count;
    uint32_t destination_pitch;
    uint32_t destination_bytes;
    uint32_t response_offset;
    uint32_t request_id;
    /* Protocol 1.7 extension. Zero for 2D; 0..5 selects a cube array layer.
     * Appended so older 48-byte senders continue to read face zero. */
    uint32_t face;
} D9WGReadbackSurface;

typedef struct D9WGReadbackResponse {
    uint32_t request_id;
    uint32_t byte_count;
    uint32_t reserved;
    volatile uint32_t status;
    /* byte_count bytes follow */
} D9WGReadbackResponse;

/* M3. A cube texture is six square faces at each mip level; the host maps it
 * onto a WebGPU 2D texture with six array layers, which is what a
 * texture_cube<f32> view is built over. UpdateTexture reaches a face through
 * D9WGUpdateTexture.z (0..5, D3DCUBEMAP_FACE_*) -- the same field a volume
 * texture uses for its slice, because in both cases it selects the layer/depth
 * the upload lands in and neither needs a second opcode. */
typedef struct D9WGCreateTextureCube {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t edge_length;
    uint32_t level_count;
    uint32_t format;
    uint32_t usage;
    uint32_t pool;
    uint32_t reserved;
} D9WGCreateTextureCube;

typedef struct D9WGCreateTextureVolume {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t level_count;
    uint32_t format;
    uint32_t usage;
    uint32_t pool;
    uint32_t reserved;
} D9WGCreateTextureVolume;

/* M3. WebGPU/WGSL has no clip-distance facility, so a user clip plane becomes a
 * per-plane distance the vertex stage writes and the fragment stage discards
 * on (plan 9.11). D3D9 defines the plane in world space for fixed-function
 * vertex processing and in clip space when a vertex shader is bound; the guest
 * sends the app's coefficients unchanged and the host applies the space rule,
 * because only the host knows which vertex stage a given draw resolved to. */
typedef struct D9WGSetClipPlane {
    uint32_t device_handle;
    uint32_t index; /* 0..5 */
    float plane[4];
} D9WGSetClipPlane;

/* M4 (implemented ahead of schedule -- a 2005-era D3D9 game renders most of its
 * frame into textures). The bound surface is always identified by the host
 * texture it is a level of, so a standalone CreateRenderTarget surface is sent
 * as a CREATE_TEXTURE_2D with D3DUSAGE_RENDERTARGET rather than needing its own
 * resource kind.
 *
 * depth_texture_handle needs three distinguishable values, not two: a real
 * surface, "no depth at all" (SetDepthStencilSurface(NULL), which D3D9 permits
 * and which disables depth testing for the following draws), and "the device's
 * own implicit auto depth-stencil". The third exists because the standard
 * render-to-texture sequence is Get the current depth surface, bind a different
 * one, then put the original back -- and the original has no texture handle of
 * its own, so without a sentinel it would be indistinguishable from unbinding. */
#define D9WG_AUTO_DEPTH_STENCIL_HANDLE 0xFFFFFFFFu
typedef struct D9WGSetDepthStencilSurface {
    uint32_t device_handle;
    uint32_t depth_texture_handle;
    uint32_t width;
    uint32_t height;
} D9WGSetDepthStencilSurface;

typedef struct D9WGSetDepthStencilSurfaceLevel {
    uint32_t device_handle;
    uint32_t depth_texture_handle;
    /* The mip selected by IDirect3DTexture9::GetSurfaceLevel.  Unlike the
     * implicit auto depth-stencil, an explicit depth texture can expose any
     * of its levels as a surface, so the subresource is part of the binding. */
    uint32_t depth_level;
    uint32_t width;
    uint32_t height;
} D9WGSetDepthStencilSurfaceLevel;

/* StretchRect between two surfaces the host owns. A zero *_texture_handle names
 * the current render target (D3D9 apps routinely stretch the back buffer into a
 * texture and back), and level selects the mip. filter_point is
 * D3DTEXF_POINT vs anything else, which is the only distinction WebGPU's blit
 * path can honour. */
typedef struct D9WGStretchRect {
    uint32_t device_handle;
    uint32_t source_texture_handle;
    uint32_t source_level;
    int32_t  source_left;
    int32_t  source_top;
    int32_t  source_right;
    int32_t  source_bottom;
    uint32_t destination_texture_handle;
    uint32_t destination_level;
    int32_t  destination_left;
    int32_t  destination_top;
    int32_t  destination_right;
    int32_t  destination_bottom;
    uint32_t filter_point;
    /* Cube faces, 0 for 2D surfaces. Present for the same reason as
     * D9WGSetRenderTarget::color_face: without them a blit that names one face
     * of an environment map silently lands on face 0, which is a wrong-pixels
     * bug rather than an error anything reports. */
    uint32_t source_face;
    uint32_t destination_face;
} D9WGStretchRect;

typedef struct D9WGColorFill {
    uint32_t device_handle;
    uint32_t texture_handle;
    uint32_t level;
    uint32_t color;
    int32_t  left;
    int32_t  top;
    int32_t  right;
    int32_t  bottom;
    uint32_t face; /* cube face, 0 for a 2D surface */
    uint32_t reserved;
} D9WGColorFill;

/*
 * D9WG_OP_GUEST_LOG. `text_bytes` ASCII characters follow the header, not
 * NUL-terminated. Severity is advisory: the host picks console.warn or
 * console.error from it, nothing is gated on it.
 */
#define D9WG_LOG_SEVERITY_INFO    0u  /* identification, not a problem */
#define D9WG_LOG_SEVERITY_REFUSED 1u  /* a call the guest DLL turned down */
#define D9WG_LOG_SEVERITY_FAILED  2u  /* a call that should have worked but did not */
#define D9WG_LOG_MAX_TEXT 240u

typedef struct D9WGGuestLog {
    uint32_t severity;
    uint32_t text_bytes;
    /* text_bytes of ASCII follow immediately. */
} D9WGGuestLog;

typedef struct D9WGDrawPrimitive {
    uint32_t device_handle;
    uint32_t primitive_type;
    uint32_t start_vertex;
    uint32_t primitive_count;
} D9WGDrawPrimitive;

/* base_vertex_index is a per-draw parameter in D3D9 (see D9WGSetIndices). */
typedef struct D9WGDrawIndexedPrimitive {
    uint32_t device_handle;
    uint32_t primitive_type;
    int32_t  base_vertex_index;
    uint32_t min_vertex_index;
    uint32_t vertex_count;
    uint32_t start_index;
    uint32_t primitive_count;
} D9WGDrawIndexedPrimitive;

typedef struct D9WGDrawPrimitiveUP {
    uint32_t device_handle;
    uint32_t primitive_type;
    uint32_t primitive_count;
    uint32_t stride;
    uint32_t vertex_count;
    uint32_t vertex_bytes;
    uint32_t vertex_data_offset;
    uint32_t reserved;
} D9WGDrawPrimitiveUP;

typedef struct D9WGDrawIndexedPrimitiveUP {
    uint32_t device_handle;
    uint32_t primitive_type;
    uint32_t min_vertex_index;
    uint32_t vertex_count;
    uint32_t primitive_count;
    uint32_t index_format;
    uint32_t stride;
    uint32_t index_count;
    uint32_t index_bytes;
    uint32_t vertex_bytes;
    uint32_t index_data_offset;
    uint32_t vertex_data_offset;
} D9WGDrawIndexedPrimitiveUP;

/* ------------------------------------------------------------------ *
 * Protocol 1.7: the DirectDraw group
 * ------------------------------------------------------------------ */

/*
 * Blt/BltFast/Load, and the flip-chain rotation the guest turns into a blit
 * into the swap-chain image. Handle 0 is the swap-chain image on either side.
 *
 * A blit with no colour key, no mirror, matching formats and matching extents
 * is a straight texture-to-texture copy; anything else is a draw through the
 * blit pipeline cache, keyed by (source format, destination format, flags).
 */
typedef struct D9WGDDBlt {
    uint32_t device_handle;
    uint32_t source_handle;
    uint32_t source_level;
    uint32_t source_face;
    int32_t source_rect[4]; /* left, top, right, bottom */
    uint32_t destination_handle;
    uint32_t destination_level;
    uint32_t destination_face;
    uint32_t flags;
    int32_t destination_rect[4];
    uint32_t fill_color; /* COLOR_FILL: the destination format's own value */
    float fill_depth;
    uint32_t fill_stencil;
    uint32_t reserved;
} D9WGDDBlt; /* 80 bytes */

#define D9WG_DDBLT_KEY_SOURCE (1u << 0)
#define D9WG_DDBLT_KEY_DESTINATION (1u << 1)
#define D9WG_DDBLT_MIRROR_X (1u << 2)
#define D9WG_DDBLT_MIRROR_Y (1u << 3)
#define D9WG_DDBLT_COLOR_FILL (1u << 4)
#define D9WG_DDBLT_DEPTH_FILL (1u << 5)
#define D9WG_DDBLT_FILTER_LINEAR (1u << 6)

/*
 * A colour key is surface state, not a blit parameter: DDBLT_KEYSRC means
 * "use the key attached to the source surface". DDBLT_KEYSRCOVERRIDE, which
 * carries its own key, is resolved guest-side into a temporary key set around
 * the blit rather than two more fields here.
 *
 * color_low/color_high are already in the comparison domain the host uses:
 * 8-bit-per-channel values expanded from the surface format by truncating a
 * value*255/max scale, or the raw index for a palettised surface. The guest
 * and the host
 * must expand by the same rule or sprite edges keep a rim of pixels that
 * should have been transparent -- see ../ddrawproxy/ddraw_protocol.h.
 */
typedef struct D9WGDDSetColorKey {
    uint32_t surface_handle;
    uint32_t key_kind; /* D9WG_DDCKEY_* */
    uint32_t color_low;
    uint32_t color_high;
    uint32_t present; /* 0 clears the key */
    uint32_t reserved;
} D9WGDDSetColorKey; /* 24 bytes */

#define D9WG_DDCKEY_SOURCE_BLT 0u
#define D9WG_DDCKEY_DESTINATION_BLT 1u
#define D9WG_DDCKEY_SOURCE_OVERLAY 2u
#define D9WG_DDCKEY_DESTINATION_OVERLAY 3u

/*
 * Binds a surface to one of the palette slots D9WG_OP_SET_PALETTE fills. D3D9
 * palettes are device state and a texture samples through whichever one is
 * current; DirectDraw attaches a palette to each surface, and two P8 surfaces
 * with different palettes are routine. A surface with no binding falls back to
 * the device's current palette.
 */
typedef struct D9WGDDSetSurfacePalette {
    uint32_t surface_handle;
    uint32_t palette_index;
    uint32_t flags;
    uint32_t reserved;
} D9WGDDSetSurfacePalette; /* 16 bytes */

/*
 * SetDisplayMode plus the cooperative level it was set under.
 *
 * guest_mode_changed reports whether ChangeDisplaySettings actually moved the
 * guest desktop to that mode. Only the guest knows, and the answer decides
 * whether the overlay covers the whole emulated screen or just the window
 * rectangle -- it cannot be inferred from the rendered image.
 */
typedef struct D9WGDDSetDisplayMode {
    uint32_t device_handle;
    uint32_t width;
    uint32_t height;
    uint32_t bits_per_pixel;
    uint32_t refresh_rate;
    uint32_t cooperative_flags; /* D9WG_DDSCL_* */
    uint32_t guest_mode_changed;
    uint32_t reserved;
} D9WGDDSetDisplayMode; /* 32 bytes */

#define D9WG_DDSCL_NORMAL (1u << 0)
#define D9WG_DDSCL_EXCLUSIVE (1u << 1)
#define D9WG_DDSCL_FULLSCREEN (1u << 2)

/*
 * UpdateOverlay. Approximated as a present-time composite: the overlay surface
 * is drawn over the swap-chain image in z_order, honouring its colour key.
 * Real hardware overlays are composited at scanout, independent of the flip
 * chain and of GDI, and that part does not survive here.
 */
typedef struct D9WGDDUpdateOverlay {
    uint32_t surface_handle;
    uint32_t overlay_id; /* COM-surface identity; aliases share only pixels */
    int32_t source_rect[4];
    int32_t destination_rect[4];
    uint32_t flags; /* D9WG_DDOVER_* */
    uint32_t z_order;
    uint32_t destination_handle; /* key state lives on the primary surface */
} D9WGDDUpdateOverlay; /* 52 bytes */

#define D9WG_DDOVER_SHOW (1u << 0)
#define D9WG_DDOVER_HIDE (1u << 1)
#define D9WG_DDOVER_KEY_SOURCE (1u << 2)
#define D9WG_DDOVER_KEY_DESTINATION (1u << 3)
#define D9WG_DDOVER_MIRROR_X (1u << 4)
#define D9WG_DDOVER_MIRROR_Y (1u << 5)
#define D9WG_DDOVER_KEY_SOURCE_OVERRIDE (1u << 6)
#define D9WG_DDOVER_KEY_DESTINATION_OVERRIDE (1u << 7)

#pragma pack(pop)

#define D9WG_ALIGN8(value) (((uint32_t)(value) + 7u) & ~7u)

typedef char D9WGAssertBatchHeaderSize[
        sizeof(D9WGBatchHeader) == 32 ? 1 : -1];
typedef char D9WGAssertCommandHeaderSize[
        sizeof(D9WGCommandHeader) == 16 ? 1 : -1];
typedef char D9WGAssertHelloSize[
        sizeof(D9WGHello) == 16 ? 1 : -1];
typedef char D9WGAssertSessionEndSize[
        sizeof(D9WGSessionEnd) == 8 ? 1 : -1];
typedef char D9WGAssertCreateDeviceSize[
        sizeof(D9WGCreateDevice) == 52 ? 1 : -1];
typedef char D9WGAssertResetDeviceSize[
        sizeof(D9WGResetDevice) == 56 ? 1 : -1];
typedef char D9WGAssertPresentSize[
        sizeof(D9WGPresent) == 24 ? 1 : -1];
typedef char D9WGAssertCreateBufferSize[
        sizeof(D9WGCreateBuffer) == 32 ? 1 : -1];
typedef char D9WGAssertUpdateBufferSize[
        sizeof(D9WGUpdateBuffer) == 24 ? 1 : -1];
typedef char D9WGAssertCreateTexture2DSize[
        sizeof(D9WGCreateTexture2D) == 40 ? 1 : -1];
typedef char D9WGAssertUpdateTextureSize[
        sizeof(D9WGUpdateTexture) == 48 ? 1 : -1];
typedef char D9WGAssertSetTextureSize[
        sizeof(D9WGSetTexture) == 16 ? 1 : -1];
typedef char D9WGAssertSetViewportSize[
        sizeof(D9WGSetViewport) == 32 ? 1 : -1];
typedef char D9WGAssertSetTransformSize[
        sizeof(D9WGSetTransform) == 72 ? 1 : -1];
typedef char D9WGAssertSetMaterialSize[
        sizeof(D9WGSetMaterial) == 72 ? 1 : -1];
typedef char D9WGAssertSetLightSize[
        sizeof(D9WGSetLight) == 112 ? 1 : -1];
typedef char D9WGAssertLightEnableSize[
        sizeof(D9WGLightEnable) == 16 ? 1 : -1];
typedef char D9WGAssertSetStreamSourceSize[
        sizeof(D9WGSetStreamSource) == 24 ? 1 : -1];
typedef char D9WGAssertSetStreamSourceFreqSize[
        sizeof(D9WGSetStreamSourceFreq) == 12 ? 1 : -1];
typedef char D9WGAssertSetIndicesSize[
        sizeof(D9WGSetIndices) == 8 ? 1 : -1];
typedef char D9WGAssertSetFVFSize[
        sizeof(D9WGSetFVF) == 16 ? 1 : -1];
typedef char D9WGAssertVertexElementSize[
        sizeof(D9WGVertexElement) == 8 ? 1 : -1];
typedef char D9WGAssertCreateVertexDeclarationSize[
        sizeof(D9WGCreateVertexDeclaration) == 16 ? 1 : -1];
typedef char D9WGAssertCreateVertexShaderSize[
        sizeof(D9WGCreateVertexShader) == 24 ? 1 : -1];
typedef char D9WGAssertSetShaderSize[
        sizeof(D9WGSetShader) == 8 ? 1 : -1];
typedef char D9WGAssertSetShaderConstantFSize[
        sizeof(D9WGSetShaderConstantF) == 16 ? 1 : -1];
typedef char D9WGAssertDrawIndexedPrimitiveSize[
        sizeof(D9WGDrawIndexedPrimitive) == 28 ? 1 : -1];
typedef char D9WGAssertDrawPrimitiveUPSize[
        sizeof(D9WGDrawPrimitiveUP) == 32 ? 1 : -1];
typedef char D9WGAssertDrawIndexedPrimitiveUPSize[
        sizeof(D9WGDrawIndexedPrimitiveUP) == 48 ? 1 : -1];
typedef char D9WGAssertCreateTextureCubeSize[
        sizeof(D9WGCreateTextureCube) == 32 ? 1 : -1];
typedef char D9WGAssertCreateTextureVolumeSize[
        sizeof(D9WGCreateTextureVolume) == 40 ? 1 : -1];
typedef char D9WGAssertSetClipPlaneSize[
        sizeof(D9WGSetClipPlane) == 24 ? 1 : -1];
typedef char D9WGAssertSetRenderTargetSize[
        sizeof(D9WGSetRenderTarget) == 20 ? 1 : -1];
typedef char D9WGAssertSetDepthStencilSurfaceSize[
        sizeof(D9WGSetDepthStencilSurface) == 16 ? 1 : -1];
typedef char D9WGAssertSetDepthStencilSurfaceLevelSize[
        sizeof(D9WGSetDepthStencilSurfaceLevel) == 20 ? 1 : -1];
typedef char D9WGAssertSetPaletteSize[
        sizeof(D9WGSetPalette) == 16 ? 1 : -1];
typedef char D9WGAssertSetCurrentTexturePaletteSize[
        sizeof(D9WGSetCurrentTexturePalette) == 8 ? 1 : -1];
typedef char D9WGAssertGenerateMipsSize[
        sizeof(D9WGGenerateMips) == 8 ? 1 : -1];
typedef char D9WGAssertSetTextureLODSize[
        sizeof(D9WGSetTextureLOD) == 16 ? 1 : -1];
typedef char D9WGAssertSetGammaRampSize[
        sizeof(D9WGSetGammaRamp) == 1552 ? 1 : -1];
typedef char D9WGAssertCreateSwapChainSize[
        sizeof(D9WGCreateSwapChain) == 32 ? 1 : -1];
typedef char D9WGAssertDestroySwapChainSize[
        sizeof(D9WGDestroySwapChain) == 8 ? 1 : -1];
typedef char D9WGAssertPresentSwapChainSize[
        sizeof(D9WGPresentSwapChain) == 32 ? 1 : -1];
typedef char D9WGAssertSetScissorRectSize[
        sizeof(D9WGSetScissorRect) == 20 ? 1 : -1];
typedef char D9WGAssertStretchRectSize[
        sizeof(D9WGStretchRect) == 64 ? 1 : -1];
typedef char D9WGAssertColorFillSize[
        sizeof(D9WGColorFill) == 40 ? 1 : -1];
typedef char D9WGAssertCreateQuerySize[
        sizeof(D9WGCreateQuery) == 16 ? 1 : -1];
typedef char D9WGAssertQueryResponseSize[
        sizeof(D9WGQueryResponse) == 16 ? 1 : -1];
typedef char D9WGAssertQueryIssueSize[
        sizeof(D9WGQueryIssue) == 16 ? 1 : -1];
typedef char D9WGAssertReadbackSurfaceSize[
        sizeof(D9WGReadbackSurface) == 52 ? 1 : -1];
typedef char D9WGAssertReadbackResponseSize[
        sizeof(D9WGReadbackResponse) == 16 ? 1 : -1];
typedef char D9WGAssertDDBltSize[
        sizeof(D9WGDDBlt) == 80 ? 1 : -1];
typedef char D9WGAssertDDSetColorKeySize[
        sizeof(D9WGDDSetColorKey) == 24 ? 1 : -1];
typedef char D9WGAssertDDSetSurfacePaletteSize[
        sizeof(D9WGDDSetSurfacePalette) == 16 ? 1 : -1];
typedef char D9WGAssertDDSetDisplayModeSize[
        sizeof(D9WGDDSetDisplayMode) == 32 ? 1 : -1];
typedef char D9WGAssertDDUpdateOverlaySize[
        sizeof(D9WGDDUpdateOverlay) == 52 ? 1 : -1];

#endif
