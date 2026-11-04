/* Actual fill_caps from refactor stage4K2, site commit b3f9da5.
 * Independent previous-source regression oracle for the shared-core migration. */
static void fill_caps(D3DCAPS9 *caps)
{
    UINT shader_model = shader_model_version();

    ZeroMemory(caps, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = D3DADAPTER_DEFAULT;
    /*
     * Everything below claims only what this proxy implements -- but the
     * fields left at their ZeroMemory value were understating it, which is the
     * same class of bug as answering a call successfully with the wrong value.
     * A cap reported as 0 is not "conservative" when the feature works: it
     * tells a title the device cannot do something it can, and titles branch on
     * that. Each addition here names the code that backs it.
     */
    caps->Caps2 = D3DCAPS2_CANMANAGERESOURCE
            /* device_set_gamma_ramp(). */
            | D3DCAPS2_FULLSCREENGAMMA
            /* D3DUSAGE_DYNAMIC passes CheckDeviceFormat and CreateTexture, and
             * the lock paths honour DISCARD/NOOVERWRITE. */
            | D3DCAPS2_DYNAMICTEXTURES
            /* D3DUSAGE_AUTOGENMIPMAP: CreateTexture and CreateCubeTexture both
             * accept it, the host allocates the full chain and regenerates
             * everything below level 0 whenever level 0 is written -- by upload
             * or by a render pass -- and GenerateMipSubLevels forces it. The
             * comment here used to say the usage was refused, which stopped
             * being true and left a working capability unadvertised. */
            | D3DCAPS2_CANAUTOGENMIPMAP;
    /* SetCursorProperties/SetCursorPosition/ShowCursor are all implemented, so
     * reporting no hardware cursor at all was simply wrong. */
    caps->CursorCaps = D3DCURSORCAPS_COLOR;
    /* SetStreamSource carries OffsetInBytes to the host, and StretchRect
     * accepts texture-level surfaces as its source. */
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET
            | D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    /* A single-adapter device is a group of one. Zero is not a value any
     * runtime reports, and an app that iterates the group gets nothing. */
    caps->NumberOfAdaptersInGroup = 1;
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
            /* Driver-model bits rather than features: every D3D9 device serves
             * draws through the DrawPrimitives2 DDI.  Some engines read their
             * absence as "this is a DX7-era driver" and take a legacy path. */
            | D3DDEVCAPS_DRAWPRIMITIVES2
            | D3DDEVCAPS_DRAWPRIMITIVES2EX
            /* Higher-order primitives. WebGPU has no tessellation stage, so
             * DrawRectPatch/DrawTriPatch evaluate the surface in the guest and
             * draw the result as an ordinary indexed triangle list -- which is
             * why nothing about them reaches the protocol.
             *
             * HANDLEZERO and not the caching form: nothing here keeps a
             * tessellated patch between calls, so every call must carry its own
             * D3DRECTPATCH_INFO/D3DTRIPATCH_INFO. QUINTICRTPATCHES stays absent
             * because only LINEAR/QUADRATIC/CUBIC are evaluated. */
            | D3DDEVCAPS_RTPATCHES
            | D3DDEVCAPS_RTPATCHHANDLEZERO
            /* N-patches: SetNPatchMode above 1 makes every following triangle
             * draw a PN triangle, evaluated in the guest and drawn as an
             * ordinary indexed list. D3DRS_POSITIONDEGREE and
             * D3DRS_NORMALDEGREE are both honoured. */
            | D3DDEVCAPS_NPATCHES;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE
            | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW
            | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP
            | D3DPMISCCAPS_SEPARATEALPHABLEND
            | D3DPMISCCAPS_INDEPENDENTWRITEMASKS
            /* D3DRS_ZWRITEENABLE reaches the host and drives the depth-stencil
             * state's depthWriteEnabled.  Reporting no MASKZ told apps to keep
             * depth writes on for passes built around turning them off --
             * particle and decal passes above all. */
            | D3DPMISCCAPS_MASKZ
            /* Pre-transformed (XYZRHW) geometry goes through the same render
             * pass, viewport and scissor rectangle as everything else, so it is
             * clipped rather than requiring the app to pre-clip it. */
            | D3DPMISCCAPS_CLIPTLVERTS
            /* M3: D3DTA_TEMP as a stage argument and D3DTSS_RESULTARG
             * choosing it, both implemented by the host's cascade. */
            | D3DPMISCCAPS_TSSARGTEMP;
    caps->RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_DEPTHBIAS
            | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS
            | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE
            | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_FOGRANGE
            /* M3: SetScissorRect reaches the host and its render pass. */
            | D3DPRASTERCAPS_SCISSORTEST
            /* WGSL interpolates varyings perspective-correct unless a stage is
             * declared @interpolate(linear), and none are. */
            | D3DPRASTERCAPS_COLORPERSPECTIVE
            /* D3DSAMP_MIPMAPLODBIAS. WebGPU samplers have no bias field, so the
             * host applies it at the sample call instead -- textureSampleBias
             * in both the fixed-function cascade and a translated pixel shader,
             * baked as a literal and quantised to 1/16 of a level so a title
             * that animates the bias cannot mint a pipeline per frame. */
            | D3DPRASTERCAPS_MIPMAPLODBIAS;
    caps->ZCmpCaps = 0xFFu;
    /* ZERO through BOTHINVSRCALPHA plus the BLENDFACTOR pair. The host maps
     * BOTH* to its resolved source/destination pair and installs WebGPU's
     * dynamic blend constant for BLENDFACTOR/INVBLENDFACTOR. */
    caps->SrcBlendCaps = 0x3FFFu;
    caps->DestBlendCaps = 0x3FFFu;
    caps->AlphaCmpCaps = 0xFFu;
    caps->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO
            | D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT
            | D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT
            | D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR
            /* D3DRS_TWOSIDEDSTENCILMODE and the four D3DRS_CCW_STENCIL* states
             * are consumed by the host and become WebGPU's stencilBack face.
             * Shadow-volume renderers pick a two-pass fallback without this. */
            | D3DSTENCILCAPS_TWOSIDED;
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB
            /* The fixed-function host computes per-vertex material/light
             * specular when D3DRS_SPECULARENABLE is set and adds COLOR1 after
             * the texture cascade. GTA SA's car environment-map pipeline
             * explicitly requires this bit; omitting it makes
             * CCarFXRenderer::Initialise fail during the legal screen and the
             * game's frontend loader then exits cleanly. */
            | D3DPSHADECAPS_SPECULARGOURAUDRGB
            | D3DPSHADECAPS_FOGGOURAUD | D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP
            | D3DPTEXTURECAPS_PERSPECTIVE
            /* M3: real IDirect3DCubeTexture9, six faces per level, sampled
             * through a texture_cube<f32> by both the fixed-function cascade
             * and a translated pixel shader's `dcl_cube`. NONPOW2CONDITIONAL
             * and CUBEMAP_POW2 stay absent: WebGPU has no such restriction, so
             * claiming one would only make apps pad textures for nothing. */
            | D3DPTEXTURECAPS_CUBEMAP
            /* CreateCubeTexture takes a level count and the host allocates the
             * cube with that mipLevelCount, same as a 2D texture. */
            | D3DPTEXTURECAPS_MIPCUBEMAP
            | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP
            /* D3DTTFF_PROJECTED is honoured: the host divides the texture
             * coordinate by its last component before sampling. */
            | D3DPTEXTURECAPS_PROJECTED;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT
            | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MIPFPOINT
            | D3DPTFILTERCAPS_MIPFLINEAR
            /* Paired with MaxAnisotropy below: D3DTEXF_ANISOTROPIC resolves to
             * linear filtering plus the sampler's anisotropy value.  Claiming a
             * MaxAnisotropy of 16 while reporting no anisotropic filter type
             * would contradict itself. */
            | D3DPTFILTERCAPS_MINFANISOTROPIC
            | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    /* Cube textures (M3) sample through the same filter path as 2D ones, so
     * advertising D3DPTEXTURECAPS_CUBEMAP while reporting no cube filtering
     * whatsoever contradicted the TextureCaps bit right above. */
    caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
    caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP
            | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP
            /* WebGPU has no border-colour sampler, so the host clamps for the
             * physical sample and then substitutes D3DSAMP_BORDERCOLOR for every
             * coordinate that fell outside the unit domain on a BORDER axis --
             * a real implementation of the mode, not an approximation of it. */
            | D3DPTADDRESSCAPS_BORDER
            /* MIRRORONCE is exact rather than emulated: "mirror about zero,
             * then clamp" is a clamp-to-edge sampler -- which is what the host
             * already selects for the mode -- plus abs() on that axis of the
             * coordinate, applied in both the fixed-function cascade and a
             * translated pixel shader. */
            | D3DPTADDRESSCAPS_MIRRORONCE
            /* ADDRESSU, ADDRESSV and ADDRESSW are read and applied separately
             * per stage; nothing forces them to agree. */
            | D3DPTADDRESSCAPS_INDEPENDENTUV;
    /* device_stretch_rect() resolves its filter argument to point or linear. */
    caps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MINFLINEAR
            | D3DPTFILTERCAPS_MAGFLINEAR;
    /* D3DPT_LINELIST/D3DPT_LINESTRIP reach the host's draw path, and lines
     * there are textured, depth-tested, blended and fogged like triangles. */
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST
            | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
            | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X
            | D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED
            | D3DTEXOPCAPS_ADDSIGNED2X | D3DTEXOPCAPS_SUBTRACT
            | D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHAPM
            | D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_DOTPRODUCT3
            | D3DTEXOPCAPS_MULTIPLYADD | D3DTEXOPCAPS_LERP
            /* Environment bump mapping: the host displaces the next stage's
             * coordinate by this stage's sampled (du, dv) through
             * D3DTSS_BUMPENVMAT00..11, and the luminance form additionally
             * modulates by BUMPENVLSCALE/BUMPENVLOFFSET. */
            | D3DTEXOPCAPS_BUMPENVMAP
            | D3DTEXOPCAPS_BUMPENVMAPLUMINANCE
            /* The four "modulate one channel set, add the other" operations,
             * emitted by textureOpExpression() in the host's cascade. D3D9
             * defines all four for D3DTSS_COLOROP only; the host refuses them
             * as an alpha operation, which is what D3D9 does too.
             * D3DTEXOPCAPS_PREMODULATE stays absent: it modulates against the
             * *next* stage's texture and nothing in the cascade carries a value
             * backwards. */
            | D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR
            | D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA
            | D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR
            | D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA;
    /* WebGPU guarantees maxTextureDimension2D >= 8192 on every implementation,
     * and the host creates its device with the default limits, so 4096 was
     * half of what actually works. */
    caps->MaxTextureWidth = 8192;
    caps->MaxTextureHeight = 8192;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 8192;
    /* D3DSAMP_MAXANISOTROPY reaches the host and becomes the WebGPU sampler's
     * maxAnisotropy (dropped only when the app's own filters are not all
     * linear, which WebGPU forbids combining with anisotropy).  16 is WebGPU's
     * ceiling; reporting 1 meant "no anisotropic filtering" and made apps grey
     * out the setting. */
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1.0e10f;
    caps->MaxPointSize = 256.0f;
    caps->MaxPrimitiveCount = 0xFFFFFu;
    caps->MaxVertexIndex = 0xFFFFFFu;
    caps->MaxStreams = D9_MAX_STREAMS;
    /* Advisory, and nothing in the proxy or the protocol clamps a stride --
     * SetStreamSource carries it as a uint32.  255 was the figure DX7-era parts
     * reported and it rejects perfectly ordinary skinned vertex formats. */
    caps->MaxStreamStride = 65535;
    /* M5 declaration formats beyond the baseline FLOATn/D3DCOLOR set. */
    caps->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N
            | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N
            | D3DDTCAPS_USHORT2N | D3DDTCAPS_USHORT4N
            | D3DDTCAPS_UDEC3 | D3DDTCAPS_DEC3N
            | D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4;
    if (shader_model) {
        caps->VertexShaderVersion = shader_model >= 3 ?
                (DWORD)D3DVS_VERSION(3, 0) : (DWORD)D3DVS_VERSION(2, 0);
        caps->MaxVertexShaderConst = D9_MAX_VS_CONST_F;
        caps->PixelShaderVersion = shader_model >= 3 ?
                (DWORD)D3DPS_VERSION(3, 0) : (DWORD)D3DPS_VERSION(2, 0);
        /* The largest absolute value a ps_1_x register can hold. 8.0 is what
         * SM1.4-class hardware reported; anything smaller makes an engine
         * pre-scale its constants. */
        caps->PixelShader1xMaxValue = 8.0f;
        caps->DevCaps |= D3DDEVCAPS_PUREDEVICE;
        /* r0..r31 and four levels of static nesting are what the translator
         * emits WGSL for; predication is handled by guarding the instruction. */
        caps->VS20Caps.Caps = D3DVS20CAPS_PREDICATION;
        caps->VS20Caps.DynamicFlowControlDepth = D3DVS20_MAX_DYNAMICFLOWCONTROLDEPTH;
        caps->VS20Caps.NumTemps = 32;
        caps->VS20Caps.StaticFlowControlDepth = 4;
        caps->PS20Caps.Caps = D3DPS20CAPS_ARBITRARYSWIZZLE
                | D3DPS20CAPS_GRADIENTINSTRUCTIONS
                | D3DPS20CAPS_PREDICATION
                | D3DPS20CAPS_NODEPENDENTREADLIMIT
                | D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT;
        caps->PS20Caps.DynamicFlowControlDepth = 0; /* see the note above */
        caps->PS20Caps.NumTemps = 32;
        caps->PS20Caps.StaticFlowControlDepth = 4;
        caps->PS20Caps.NumInstructionSlots = 512;
        caps->MaxVShaderInstructionsExecuted = 65535;
        caps->MaxPShaderInstructionsExecuted = 65535;
        caps->MaxVertexShader30InstructionSlots = shader_model >= 3 ?
                32768 : 0;
        caps->MaxPixelShader30InstructionSlots = shader_model >= 3 ?
                32768 : 0;
        caps->VertexTextureFilterCaps = shader_model >= 3
                ? D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR
                    | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR
                    | D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR
                : 0;
    } else {
        caps->VertexShaderVersion = (DWORD)D3DVS_VERSION(0, 0);
        caps->MaxVertexShaderConst = 0;
        caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(0, 0);
    }
    /* Eight texture coordinate sets, plus per-vertex point size: a declaration
     * carrying D3DDECLUSAGE_PSIZE (or the D3DFVF_PSIZE bit) is bound to the
     * host's point-size input and used in place of D3DRS_POINTSIZE. */
    caps->FVFCaps = (8u & D3DFVFCAPS_TEXCOORDCOUNTMASK) | D3DFVFCAPS_PSIZE;
    caps->MaxTextureBlendStages = D9_MAX_TEXTURE_STAGES;
    caps->MaxSimultaneousTextures = D9_MAX_TEXTURE_STAGES;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN
            | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS
            | D3DVTXPCAPS_LOCALVIEWER
            /* All four of D3DRS_{DIFFUSE,SPECULAR,AMBIENT,EMISSIVE}MATERIALSOURCE
             * are consumed, so a vertex colour can stand in for any material
             * channel.  Without this bit an engine duplicates its meshes per
             * material instead of colouring them per vertex. */
            | D3DVTXPCAPS_MATERIALSOURCE7
            /* D3DTSS_TCI_SPHEREMAP. The host's fixed-function vertex stage
             * generates the classic sphere-map projection of the eye-space
             * reflection vector -- the same formula GL_SPHERE_MAP and wined3d
             * use, which is what titles authored their sphere-map art
             * against. */
            | D3DVTXPCAPS_TEXGEN_SPHEREMAP | D3DVTXPCAPS_TWEENING;
    caps->MaxActiveLights = 8;
    /* Implemented through interpolated distances plus fragment discard because
     * core WGSL has no user clip-distance builtin. */
    caps->MaxUserClipPlanes = reported_user_clip_planes();
    /* Fixed-function vertex blending: the host builds the weighted sum of
     * D3DTS_WORLDMATRIX(0..3) in its generated vertex stage, so 4 -- D3D9's own
     * maximum -- is now backed rather than claimed. Indexed blending is backed
     * too, and BLENDINDICES can name any of D3D9's 256 world matrices; the host
     * uploads a palette sized from the matrices the guest has actually set.
     *
     * Tweening independently interpolates POSITION1/NORMAL1 before the same
     * transforms, lighting, texgen, fog and clipping. The common CPU path and
     * real WebGPU pixels cover that separate mode before advertising it. */
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    /* The tessellation ceiling d9_draw_npatch() clamps to. It bounds the
     * scratch each draw allocates; 16-bit output indices bound it further for
     * a large mesh, and a draw that would overflow them is left flat. */
    caps->MaxNpatchTessellationLevel = (float)D9_PATCH_MAX_SEGMENTS;
    apply_permissive_caps(caps);
    /* Render targets and MRT: the host binds up to four colour attachments and
     * a translated pixel shader's oC0..oC3 reach them (M4 work brought forward
     * because a 2005-era D3D9 game renders most of its frame into textures). */
    caps->NumSimultaneousRTs = D9_MAX_RENDER_TARGETS;
}
