#ifndef D3D8_PROTOCOL_H
#define D3D8_PROTOCOL_H

/*
 * The D3D8 guest frontend does not define a wire protocol of its own.
 *
 * D3D8 is very nearly a semantic subset of D3D9: the two APIs share render
 * state numbering, texture-stage-state numbering, FVF bit layout, primitive
 * types, formats, and -- for shader model 1.x -- bytecode token encoding.
 * The places they genuinely differ are few, local, and enumerated in this
 * header.  Every one of them is a guest-side translation, so the host sees a
 * single D3D9-shaped command stream no matter which API produced it.
 *
 * This is the same architecture DXVK uses (its d3d8 is a translation layer
 * over its d3d9 rather than a second backend), and for the same reason: a
 * second backend has to re-implement cube/volume textures, clip planes, point
 * sprites, vertex blending, multi-stream layouts, palettes, multisampling,
 * readback and the whole shader translator, all of which ../d3d9-webgpu/
 * already implements and 3DMark06 already exercises.
 *
 * Consequences that are easy to get wrong, so stated once here:
 *
 *  - Batches ride V86GL_CTRL_D3D9_BATCH (0xFFE1) and are decoded by
 *    ../d3d9-webgpu/d3d9_executor.js.  ../d3d8-webgpu/d3d8_executor.js is no
 *    longer on the path for new work; see this directory's README.
 *  - The last D9WG_RESPONSE_REGION_BYTES of the DMA mapping are the host's
 *    response region, not batch space.  batch_capacity() must exclude it.
 *  - A guest process loads either d3d8.dll or d3d9.dll, never both, so the
 *    two frontends never contend for the single DMA arena.
 */

#include "../d3d9proxy/d3d9_protocol.h"

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

/*
 * D3D8's ZBIAS is an integer 0..16 meaning "pull this many units towards the
 * viewer", with the unit left to the driver.  D3D9's DEPTHBIAS is a float
 * added to the depth value directly, in normalised depth units.  Wine and
 * DXVK both settle on treating one ZBIAS step as one unit of a 24-bit depth
 * buffer's resolution scaled by a small constant; the exact constant is a
 * driver-quality choice, not a specification, and this one matches DXVK's so
 * that content tuned against it (the z-fighting-avoidance decals in most D3D8
 * titles) lands the same way here.
 *
 * Sign: D3D8 biases towards the viewer, i.e. makes the fragment win the depth
 * test, which is a *negative* offset under D3D9's convention.
 */
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

/* ------------------------------------------------------------------ *
 * Vertex declarations
 * ------------------------------------------------------------------ *
 *
 * D3D8 declares vertex shader inputs as a D3DVSD_* token stream that binds
 * stream data to explicit vertex register numbers (v0..v15).  D3D9 declares
 * them as D3DVERTEXELEMENT9 entries that bind stream data to *usages*
 * (POSITION, NORMAL, TEXCOORD[n], ...), and the shader's own dcl_ statements
 * connect a usage to a register.
 *
 * vs_1_1 bytecode carries no dcl_ statements at all, so the register->usage
 * association it relies on is exactly the one its declaration established.
 * The guest therefore synthesises a usage for each declared register using
 * D3D8's own fixed register semantics (see the "Vertex Shader Registers"
 * table in the D3D8 SDK), which is what a D3D8 app's declaration means:
 */
#define D3D8_VSD_REG_POSITION 0u
#define D3D8_VSD_REG_BLENDWEIGHT 1u
#define D3D8_VSD_REG_BLENDINDICES 2u
#define D3D8_VSD_REG_NORMAL 3u
#define D3D8_VSD_REG_POINTSIZE 4u
#define D3D8_VSD_REG_DIFFUSE 5u
#define D3D8_VSD_REG_SPECULAR 6u
#define D3D8_VSD_REG_TEXCOORD0 7u
#define D3D8_VSD_REG_TEXCOORD7 14u
#define D3D8_VSD_REG_POSITION2 15u
#define D3D8_VSD_REG_NORMAL2 16u

/* D3DVSD token field layout (d3d8types.h). */
#define D3D8VSD_TOKENTYPESHIFT 29u
#define D3D8VSD_TOKENTYPEMASK (0x7u << D3D8VSD_TOKENTYPESHIFT)
#define D3D8VSD_TOKEN_NOP 0u
#define D3D8VSD_TOKEN_STREAM 1u
#define D3D8VSD_TOKEN_STREAMDATA 2u
#define D3D8VSD_TOKEN_TESSELLATOR 3u
#define D3D8VSD_TOKEN_CONSTMEM 4u
#define D3D8VSD_TOKEN_EXT 5u
#define D3D8VSD_TOKEN_END 7u
#define D3D8VSD_STREAMNUMBERMASK 0xFu
#define D3D8VSD_VERTEXREGMASK 0x1Fu
#define D3D8VSD_DATATYPESHIFT 16u
#define D3D8VSD_DATATYPEMASK (0xFu << D3D8VSD_DATATYPESHIFT)
/* D3DVSD_SKIP sets bit 28 of a STREAMDATA token; the DWORD count it skips
 * sits in the same field a REG token uses for its data type. */
#define D3D8VSD_SKIPFLAG 0x10000000u
#define D3D8VSD_STREAM_TESSFLAG 0x10000000u
#define D3D8VSD_SKIPCOUNTSHIFT 16u
#define D3D8VSD_SKIPCOUNTMASK (0xFu << D3D8VSD_SKIPCOUNTSHIFT)
#define D3D8VSD_CONSTCOUNTSHIFT 25u
#define D3D8VSD_CONSTCOUNTMASK (0xFu << D3D8VSD_CONSTCOUNTSHIFT)
#define D3D8VSD_CONSTADDRESSMASK 0x7Fu

/* D3DVSDT_* data types, and the D3DDECLTYPE_* they become. */
#define D3D8VSDT_FLOAT1 0u
#define D3D8VSDT_FLOAT2 1u
#define D3D8VSDT_FLOAT3 2u
#define D3D8VSDT_FLOAT4 3u
#define D3D8VSDT_D3DCOLOR 4u
#define D3D8VSDT_UBYTE4 5u
#define D3D8VSDT_SHORT2 6u
#define D3D8VSDT_SHORT4 7u

/* D3DDECLTYPE_*, named here because d3d8.h does not declare them. */
#define D3D9DECLTYPE_FLOAT1 0u
#define D3D9DECLTYPE_FLOAT2 1u
#define D3D9DECLTYPE_FLOAT3 2u
#define D3D9DECLTYPE_FLOAT4 3u
#define D3D9DECLTYPE_D3DCOLOR 4u
#define D3D9DECLTYPE_UBYTE4 5u
#define D3D9DECLTYPE_SHORT2 6u
#define D3D9DECLTYPE_SHORT4 7u
#define D3D9DECLTYPE_UNUSED 17u

#define D3D9DECLMETHOD_DEFAULT 0u

#define D3D9DECLUSAGE_POSITION 0u
#define D3D9DECLUSAGE_BLENDWEIGHT 1u
#define D3D9DECLUSAGE_BLENDINDICES 2u
#define D3D9DECLUSAGE_NORMAL 3u
#define D3D9DECLUSAGE_PSIZE 4u
#define D3D9DECLUSAGE_TEXCOORD 5u
#define D3D9DECLUSAGE_TANGENT 6u
#define D3D9DECLUSAGE_BINORMAL 7u
#define D3D9DECLUSAGE_TESSFACTOR 8u
#define D3D9DECLUSAGE_POSITIONT 9u
#define D3D9DECLUSAGE_COLOR 10u
#define D3D9DECLUSAGE_FOG 11u
#define D3D9DECLUSAGE_DEPTH 12u
#define D3D9DECLUSAGE_SAMPLE 13u

/* Byte size of each D3DVSDT_* type, indexed by the type value. */
static __inline unsigned d3d8_vsdt_size(unsigned type)
{
    switch (type) {
    case D3D8VSDT_FLOAT1: return 4u;
    case D3D8VSDT_FLOAT2: return 8u;
    case D3D8VSDT_FLOAT3: return 12u;
    case D3D8VSDT_FLOAT4: return 16u;
    case D3D8VSDT_D3DCOLOR: return 4u;
    case D3D8VSDT_UBYTE4: return 4u;
    case D3D8VSDT_SHORT2: return 4u;
    case D3D8VSDT_SHORT4: return 8u;
    default: return 0u;
    }
}

/*
 * D3DVSDT_* and D3DDECLTYPE_* happen to agree on 0..7, but that is a
 * coincidence of two independently-written enums rather than a guarantee, so
 * the mapping is spelled out instead of assumed.
 */
static __inline unsigned d3d8_vsdt_to_decltype(unsigned type)
{
    switch (type) {
    case D3D8VSDT_FLOAT1: return D3D9DECLTYPE_FLOAT1;
    case D3D8VSDT_FLOAT2: return D3D9DECLTYPE_FLOAT2;
    case D3D8VSDT_FLOAT3: return D3D9DECLTYPE_FLOAT3;
    case D3D8VSDT_FLOAT4: return D3D9DECLTYPE_FLOAT4;
    case D3D8VSDT_D3DCOLOR: return D3D9DECLTYPE_D3DCOLOR;
    case D3D8VSDT_UBYTE4: return D3D9DECLTYPE_UBYTE4;
    case D3D8VSDT_SHORT2: return D3D9DECLTYPE_SHORT2;
    case D3D8VSDT_SHORT4: return D3D9DECLTYPE_SHORT4;
    default: return D3D9DECLTYPE_UNUSED;
    }
}

/*
 * The register -> (usage, usage_index) association a D3D8 declaration implies.
 * Registers v15/v16 carry the second tween position/normal.  Anything else is given a TEXCOORD usage with a usage index that
 * cannot collide with a real texture coordinate set, so a shader that used a
 * register purely as an opaque input still receives its data.
 */
static __inline void d3d8_vsd_register_usage(unsigned reg,
        unsigned *usage, unsigned *usage_index)
{
    switch (reg) {
    case D3D8_VSD_REG_POSITION:
        *usage = D3D9DECLUSAGE_POSITION; *usage_index = 0u; return;
    case D3D8_VSD_REG_BLENDWEIGHT:
        *usage = D3D9DECLUSAGE_BLENDWEIGHT; *usage_index = 0u; return;
    case D3D8_VSD_REG_BLENDINDICES:
        *usage = D3D9DECLUSAGE_BLENDINDICES; *usage_index = 0u; return;
    case D3D8_VSD_REG_NORMAL:
        *usage = D3D9DECLUSAGE_NORMAL; *usage_index = 0u; return;
    case D3D8_VSD_REG_POINTSIZE:
        *usage = D3D9DECLUSAGE_PSIZE; *usage_index = 0u; return;
    case D3D8_VSD_REG_DIFFUSE:
        *usage = D3D9DECLUSAGE_COLOR; *usage_index = 0u; return;
    case D3D8_VSD_REG_SPECULAR:
        *usage = D3D9DECLUSAGE_COLOR; *usage_index = 1u; return;
    case D3D8_VSD_REG_POSITION2:
        *usage = D3D9DECLUSAGE_POSITION; *usage_index = 1u; return;
    case D3D8_VSD_REG_NORMAL2:
        *usage = D3D9DECLUSAGE_NORMAL; *usage_index = 1u; return;
    default:
        if (reg >= D3D8_VSD_REG_TEXCOORD0 && reg <= D3D8_VSD_REG_TEXCOORD7) {
            *usage = D3D9DECLUSAGE_TEXCOORD;
            *usage_index = reg - D3D8_VSD_REG_TEXCOORD0;
            return;
        }
        *usage = D3D9DECLUSAGE_TEXCOORD;
        *usage_index = 8u + (reg & 0x7u);
        return;
    }
}

#endif
