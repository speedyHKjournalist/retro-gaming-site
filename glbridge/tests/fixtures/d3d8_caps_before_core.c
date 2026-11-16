/* Actual fill_caps from refactor stage4K2, site commit b3f9da5.
 * Independent previous-source regression oracle for the shared-core migration. */
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
            | D3DCAPS2_DYNAMICTEXTURES
            | D3DCAPS2_FULLSCREENGAMMA;
    caps->CursorCaps = D3DCURSORCAPS_COLOR;
    caps->PresentationIntervals = D9WG_PRESENT_INTERVAL_MASK;
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
            | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_TWEENING;
    /* Implemented through interpolated distances plus fragment discard,
     * because core WGSL has no user clip-distance builtin. */
    caps->MaxUserClipPlanes = D8WG_MAX_CLIP_PLANES;
    /*
     * Fixed-function vertex blending: the host builds the weighted sum of
     * D3DTS_WORLDMATRIX(0..3) in its generated vertex stage, and indexed
     * blending can name any of the 256 world matrices.
     *
     * Tweening instead interpolates POSITION1/NORMAL1 by TWEENFACTOR before
     * the ordinary world transform; guest CPU processing and the shared GPU
     * fixed-function pipeline both implement this separate mode.
     */
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
}
