#ifndef D9WG_STATE_BRIDGE8_H
#define D9WG_STATE_BRIDGE8_H
#include <stdint.h>
#define D9WG_RENDER_STATES8 256u
#define D9WG_TEXTURE_STAGES8 8u
#define D9WG_TEXTURE_STAGE_STATES8 32u
/* ------------------------------------------------------------------ *
 * Render states
 * ------------------------------------------------------------------ *
 *
 * D3D8 and D3D9 agree on the numeric value of every render state that exists
 * in both.  Only two classes need attention:
 *
 *  1. States D3D9 deleted outright.  The guest keeps shadowing them so
 *     GetRenderState() still answers what the app last set, but nothing goes
 *     on the wire -- the host has no code for them and never had.
 *  2. D3DRS_ZBIAS, which D3D9 replaced with a differently-scaled float.
 */

#define D3D8RS_LINEPATTERN 10u
#define D3D8RS_ZVISIBLE 30u
#define D3D8RS_EDGEANTIALIAS 40u
#define D3D8RS_ZBIAS 47u
#define D3D8RS_SOFTWAREVERTEXPROCESSING 153u
#define D3D8RS_PATCHSEGMENTS 164u
#define D3D8RS_POSITIONORDER 172u
#define D3D8RS_NORMALORDER 173u

/* D3D9 replacements the D3D8 header does not name. */
#define D3D9RS_DEPTHBIAS 195u
#define D3D9RS_SLOPESCALEDEPTHBIAS 175u

/* Compatibility policy inherited from the public8 frontend: clamp integer
 * ZBIAS to16 and add a negative floating depth offset. The scale is a local
 * approximation, not a native-driver precision guarantee. */
#define D3D8_ZBIAS_TO_DEPTHBIAS_STEP (-0.000005f)

/* ------------------------------------------------------------------ *
 * Texture stage state vs sampler state
 * ------------------------------------------------------------------ *
 *
 * D3D8 addressed sampler configuration through SetTextureStageState, and D3D9
 * split it into a separate SetSamplerState with its own (different) numbering.
 * The blending cascade states -- COLOROP, ALPHAOP, the COLORARG/ALPHAARG
 * family, BUMPENVMAT/BUMPENVL, TEXCOORDINDEX, TEXTURETRANSFORMFLAGS and
 * RESULTARG -- kept D3D8's numbering in D3D9, so they pass through untouched.
 * These ten are the ones that move.
 */

#define D3D8TSS_ADDRESSU 13u
#define D3D8TSS_ADDRESSV 14u
#define D3D8TSS_BORDERCOLOR 15u
#define D3D8TSS_MAGFILTER 16u
#define D3D8TSS_MINFILTER 17u
#define D3D8TSS_MIPFILTER 18u
#define D3D8TSS_MIPMAPLODBIAS 19u
#define D3D8TSS_MAXMIPLEVEL 20u
#define D3D8TSS_MAXANISOTROPY 21u
#define D3D8TSS_ADDRESSW 25u

#define D3D9SAMP_ADDRESSU 1u
#define D3D9SAMP_ADDRESSV 2u
#define D3D9SAMP_ADDRESSW 3u
#define D3D9SAMP_BORDERCOLOR 4u
#define D3D9SAMP_MAGFILTER 5u
#define D3D9SAMP_MINFILTER 6u
#define D3D9SAMP_MIPFILTER 7u
#define D3D9SAMP_MIPMAPLODBIAS 8u
#define D3D9SAMP_MAXMIPLEVEL 9u
#define D3D9SAMP_MAXANISOTROPY 10u

/*
 * Returns the D3D9 sampler state a D3D8 texture stage state maps to, or 0
 * (which is not a valid D3DSAMP value) when the state is a real blending-
 * cascade stage state that D3D9 kept in SetTextureStageState.
 */
static __inline unsigned d3d8_stage_state_to_sampler_state(unsigned state)
{
    switch (state) {
    case D3D8TSS_ADDRESSU: return D3D9SAMP_ADDRESSU;
    case D3D8TSS_ADDRESSV: return D3D9SAMP_ADDRESSV;
    case D3D8TSS_ADDRESSW: return D3D9SAMP_ADDRESSW;
    case D3D8TSS_BORDERCOLOR: return D3D9SAMP_BORDERCOLOR;
    case D3D8TSS_MAGFILTER: return D3D9SAMP_MAGFILTER;
    case D3D8TSS_MINFILTER: return D3D9SAMP_MINFILTER;
    case D3D8TSS_MIPFILTER: return D3D9SAMP_MIPFILTER;
    case D3D8TSS_MIPMAPLODBIAS: return D3D9SAMP_MIPMAPLODBIAS;
    case D3D8TSS_MAXMIPLEVEL: return D3D9SAMP_MAXMIPLEVEL;
    case D3D8TSS_MAXANISOTROPY: return D3D9SAMP_MAXANISOTROPY;
    default: return 0u;
    }
}

/* Pure translation only. Raw API8 shadows (including deleted states and
 * unclamped ZBIAS) and their state-block masks belong to the frontend. */
#define D9WG_STATE8_DROP 0u
#define D9WG_STATE8_RENDER 1u
#define D9WG_STATE8_STAGE 2u
#define D9WG_STATE8_SAMPLER 3u
typedef struct D9WGState8 {
    uint32_t kind, state, value;
} D9WGState8;
static __inline D9WGState8 d9wg_render_state8(uint32_t state, uint32_t value)
{
    D9WGState8 translated = {D9WG_STATE8_RENDER, state, value};
    switch (state) {
    case D3D8RS_LINEPATTERN:
    case D3D8RS_ZVISIBLE:
    case D3D8RS_EDGEANTIALIAS:
    case D3D8RS_SOFTWAREVERTEXPROCESSING:
    case D3D8RS_PATCHSEGMENTS:
    case D3D8RS_POSITIONORDER:
    case D3D8RS_NORMALORDER:
        translated.kind = D9WG_STATE8_DROP;
        break;
    case D3D8RS_ZBIAS: {
        union { float f; uint32_t bits; } bias;
        bias.f = (float)(value > 16u ? 16u : value)
                * D3D8_ZBIAS_TO_DEPTHBIAS_STEP;
        translated.state = D3D9RS_DEPTHBIAS;
        translated.value = bias.bits;
        break;
    }
    default:
        break;
    }
    return translated;
}
static __inline D9WGState8 d9wg_texture_stage_state8(uint32_t state, uint32_t value)
{
    uint32_t sampler = d3d8_stage_state_to_sampler_state(state);
    D9WGState8 translated = {sampler ? D9WG_STATE8_SAMPLER : D9WG_STATE8_STAGE,
            sampler ? sampler : state, value};
    return translated;
}
#endif
