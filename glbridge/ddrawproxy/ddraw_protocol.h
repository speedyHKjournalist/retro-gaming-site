#ifndef DDRAW_PROTOCOL_H
#define DDRAW_PROTOCOL_H

/*
 * The DirectDraw / Direct3D 1-7 guest frontend does not define a wire protocol
 * of its own.
 *
 * Direct3D 7 is a cleaner semantic subset of Direct3D 9 than Direct3D 8 was.
 * Diffing d3dtypes.h against d3d9types.h: 53 render states share a name and
 * every one of them shares its number; the 19 shared texture-stage states are
 * numerically identical and the 10 legacy-only ones are exactly the sampler
 * states D3D9 moved to SetSamplerState; D3DTOP_*, D3DTA_* and D3DPT_* are
 * identical without exception; D3DLIGHT7, D3DMATERIAL7 and D3DVIEWPORT7 are
 * byte-for-byte their D3D9 counterparts. And D3D7 has no programmable shaders
 * at all, so the hardest part of the D3D9 path has no counterpart here.
 *
 * So this frontend is a translation layer, the same architecture ../d3d8proxy
 * uses over the same backend, and this header is that layer: it enumerates the
 * places where the legacy APIs genuinely differ, and everything else passes
 * through untouched.
 *
 * Consequences that are easy to get wrong, so stated once here:
 *
 *  - Batches ride V86GL_CTRL_D3D9_BATCH (0xFFE1) and are decoded by
 *    ../d3d9-webgpu/d3d9_executor.js plus its ddraw_ops.js mixin.
 *  - A DirectDraw surface is an ordinary D9WG texture. The attached-surface
 *    graph (flip chains, mip chains, Z buffers) is guest-side bookkeeping; the
 *    host only ever sees flat handles.
 *  - Flipping and clip lists never reach the host: a flip is a rotation plus a
 *    blit into the swap-chain image, and a clip list is resolved into
 *    per-rectangle blits.
 *  - A guest process loads exactly one of ddraw.dll, d3d8.dll, d3d9.dll or
 *    opengl32.dll.
 *
 * This header is deliberately free of ddraw.h/d3d.h dependencies: it is also
 * parsed by ../tests/ddraw_protocol_consistency_test.js, and every value in it
 * is a number the wire carries rather than a type the SDK declares.
 */

#include "../d3d9proxy/d3d9_protocol.h"
/*
 * Reused rather than restated: d3d8_stage_state_to_sampler_state() covers 9 of
 * D3D7's 10 sampler-class texture stage states (D3D8 inherited that numbering
 * from D3D7 unchanged), D3D8_ZBIAS_TO_DEPTHBIAS_STEP is the same conversion
 * with the same constant, and the D3D9DECLUSAGE_ and D3D9DECLTYPE_ names
 * are needed to build the vertex elements an FVF expands into. The header is pure
 * constants and static inlines, so including it costs nothing at runtime.
 */
#include "../d3d8proxy/d3d8_protocol.h"

/* ------------------------------------------------------------------ *
 * D3DFORMAT values the wire carries
 * ------------------------------------------------------------------ *
 *
 * Spelled out because this DLL includes d3d.h, and d3d.h and d3d9.h cannot
 * share a translation unit -- they declare the same type names (D3DLIGHTTYPE,
 * D3DPRIMITIVETYPE, D3DVIEWPORT, ...) with different contents.
 */
#define DDWG_FMT_UNKNOWN 0u
#define DDWG_FMT_R8G8B8 20u
#define DDWG_FMT_A8R8G8B8 21u
#define DDWG_FMT_X8R8G8B8 22u
#define DDWG_FMT_R5G6B5 23u
#define DDWG_FMT_X1R5G5B5 24u
#define DDWG_FMT_A1R5G5B5 25u
#define DDWG_FMT_A4R4G4B4 26u
#define DDWG_FMT_R3G3B2 27u
#define DDWG_FMT_A8 28u
#define DDWG_FMT_A8R3G3B2 29u
#define DDWG_FMT_X4R4G4B4 30u
#define DDWG_FMT_A8P8 40u
#define DDWG_FMT_P8 41u
#define DDWG_FMT_L8 50u
#define DDWG_FMT_A8L8 51u
#define DDWG_FMT_A4L4 52u
#define DDWG_FMT_V8U8 60u
#define DDWG_FMT_L6V5U5 61u
#define DDWG_FMT_X8L8V8U8 62u
#define DDWG_FMT_D16_LOCKABLE 70u
#define DDWG_FMT_D32 71u
#define DDWG_FMT_D15S1 73u
#define DDWG_FMT_D24S8 75u
#define DDWG_FMT_D24X8 77u
#define DDWG_FMT_D24X4S4 79u
#define DDWG_FMT_D16 80u
#define DDWG_FMT_DXT1 0x31545844u
#define DDWG_FMT_DXT2 0x32545844u
#define DDWG_FMT_DXT3 0x33545844u
#define DDWG_FMT_DXT4 0x34545844u
#define DDWG_FMT_DXT5 0x35545844u
#define DDWG_FMT_UYVY 0x59565955u
#define DDWG_FMT_YUY2 0x32595559u

/* D3DPOOL, for CREATE_TEXTURE_2D/CREATE_BUFFER. */
#define DDWG_POOL_DEFAULT 0u
#define DDWG_POOL_MANAGED 1u
#define DDWG_POOL_SYSTEMMEM 2u

/* D3DUSAGE bits this frontend emits. */
#define DDWG_USAGE_RENDERTARGET 0x00000001u
#define DDWG_USAGE_DEPTHSTENCIL 0x00000002u

/* ------------------------------------------------------------------ *
 * Pixel formats: DDPIXELFORMAT bitmasks -> D3DFORMAT
 * ------------------------------------------------------------------ *
 *
 * DirectDraw describes a format by channel bitmasks rather than by an
 * enumeration, so the same 16-bit surface can arrive as 565, 1555 or 555
 * depending on three DWORDs. Named locally (DDRAW_PF_*) so this header stays
 * independent of ddraw.h; the caller passes ddraw.h's own values in.
 */
#define DDRAW_PF_ALPHAPIXELS 0x00000001u
#define DDRAW_PF_ALPHA 0x00000002u
#define DDRAW_PF_FOURCC 0x00000004u
#define DDRAW_PF_PALETTEINDEXED4 0x00000008u
#define DDRAW_PF_PALETTEINDEXED8 0x00000020u
#define DDRAW_PF_RGB 0x00000040u
#define DDRAW_PF_ZBUFFER 0x00000400u
#define DDRAW_PF_PALETTEINDEXED1 0x00000800u
#define DDRAW_PF_PALETTEINDEXED2 0x00001000u
#define DDRAW_PF_ZPIXELS 0x00002000u
#define DDRAW_PF_STENCILBUFFER 0x00004000u
#define DDRAW_PF_LUMINANCE 0x00020000u
#define DDRAW_PF_BUMPLUMINANCE 0x00040000u
#define DDRAW_PF_BUMPDUDV 0x00080000u

typedef struct DDrawPixelFormatDesc {
    unsigned flags;
    unsigned fourcc;
    unsigned bit_count;
    unsigned red_mask;
    unsigned green_mask;
    unsigned blue_mask;
    unsigned alpha_mask;
    unsigned z_bit_depth;
    unsigned stencil_bit_depth;
} DDrawPixelFormatDesc;

/* Bit population count, and the index of the lowest set bit. A channel mask is
 * always contiguous in every format DirectDraw can describe, so the pair is
 * enough to describe the channel completely. */
static __inline unsigned ddraw_mask_bits(unsigned mask)
{
    unsigned bits = 0u;
    while (mask) { bits += mask & 1u; mask >>= 1; }
    return bits;
}

static __inline unsigned ddraw_mask_shift(unsigned mask)
{
    unsigned shift = 0u;
    if (!mask) return 0u;
    while (!(mask & 1u)) { mask >>= 1; ++shift; }
    return shift;
}

/*
 * Returns DDWG_FMT_UNKNOWN for anything outside the supported matrix, which
 * the caller must turn into an explicit refusal plus a GUEST_LOG rather than a
 * silent substitution: a surface created in a format we do not really support
 * fails later, somewhere else, as a wrong picture.
 */
static __inline unsigned ddraw_pixel_format_to_d3dformat(
        const DDrawPixelFormatDesc *pf)
{
    unsigned r, g, b, a;

    if (pf->flags & DDRAW_PF_FOURCC) {
        switch (pf->fourcc) {
        case DDWG_FMT_DXT1: case DDWG_FMT_DXT2: case DDWG_FMT_DXT3:
        case DDWG_FMT_DXT4: case DDWG_FMT_DXT5:
        case DDWG_FMT_UYVY: case DDWG_FMT_YUY2:
            return pf->fourcc;
        default:
            return DDWG_FMT_UNKNOWN;
        }
    }

    if (pf->flags & (DDRAW_PF_ZBUFFER | DDRAW_PF_ZPIXELS)) {
        unsigned stencil = (pf->flags & DDRAW_PF_STENCILBUFFER)
            ? pf->stencil_bit_depth : 0u;
        if (pf->z_bit_depth == 16u && !stencil) return DDWG_FMT_D16;
        if (pf->z_bit_depth == 32u && !stencil) return DDWG_FMT_D32;
        if (pf->z_bit_depth == 24u && !stencil) return DDWG_FMT_D24X8;
        if (pf->z_bit_depth == 24u && stencil == 8u) return DDWG_FMT_D24S8;
        if (pf->z_bit_depth == 24u && stencil == 4u) return DDWG_FMT_D24X4S4;
        if (pf->z_bit_depth == 15u && stencil == 1u) return DDWG_FMT_D15S1;
        if (pf->z_bit_depth == 32u && stencil == 8u) return DDWG_FMT_D24S8;
        return DDWG_FMT_UNKNOWN;
    }

    if (pf->flags & DDRAW_PF_PALETTEINDEXED8)
        return (pf->flags & DDRAW_PF_ALPHAPIXELS) && pf->bit_count == 16u
            ? DDWG_FMT_A8P8 : DDWG_FMT_P8;
    /* 1/2/4-bit palettised surfaces have no GPU equivalent and no user in the
     * target library; refused rather than expanded behind the app's back. */
    if (pf->flags & (DDRAW_PF_PALETTEINDEXED1 | DDRAW_PF_PALETTEINDEXED2 |
            DDRAW_PF_PALETTEINDEXED4))
        return DDWG_FMT_UNKNOWN;

    if (pf->flags & DDRAW_PF_LUMINANCE) {
        if (pf->bit_count == 8u && !(pf->flags & DDRAW_PF_ALPHAPIXELS))
            return DDWG_FMT_L8;
        if (pf->bit_count == 16u) return DDWG_FMT_A8L8;
        if (pf->bit_count == 8u) return DDWG_FMT_A4L4;
        return DDWG_FMT_UNKNOWN;
    }

    if ((pf->flags & DDRAW_PF_ALPHA) && pf->bit_count == 8u)
        return DDWG_FMT_A8;

    /* D3D7 environment-bump formats. DDPIXELFORMAT overlays Du/Dv/L on its
     * R/G/B fields, so the three masks here deliberately describe signed U,
     * signed V and unsigned luminance. Accept only the layouts the D3DFORMAT
     * values specify: forwarding an arbitrary DDPF_BUMPDUDV description would
     * make the host decode its texels with the wrong component widths. */
    if (pf->flags & DDRAW_PF_BUMPDUDV) {
        if (pf->bit_count == 16u && pf->red_mask == 0x00ffu &&
                pf->green_mask == 0xff00u && !pf->blue_mask)
            return DDWG_FMT_V8U8;
        if ((pf->flags & DDRAW_PF_BUMPLUMINANCE) &&
                pf->bit_count == 16u && pf->red_mask == 0x001fu &&
                pf->green_mask == 0x03e0u && pf->blue_mask == 0xfc00u)
            return DDWG_FMT_L6V5U5;
        if ((pf->flags & DDRAW_PF_BUMPLUMINANCE) &&
                pf->bit_count == 32u && pf->red_mask == 0x000000ffu &&
                pf->green_mask == 0x0000ff00u &&
                pf->blue_mask == 0x00ff0000u)
            return DDWG_FMT_X8L8V8U8;
        return DDWG_FMT_UNKNOWN;
    }

    if (!(pf->flags & DDRAW_PF_RGB)) return DDWG_FMT_UNKNOWN;

    r = ddraw_mask_bits(pf->red_mask);
    g = ddraw_mask_bits(pf->green_mask);
    b = ddraw_mask_bits(pf->blue_mask);
    a = (pf->flags & DDRAW_PF_ALPHAPIXELS)
        ? ddraw_mask_bits(pf->alpha_mask) : 0u;

    switch (pf->bit_count) {
    case 8u:
        if (r == 3u && g == 3u && b == 2u) return DDWG_FMT_R3G3B2;
        return DDWG_FMT_UNKNOWN;
    case 16u:
        if (r == 5u && g == 6u && b == 5u) return DDWG_FMT_R5G6B5;
        if (r == 5u && g == 5u && b == 5u)
            return a == 1u ? DDWG_FMT_A1R5G5B5 : DDWG_FMT_X1R5G5B5;
        if (r == 4u && g == 4u && b == 4u)
            return a == 4u ? DDWG_FMT_A4R4G4B4 : DDWG_FMT_X4R4G4B4;
        if (r == 3u && g == 3u && b == 2u && a == 8u)
            return DDWG_FMT_A8R3G3B2;
        return DDWG_FMT_UNKNOWN;
    case 24u:
        if (r == 8u && g == 8u && b == 8u) return DDWG_FMT_R8G8B8;
        return DDWG_FMT_UNKNOWN;
    case 32u:
        if (r == 8u && g == 8u && b == 8u)
            return a == 8u ? DDWG_FMT_A8R8G8B8 : DDWG_FMT_X8R8G8B8;
        return DDWG_FMT_UNKNOWN;
    default:
        return DDWG_FMT_UNKNOWN;
    }
}

static __inline unsigned ddraw_format_is_palettised(unsigned format)
{
    return format == DDWG_FMT_P8 || format == DDWG_FMT_A8P8;
}

/* ------------------------------------------------------------------ *
 * Colour keys
 * ------------------------------------------------------------------ *
 *
 * A DDCOLORKEY is a closed interval in the *surface's own* format: a 16-bit
 * value for RGB565, a palette index for P8. The GPU compares 8-bit-per-channel
 * values, so both ends of the key and every texel have to be widened by the
 * identical truncated scaling rule used by the shared D3D texture uploader.
 *
 * Getting this wrong does not look like a bug; it looks like a one-pixel rim
 * of not-quite-transparent colour around every sprite, which is why the rule
 * lives in one place and is verified per format by
 * ../tests/ddraw_blit_colorkey_test.js.
 */
static __inline unsigned ddraw_expand_channel(unsigned value, unsigned bits)
{
    unsigned max;
    if (bits == 0u) return 0u;
    if (bits >= 8u) return (value >> (bits - 8u)) & 0xFFu;
    max = (1u << bits) - 1u;
    value &= max;
    /*
     * Truncated scaling, not the high-bit replication DirectDraw drivers used.
     *
     * The rule itself is a choice; agreeing with the host is not. The executor
     * expands a 16-bit texture on upload as `(v * 255 / max) | 0`
     * (expandRowToGPU in ../d3d9-webgpu/d3d9_executor.js), and a colour key is
     * compared against those expanded texels. Replication and truncated
     * scaling differ by one for values like 24/31 -- 198 against 197 -- so a
     * key expanded by the other rule misses on exactly those colours, and a
     * sprite keyed on one of them blits as a solid rectangle.
     *
     * The executor's rule is the incumbent: every D3D8 and D3D9 16-bit texture
     * already goes through it, so this side is the one that moves.
     * ../tests/ddraw_channel_expansion_test.js compiles this function and
     * compares it against the executor's, value by value, for every width.
     */
    return (value * 255u) / max;
}

/*
 * Converts one end of a DDCOLORKEY into the host's comparison domain:
 * 0x00RRGGBB with every channel widened to 8 bits, or the raw index for a
 * palettised surface.
 */
static __inline unsigned ddraw_color_key_to_comparison(
        const DDrawPixelFormatDesc *pf, unsigned key)
{
    unsigned r_bits, g_bits, b_bits, r, g, b;

    if (pf->flags & (DDRAW_PF_PALETTEINDEXED1 | DDRAW_PF_PALETTEINDEXED2 |
            DDRAW_PF_PALETTEINDEXED4 | DDRAW_PF_PALETTEINDEXED8))
        return key & 0xFFu;
    if (!(pf->flags & DDRAW_PF_RGB)) return key;

    r_bits = ddraw_mask_bits(pf->red_mask);
    g_bits = ddraw_mask_bits(pf->green_mask);
    b_bits = ddraw_mask_bits(pf->blue_mask);
    r = (key & pf->red_mask) >> ddraw_mask_shift(pf->red_mask);
    g = (key & pf->green_mask) >> ddraw_mask_shift(pf->green_mask);
    b = (key & pf->blue_mask) >> ddraw_mask_shift(pf->blue_mask);
    return (ddraw_expand_channel(r, r_bits) << 16) |
        (ddraw_expand_channel(g, g_bits) << 8) |
        ddraw_expand_channel(b, b_bits);
}

/* ------------------------------------------------------------------ *
 * Render states
 * ------------------------------------------------------------------ *
 *
 * The 53 states D3D7 and D3D9 share are all at the same number and pass
 * through. These are the legacy-only ones. Three of them -- FOGTABLESTART,
 * FOGTABLEEND, FOGTABLEDENSITY at 36/37/38 -- sit exactly where D3D9 put
 * FOGSTART/FOGEND/FOGDENSITY and mean the same thing, so they too pass
 * through, by an accident of history rather than by translation.
 */
#define D3D7RS_TEXTUREHANDLE 1u
#define D3D7RS_ANTIALIAS 2u
#define D3D7RS_TEXTUREADDRESS 3u
#define D3D7RS_TEXTUREPERSPECTIVE 4u
#define D3D7RS_WRAPU 5u
#define D3D7RS_WRAPV 6u
#define D3D7RS_LINEPATTERN 10u
#define D3D7RS_MONOENABLE 11u
#define D3D7RS_ROP2 12u
#define D3D7RS_PLANEMASK 13u
#define D3D7RS_TEXTUREMAG 17u
#define D3D7RS_TEXTUREMIN 18u
#define D3D7RS_TEXTUREMAPBLEND 21u
#define D3D7RS_ZVISIBLE 30u
#define D3D7RS_SUBPIXEL 31u
#define D3D7RS_SUBPIXELX 32u
#define D3D7RS_STIPPLEDALPHA 33u
#define D3D7RS_FOGTABLESTART 36u
#define D3D7RS_FOGTABLEEND 37u
#define D3D7RS_FOGTABLEDENSITY 38u
#define D3D7RS_STIPPLEENABLE 39u
#define D3D7RS_EDGEANTIALIAS 40u
#define D3D7RS_COLORKEYENABLE 41u
#define D3D7RS_BORDERCOLOR 43u
#define D3D7RS_TEXTUREADDRESSU 44u
#define D3D7RS_TEXTUREADDRESSV 45u
#define D3D7RS_MIPMAPLODBIAS 46u
#define D3D7RS_ZBIAS 47u
#define D3D7RS_ANISOTROPY 49u
#define D3D7RS_FLUSHBATCH 50u
#define D3D7RS_TRANSLUCENTSORTINDEPENDENT 51u
#define D3D7RS_STIPPLEPATTERN00 64u
#define D3D7RS_STIPPLEPATTERN31 95u
#define D3D7RS_EXTENTS 138u
#define D3D7RS_COLORKEYBLENDENABLE 144u

/* D3D9 render states the legacy headers do not name. */
#define D3D9RS_WRAP0 128u
#define D3D9RS_MULTISAMPLEANTIALIAS 161u

enum DDrawStateAction {
    /* Same number in D3D9: emit SET_RENDER_STATE unchanged. */
    DDRAW_STATE_PASS_THROUGH = 0,
    /* Emit SET_SAMPLER_STATE for every stage the legacy state covers. */
    DDRAW_STATE_SAMPLER = 1,
    /* Keep for GetRenderState, never put on the wire: the host has no code
     * for it and never had. */
    DDRAW_STATE_SHADOW_ONLY = 2,
    /* Needs bespoke handling in the proxy (texture binding, the blend
     * cascade, the depth-bias rescale, the colour-key alpha test). */
    DDRAW_STATE_SPECIAL = 3
};

/*
 * Classifies one legacy render state. *sampler_state receives the D3DSAMP_*
 * value when the answer is DDRAW_STATE_SAMPLER.
 */
static __inline unsigned ddraw_render_state_action(unsigned state,
        unsigned *sampler_state)
{
    *sampler_state = 0u;
    switch (state) {
    case D3D7RS_TEXTUREADDRESS:
        /* Sets U and V together; the caller emits two commands. */
        *sampler_state = D3D9SAMP_ADDRESSU;
        return DDRAW_STATE_SAMPLER;
    case D3D7RS_TEXTUREADDRESSU:
        *sampler_state = D3D9SAMP_ADDRESSU;
        return DDRAW_STATE_SAMPLER;
    case D3D7RS_TEXTUREADDRESSV:
        *sampler_state = D3D9SAMP_ADDRESSV;
        return DDRAW_STATE_SAMPLER;
    case D3D7RS_BORDERCOLOR:
        *sampler_state = D3D9SAMP_BORDERCOLOR;
        return DDRAW_STATE_SAMPLER;
    case D3D7RS_MIPMAPLODBIAS:
        *sampler_state = D3D9SAMP_MIPMAPLODBIAS;
        return DDRAW_STATE_SAMPLER;
    case D3D7RS_ANISOTROPY:
        *sampler_state = D3D9SAMP_MAXANISOTROPY;
        return DDRAW_STATE_SAMPLER;

    case D3D7RS_TEXTUREHANDLE:      /* -> SetTexture(0, ...) */
    case D3D7RS_TEXTUREMAG:         /* -> MAGFILTER */
    case D3D7RS_TEXTUREMIN:         /* -> MINFILTER + MIPFILTER */
    case D3D7RS_TEXTUREMAPBLEND:    /* -> the D3DTOP_* cascade */
    case D3D7RS_ZBIAS:              /* -> DEPTHBIAS, rescaled */
    case D3D7RS_COLORKEYENABLE:     /* -> alpha test over a keyed upload */
    case D3D7RS_COLORKEYBLENDENABLE:
    case D3D7RS_WRAPU:              /* -> guest primitive-coordinate unwrap */
    case D3D7RS_WRAPV:              /* -> guest primitive-coordinate unwrap */
    case D3D7RS_ANTIALIAS:          /* -> MULTISAMPLEANTIALIAS */
        return DDRAW_STATE_SPECIAL;

    /* No GPU equivalent, and no attempt to fake one. Shadowed so
     * GetRenderState still answers what the app last set. */
    case D3D7RS_TEXTUREPERSPECTIVE: /* always perspective-correct here */
    case D3D7RS_LINEPATTERN:
    case D3D7RS_MONOENABLE:
    case D3D7RS_ROP2:
    case D3D7RS_PLANEMASK:
    case D3D7RS_ZVISIBLE:
    case D3D7RS_SUBPIXEL:
    case D3D7RS_SUBPIXELX:
    case D3D7RS_STIPPLEDALPHA:
    case D3D7RS_STIPPLEENABLE:
    case D3D7RS_EDGEANTIALIAS:
    case D3D7RS_FLUSHBATCH:
    case D3D7RS_TRANSLUCENTSORTINDEPENDENT:
    case D3D7RS_EXTENTS:
        return DDRAW_STATE_SHADOW_ONLY;

    default:
        if (state >= D3D7RS_STIPPLEPATTERN00 &&
                state <= D3D7RS_STIPPLEPATTERN31)
            return DDRAW_STATE_SHADOW_ONLY;
        return DDRAW_STATE_PASS_THROUGH;
    }
}

/*
 * D3DRENDERSTATE_TEXTUREMAG / TEXTUREMIN carry a D3DTEXTUREFILTER, which
 * conflates the magnification, minification and mip filters that D3D9 splits
 * into three sampler states.
 */
#define D3D7FILTER_NEAREST 1u
#define D3D7FILTER_LINEAR 2u
#define D3D7FILTER_MIPNEAREST 3u
#define D3D7FILTER_MIPLINEAR 4u
#define D3D7FILTER_LINEARMIPNEAREST 5u
#define D3D7FILTER_LINEARMIPLINEAR 6u

#define D3D9TEXF_NONE 0u
#define D3D9TEXF_POINT 1u
#define D3D9TEXF_LINEAR 2u
#define D3D9TEXF_ANISOTROPIC 3u

static __inline void ddraw_filter_to_sampler(unsigned filter,
        unsigned *base_filter, unsigned *mip_filter)
{
    switch (filter) {
    case D3D7FILTER_NEAREST:
        *base_filter = D3D9TEXF_POINT; *mip_filter = D3D9TEXF_NONE; return;
    case D3D7FILTER_LINEAR:
        *base_filter = D3D9TEXF_LINEAR; *mip_filter = D3D9TEXF_NONE; return;
    case D3D7FILTER_MIPNEAREST:
        *base_filter = D3D9TEXF_POINT; *mip_filter = D3D9TEXF_POINT; return;
    case D3D7FILTER_MIPLINEAR:
        *base_filter = D3D9TEXF_POINT; *mip_filter = D3D9TEXF_LINEAR; return;
    case D3D7FILTER_LINEARMIPNEAREST:
        *base_filter = D3D9TEXF_LINEAR; *mip_filter = D3D9TEXF_POINT; return;
    case D3D7FILTER_LINEARMIPLINEAR:
        *base_filter = D3D9TEXF_LINEAR; *mip_filter = D3D9TEXF_LINEAR; return;
    default:
        *base_filter = D3D9TEXF_POINT; *mip_filter = D3D9TEXF_NONE; return;
    }
}

/* ------------------------------------------------------------------ *
 * Texture stage states
 * ------------------------------------------------------------------ *
 *
 * D3D7's sampler-class stage states are D3D8's, one for one, except that D3D7
 * additionally has D3DTSS_ADDRESS(12) meaning "set U and V together" and has
 * no ADDRESSW (volume textures arrived with D3D8).
 */
#define D3D7TSS_ADDRESS 12u

static __inline unsigned ddraw_stage_state_to_sampler_state(unsigned state)
{
    if (state == D3D7TSS_ADDRESS) return D3D9SAMP_ADDRESSU; /* plus ADDRESSV */
    return d3d8_stage_state_to_sampler_state(state);
}

/* ------------------------------------------------------------------ *
 * Transform states
 * ------------------------------------------------------------------ *
 *
 * The 11 shared transform states are numerically identical. Only the world
 * matrices move: D3D7 numbers them 1/4/5/6, D3D9 gives them
 * D3DTS_WORLDMATRIX(index) = 256 + index.
 */
#define D3D7TS_WORLD 1u
#define D3D7TS_WORLD1 4u
#define D3D7TS_WORLD2 5u
#define D3D7TS_WORLD3 6u
#define D3D9TS_WORLDMATRIX_BASE 256u

static __inline unsigned ddraw_transform_state_to_d3d9(unsigned state)
{
    switch (state) {
    case D3D7TS_WORLD: return D3D9TS_WORLDMATRIX_BASE + 0u;
    case D3D7TS_WORLD1: return D3D9TS_WORLDMATRIX_BASE + 1u;
    case D3D7TS_WORLD2: return D3D9TS_WORLDMATRIX_BASE + 2u;
    case D3D7TS_WORLD3: return D3D9TS_WORLDMATRIX_BASE + 3u;
    default: return state;
    }
}

/* ------------------------------------------------------------------ *
 * FVF
 * ------------------------------------------------------------------ *
 *
 * 28 FVF bits are shared and identical, but the D3D7 token cannot be passed
 * through as a D3D9 token: D3D7's D3DFVF_RESERVED1 (0x20) is the bit D3D9 gave
 * D3DFVF_PSIZE, and POSITION_MASK/RESERVED2 were widened. So the guest parses
 * with D3D7's meaning and re-assembles with D3D9's.
 */
#define DDWG_FVF_RESERVED0 0x0001u
#define DDWG_FVF_XYZ 0x0002u
#define DDWG_FVF_XYZRHW 0x0004u
#define DDWG_FVF_XYZB1 0x0006u
#define DDWG_FVF_XYZB2 0x0008u
#define DDWG_FVF_XYZB3 0x000au
#define DDWG_FVF_XYZB4 0x000cu
#define DDWG_FVF_XYZB5 0x000eu
#define DDWG_FVF_POSITION_MASK_D3D7 0x000eu
#define DDWG_FVF_NORMAL 0x0010u
#define DDWG_FVF_RESERVED1_D3D7 0x0020u  /* == D3DFVF_PSIZE in D3D9 */
#define DDWG_FVF_DIFFUSE 0x0040u
#define DDWG_FVF_SPECULAR 0x0080u
#define DDWG_FVF_TEXCOUNT_MASK 0x0f00u
#define DDWG_FVF_TEXCOUNT_SHIFT 8u
#define DDWG_FVF_RESERVED2_D3D7 0xf000u

static __inline unsigned ddraw_fvf_to_d3d9(unsigned fvf)
{
    return fvf & ~(DDWG_FVF_RESERVED0 | DDWG_FVF_RESERVED1_D3D7 |
        DDWG_FVF_RESERVED2_D3D7);
}

/*
 * D3DVERTEXTYPE, the fixed vertex layouts IDirect3DDevice2/3::DrawPrimitive
 * takes instead of an FVF.
 */
#define D3D7VT_VERTEX 1u
#define D3D7VT_LVERTEX 2u
#define D3D7VT_TLVERTEX 3u

static __inline unsigned ddraw_vertex_type_to_fvf(unsigned vertex_type)
{
    switch (vertex_type) {
    case D3D7VT_VERTEX: /* position, normal, one 2D texture coordinate */
        return DDWG_FVF_XYZ | DDWG_FVF_NORMAL | (1u << DDWG_FVF_TEXCOUNT_SHIFT);
    case D3D7VT_LVERTEX: /* pre-lit: position plus two colours */
        return DDWG_FVF_XYZ | DDWG_FVF_DIFFUSE | DDWG_FVF_SPECULAR |
            (1u << DDWG_FVF_TEXCOUNT_SHIFT);
    case D3D7VT_TLVERTEX: /* pre-transformed and pre-lit */
        return DDWG_FVF_XYZRHW | DDWG_FVF_DIFFUSE | DDWG_FVF_SPECULAR |
            (1u << DDWG_FVF_TEXCOUNT_SHIFT);
    default:
        return 0u;
    }
}

/*
 * All three legacy vertex layouts are 32 bytes, but only two of them *are*
 * their FVF equivalent laid out in memory. D3DLVERTEX carries a dwReserved
 * DWORD between position and colour that FVF XYZ|DIFFUSE|SPECULAR|TEX1 (28
 * bytes) does not have, so an LVERTEX array has to be repacked before it can
 * be a vertex stream -- passing it through with a 32-byte stride reads the
 * reserved DWORD as the diffuse colour and shifts every field after it.
 */
#define DDRAW_LEGACY_VERTEX_STRIDE 32u

static __inline unsigned ddraw_vertex_type_stride(unsigned vertex_type)
{
    switch (vertex_type) {
    case D3D7VT_VERTEX:    /* 3 + 3 floats + 2 floats */
    case D3D7VT_LVERTEX:   /* 3 floats + reserved + 2 colours + 2 floats */
    case D3D7VT_TLVERTEX:  /* 4 floats + 2 colours + 2 floats */
        return DDRAW_LEGACY_VERTEX_STRIDE;
    default: return 0u;
    }
}

/* The FVF-equivalent stride, i.e. what the vertex looks like after repacking.
 * Only LVERTEX differs from its legacy stride. */
static __inline unsigned ddraw_vertex_type_packed_stride(unsigned vertex_type)
{
    switch (vertex_type) {
    case D3D7VT_VERTEX: return 32u;
    case D3D7VT_LVERTEX: return 28u;
    case D3D7VT_TLVERTEX: return 32u;
    default: return 0u;
    }
}

static __inline unsigned ddraw_vertex_type_needs_repack(unsigned vertex_type)
{
    return vertex_type == D3D7VT_LVERTEX;
}

/* ------------------------------------------------------------------ *
 * Lights
 * ------------------------------------------------------------------ *
 *
 * D3DLIGHT7 is byte-for-byte D3DLIGHT9, so the structure is memcpy'd. Only the
 * type enumeration has two extra values, both of which D3D dropped in DX7
 * because no driver implemented them.
 */
#define D3D7LIGHT_POINT 1u
#define D3D7LIGHT_SPOT 2u
#define D3D7LIGHT_DIRECTIONAL 3u
#define D3D7LIGHT_PARALLELPOINT 4u
#define D3D7LIGHT_GLSPOT 5u

static __inline unsigned ddraw_light_type_to_d3d9(unsigned type)
{
    switch (type) {
    /* A parallel-point light is a point light whose rays are parallel, which
     * is a directional light with the position used as the direction. The
     * caller substitutes the direction; here it only picks the type. */
    case D3D7LIGHT_PARALLELPOINT: return D3D7LIGHT_DIRECTIONAL;
    case D3D7LIGHT_GLSPOT: return D3D7LIGHT_SPOT;
    default: return type;
    }
}

/* D3DLIGHT/D3DLIGHT2 dwFlags. */
#define D3D7LIGHT_ACTIVE 0x0001u
#define D3D7LIGHT_NO_SPECULAR 0x0002u

/* ------------------------------------------------------------------ *
 * D3DLIGHTSTATE (IDirect3DDevice2/3::SetLightState)
 * ------------------------------------------------------------------ */
#define D3D7LIGHTSTATE_MATERIAL 1u
#define D3D7LIGHTSTATE_AMBIENT 2u
#define D3D7LIGHTSTATE_COLORMODEL 3u
#define D3D7LIGHTSTATE_FOGMODE 4u
#define D3D7LIGHTSTATE_FOGSTART 5u
#define D3D7LIGHTSTATE_FOGEND 6u
#define D3D7LIGHTSTATE_FOGDENSITY 7u
#define D3D7LIGHTSTATE_COLORVERTEX 8u

/* The D3D9 render states those fold into. MATERIAL and COLORMODEL have no
 * render-state form: the first selects a material object, the second chose
 * between RGB and ramp colour models and only ever mattered to the ramp
 * device. */
#define D3D9RS_AMBIENT 139u
#define D3D9RS_FOGVERTEXMODE 140u
#define D3D9RS_FOGSTART 36u
#define D3D9RS_FOGEND 37u
#define D3D9RS_FOGDENSITY 38u
#define D3D9RS_COLORVERTEX 141u

static __inline unsigned ddraw_light_state_to_render_state(unsigned state)
{
    switch (state) {
    case D3D7LIGHTSTATE_AMBIENT: return D3D9RS_AMBIENT;
    case D3D7LIGHTSTATE_FOGMODE: return D3D9RS_FOGVERTEXMODE;
    case D3D7LIGHTSTATE_FOGSTART: return D3D9RS_FOGSTART;
    case D3D7LIGHTSTATE_FOGEND: return D3D9RS_FOGEND;
    case D3D7LIGHTSTATE_FOGDENSITY: return D3D9RS_FOGDENSITY;
    case D3D7LIGHTSTATE_COLORVERTEX: return D3D9RS_COLORVERTEX;
    default: return 0u; /* MATERIAL and COLORMODEL are handled by the caller */
    }
}

/* ------------------------------------------------------------------ *
 * D3DRENDERSTATE_TEXTUREMAPBLEND
 * ------------------------------------------------------------------ *
 *
 * The pre-multitexture way of saying what a texture does to the vertex colour.
 * D3D7 kept it alongside the D3DTOP_* cascade, and setting it overwrites stage
 * 0's cascade -- which is what makes it worth expanding here rather than
 * approximating: an app that sets TEXTUREMAPBLEND and then reads back
 * D3DTSS_COLOROP expects to see the expansion, and Wine, DXVK and the real
 * runtime all expand it the same way.
 */
#define D3D7TBLEND_DECAL 1u
#define D3D7TBLEND_MODULATE 2u
#define D3D7TBLEND_DECALALPHA 3u
#define D3D7TBLEND_MODULATEALPHA 4u
#define D3D7TBLEND_DECALMASK 5u
#define D3D7TBLEND_MODULATEMASK 6u
#define D3D7TBLEND_COPY 7u
#define D3D7TBLEND_ADD 8u

#define D3D9TOP_DISABLE 1u
#define D3D9TOP_SELECTARG1 2u
#define D3D9TOP_SELECTARG2 3u
#define D3D9TOP_MODULATE 4u
#define D3D9TOP_ADD 7u
#define D3D9TOP_BLENDTEXTUREALPHA 13u
#define D3D9TA_DIFFUSE 0u
#define D3D9TA_CURRENT 1u
#define D3D9TA_TEXTURE 2u

typedef struct DDrawBlendCascade {
    unsigned color_op;
    unsigned color_arg1;
    unsigned color_arg2;
    unsigned alpha_op;
    unsigned alpha_arg1;
    unsigned alpha_arg2;
    unsigned supported;
} DDrawBlendCascade;

static __inline void ddraw_texture_map_blend_cascade(unsigned blend,
        DDrawBlendCascade *out)
{
    out->color_arg1 = D3D9TA_TEXTURE;
    out->color_arg2 = D3D9TA_CURRENT;
    out->alpha_arg1 = D3D9TA_TEXTURE;
    out->alpha_arg2 = D3D9TA_CURRENT;
    out->supported = 1u;
    switch (blend) {
    case D3D7TBLEND_DECAL:
    case D3D7TBLEND_COPY:
        out->color_op = D3D9TOP_SELECTARG1;
        out->alpha_op = D3D9TOP_SELECTARG1;
        return;
    case D3D7TBLEND_MODULATE:
        out->color_op = D3D9TOP_MODULATE;
        /* MODULATE keeps the *vertex* alpha, unlike MODULATEALPHA. */
        out->alpha_op = D3D9TOP_SELECTARG2;
        out->alpha_arg2 = D3D9TA_DIFFUSE;
        return;
    case D3D7TBLEND_MODULATEALPHA:
        out->color_op = D3D9TOP_MODULATE;
        out->alpha_op = D3D9TOP_MODULATE;
        return;
    case D3D7TBLEND_DECALALPHA:
        out->color_op = D3D9TOP_BLENDTEXTUREALPHA;
        out->alpha_op = D3D9TOP_SELECTARG2;
        out->alpha_arg2 = D3D9TA_DIFFUSE;
        return;
    case D3D7TBLEND_ADD:
        out->color_op = D3D9TOP_ADD;
        out->alpha_op = D3D9TOP_SELECTARG2;
        out->alpha_arg2 = D3D9TA_DIFFUSE;
        return;
    /* DECALMASK/MODULATEMASK are the 1-bit-mask forms, which need the colour
     * key path rather than a blend cascade, and no driver of the era
     * implemented them either. Refused, not approximated. */
    case D3D7TBLEND_DECALMASK:
    case D3D7TBLEND_MODULATEMASK:
    default:
        out->color_op = D3D9TOP_SELECTARG1;
        out->alpha_op = D3D9TOP_SELECTARG1;
        out->supported = 0u;
        return;
    }
}

/* ------------------------------------------------------------------ *
 * Execute buffers (Direct3D 1/2)
 * ------------------------------------------------------------------ */
#define D3D7OP_POINT 1u
#define D3D7OP_LINE 2u
#define D3D7OP_TRIANGLE 3u
#define D3D7OP_MATRIXLOAD 4u
#define D3D7OP_MATRIXMULTIPLY 5u
#define D3D7OP_STATETRANSFORM 6u
#define D3D7OP_STATELIGHT 7u
#define D3D7OP_STATERENDER 8u
#define D3D7OP_PROCESSVERTICES 9u
#define D3D7OP_TEXTURELOAD 10u
#define D3D7OP_EXIT 11u
#define D3D7OP_BRANCHFORWARD 12u
#define D3D7OP_SPAN 13u
#define D3D7OP_SETSTATUS 14u

#endif
