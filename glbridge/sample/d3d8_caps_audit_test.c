// Direct3D 8 extended capability-profile and CheckDeviceFormat audit.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_WIDTH  680
#define TEST_HEIGHT 380

typedef struct FormatProbe
{
    const char *name;
    DWORD usage;
    D3DRESOURCETYPE type;
    D3DFORMAT format;
    BOOL expected_supported;
    UINT category;
} FormatProbe;

static const FormatProbe g_probes[] =
{
    {"X8R8G8B8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8, TRUE, 0},
    {"A8R8G8B8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, TRUE, 0},
    {"R5G6B5 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_R5G6B5, TRUE, 0},
    {"A1R5G5B5 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A1R5G5B5, TRUE, 0},
    {"A4R4G4B4 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A4R4G4B4, TRUE, 0},
    {"L8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_L8, TRUE, 0},
    {"A8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A8, TRUE, 0},
    {"D24S8 depth/stencil surface", D3DUSAGE_DEPTHSTENCIL,
            D3DRTYPE_SURFACE, D3DFMT_D24S8, TRUE, 0},

    {"DXT1 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1, TRUE, 1},
    {"DXT3 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT3, TRUE, 1},
    {"DXT5 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5, TRUE, 1},
    {"cube texture", 0, D3DRTYPE_CUBETEXTURE,
            D3DFMT_A8R8G8B8, TRUE, 1},
    {"volume texture", 0, D3DRTYPE_VOLUMETEXTURE,
            D3DFMT_A8R8G8B8, TRUE, 1},
    {"render-target texture", D3DUSAGE_RENDERTARGET,
            D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, TRUE, 1}
};

static const char g_class_name[] = "V86GLD3D8CapsAuditTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static HWND g_window;
static D3DCAPS8 g_caps;
static D3DFORMAT g_adapter_format;
static UINT g_mismatch_count;
static UINT g_category_mismatches[4];
static char g_first_mismatch[240];

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-caps-audit] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_dword(const char *name, DWORD value)
{
    char line[180];
    wsprintfA(line, "%s=0x%08lX (%lu)", name,
            (unsigned long)value, (unsigned long)value);
    trace_text(line);
}

static void trace_float_bits(const char *name, FLOAT value)
{
    union { FLOAT f; DWORD d; } bits;
    char line[180];
    bits.f = value;
    wsprintfA(line, "%s float-bits=0x%08lX", name,
            (unsigned long)bits.d);
    trace_text(line);
}

static void begin_stage(const char *stage)
{
    char title[250];
    trace_text(stage);
    wsprintfA(title, "D3D8 capability audit: calling %s", stage);
    if (g_window) SetWindowTextA(g_window, title);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    char title[250];
    char line[250];
    wsprintfA(line, "[d3d8-caps-audit] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
    wsprintfA(title, "D3D8 capability audit: %s (0x%08lX)",
            stage, (unsigned long)hr);
    if (g_window) SetWindowTextA(g_window, title);
    return hr;
}

static void mismatch(const char *name, UINT category)
{
    char line[250];
    if (!g_first_mismatch[0]) lstrcpynA(g_first_mismatch, name,
            sizeof(g_first_mismatch));
    ++g_mismatch_count;
    if (category < 4) ++g_category_mismatches[category];
    wsprintfA(line, "MISMATCH #%u: %s", g_mismatch_count, name);
    trace_text(line);
}

static void require_bits(const char *name, DWORD actual, DWORD required,
        UINT category)
{
    char line[240];
    if ((actual & required) == required) return;
    wsprintfA(line, "%s is missing required tested bits", name);
    mismatch(line, category);
}

static void dump_caps(void)
{
    trace_text("---- GetDeviceCaps complete dump ----");
    trace_dword("Caps", g_caps.Caps);
    trace_dword("Caps2", g_caps.Caps2);
    trace_dword("Caps3", g_caps.Caps3);
    trace_dword("PresentationIntervals", g_caps.PresentationIntervals);
    trace_dword("CursorCaps", g_caps.CursorCaps);
    trace_dword("DevCaps", g_caps.DevCaps);
    trace_dword("PrimitiveMiscCaps", g_caps.PrimitiveMiscCaps);
    trace_dword("RasterCaps", g_caps.RasterCaps);
    trace_dword("ZCmpCaps", g_caps.ZCmpCaps);
    trace_dword("SrcBlendCaps", g_caps.SrcBlendCaps);
    trace_dword("DestBlendCaps", g_caps.DestBlendCaps);
    trace_dword("AlphaCmpCaps", g_caps.AlphaCmpCaps);
    trace_dword("ShadeCaps", g_caps.ShadeCaps);
    trace_dword("TextureCaps", g_caps.TextureCaps);
    trace_dword("TextureFilterCaps", g_caps.TextureFilterCaps);
    trace_dword("CubeTextureFilterCaps", g_caps.CubeTextureFilterCaps);
    trace_dword("VolumeTextureFilterCaps", g_caps.VolumeTextureFilterCaps);
    trace_dword("TextureAddressCaps", g_caps.TextureAddressCaps);
    trace_dword("VolumeTextureAddressCaps", g_caps.VolumeTextureAddressCaps);
    trace_dword("LineCaps", g_caps.LineCaps);
    trace_dword("MaxTextureWidth", g_caps.MaxTextureWidth);
    trace_dword("MaxTextureHeight", g_caps.MaxTextureHeight);
    trace_dword("MaxVolumeExtent", g_caps.MaxVolumeExtent);
    trace_dword("MaxTextureRepeat", g_caps.MaxTextureRepeat);
    trace_dword("MaxTextureAspectRatio", g_caps.MaxTextureAspectRatio);
    trace_dword("MaxAnisotropy", g_caps.MaxAnisotropy);
    trace_float_bits("MaxVertexW", g_caps.MaxVertexW);
    trace_float_bits("GuardBandLeft", g_caps.GuardBandLeft);
    trace_float_bits("GuardBandTop", g_caps.GuardBandTop);
    trace_float_bits("GuardBandRight", g_caps.GuardBandRight);
    trace_float_bits("GuardBandBottom", g_caps.GuardBandBottom);
    trace_float_bits("ExtentsAdjust", g_caps.ExtentsAdjust);
    trace_dword("StencilCaps", g_caps.StencilCaps);
    trace_dword("FVFCaps", g_caps.FVFCaps);
    trace_dword("TextureOpCaps", g_caps.TextureOpCaps);
    trace_dword("MaxTextureBlendStages", g_caps.MaxTextureBlendStages);
    trace_dword("MaxSimultaneousTextures", g_caps.MaxSimultaneousTextures);
    trace_dword("VertexProcessingCaps", g_caps.VertexProcessingCaps);
    trace_dword("MaxActiveLights", g_caps.MaxActiveLights);
    trace_dword("MaxUserClipPlanes", g_caps.MaxUserClipPlanes);
    trace_dword("MaxVertexBlendMatrices", g_caps.MaxVertexBlendMatrices);
    trace_dword("MaxVertexBlendMatrixIndex", g_caps.MaxVertexBlendMatrixIndex);
    trace_float_bits("MaxPointSize", g_caps.MaxPointSize);
    trace_dword("MaxPrimitiveCount", g_caps.MaxPrimitiveCount);
    trace_dword("MaxVertexIndex", g_caps.MaxVertexIndex);
    trace_dword("MaxStreams", g_caps.MaxStreams);
    trace_dword("MaxStreamStride", g_caps.MaxStreamStride);
    trace_dword("VertexShaderVersion", g_caps.VertexShaderVersion);
    trace_dword("MaxVertexShaderConst", g_caps.MaxVertexShaderConst);
    trace_dword("PixelShaderVersion", g_caps.PixelShaderVersion);
    trace_float_bits("MaxPixelShaderValue", g_caps.MaxPixelShaderValue);
}

static void audit_caps(void)
{
    DWORD tested_texture_ops = D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
            | D3DTEXOPCAPS_ADD;
    begin_stage("audit tested fixed-pipeline caps");
    require_bits("PrimitiveMiscCaps", g_caps.PrimitiveMiscCaps,
            D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW
            | D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE, 2);
    require_bits("RasterCaps", g_caps.RasterCaps,
            D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_ZBIAS
            | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE, 2);
    require_bits("SrcBlendCaps", g_caps.SrcBlendCaps,
            D3DPBLENDCAPS_SRCALPHA, 2);
    require_bits("DestBlendCaps", g_caps.DestBlendCaps,
            D3DPBLENDCAPS_INVSRCALPHA, 2);
    require_bits("StencilCaps", g_caps.StencilCaps,
            D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_REPLACE, 2);
    require_bits("TextureOpCaps", g_caps.TextureOpCaps,
            tested_texture_ops, 2);
    begin_stage("audit required extended caps");
    require_bits("TextureCaps", g_caps.TextureCaps,
            D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP
            | D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP,
            3);
    require_bits("CubeTextureFilterCaps", g_caps.CubeTextureFilterCaps,
            D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT, 3);
    require_bits("VolumeTextureFilterCaps", g_caps.VolumeTextureFilterCaps,
            D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT, 3);
    require_bits("VolumeTextureAddressCaps", g_caps.VolumeTextureAddressCaps,
            D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_CLAMP, 3);
    if (g_caps.MaxVolumeExtent < 256)
        mismatch("MaxVolumeExtent is below required value 256", 3);
    if (g_caps.VertexShaderVersion < D3DVS_VERSION(1, 1))
        mismatch("VertexShaderVersion is below 1.1", 3);
    if (g_caps.MaxVertexShaderConst < 96)
        mismatch("MaxVertexShaderConst is below 96", 3);
    if (g_caps.PixelShaderVersion < D3DPS_VERSION(1, 1))
        mismatch("PixelShaderVersion is below 1.1", 3);
    if (g_caps.MaxSimultaneousTextures < 8)
        mismatch("MaxSimultaneousTextures is below required value 8", 3);
    if (g_caps.MaxTextureBlendStages < 8)
        mismatch("MaxTextureBlendStages is below required value 8", 3);
}

static void audit_formats(void)
{
    UINT i;
    trace_text("---- CheckDeviceFormat matrix ----");
    for (i = 0; i < sizeof(g_probes) / sizeof(g_probes[0]); ++i)
    {
        const FormatProbe *probe = &g_probes[i];
        HRESULT hr = IDirect3D8_CheckDeviceFormat(g_d3d, 0,
                D3DDEVTYPE_HAL, g_adapter_format, probe->usage,
                probe->type, probe->format);
        BOOL supported = SUCCEEDED(hr);
        char line[250];
        wsprintfA(line, "%s: hr=0x%08lX actual=%s expected=%s",
                probe->name, (unsigned long)hr,
                supported ? "SUPPORTED" : "HIDDEN",
                probe->expected_supported ? "SUPPORTED" : "HIDDEN");
        trace_text(line);
        if (supported != probe->expected_supported)
            mismatch(probe->name, probe->category);
    }
}

typedef struct AuditVertex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v, w;
} AuditVertex;

#define AUDIT_VERTEX_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0))
#define AUDIT_SHADER_PARAM 0x80000000u

static void resource_failure(const char *name, HRESULT hr, UINT category)
{
    char line[240];
    wsprintfA(line, "%s failed (0x%08lX)", name, (unsigned long)hr);
    mismatch(line, category);
}

static HRESULT draw_texture(IDirect3DBaseTexture8 *texture)
{
    static const AuditVertex vertices[3] =
    {
        {40.0f, 40.0f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f, 0.0f},
        {80.0f, 40.0f, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f, 0.0f},
        {40.0f, 80.0f, 0.0f, 1.0f, 0xffffffff, 0.0f, 1.0f, 1.0f}
    };
    HRESULT hr;

    IDirect3DDevice8_SetPixelShader(g_device, 0);
    IDirect3DDevice8_SetVertexShader(g_device, AUDIT_VERTEX_FVF);
    IDirect3DDevice8_SetTexture(g_device, 0, texture);
    IDirect3DDevice8_SetTextureStageState(g_device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(g_device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(g_device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(g_device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(g_device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    hr = IDirect3DDevice8_DrawPrimitiveUP(g_device, D3DPT_TRIANGLELIST, 1,
            vertices, sizeof(vertices[0]));
    IDirect3DDevice8_SetTexture(g_device, 0, NULL);
    return hr;
}

static void audit_render_target_texture(void)
{
    IDirect3DTexture8 *texture = NULL;
    IDirect3DSurface8 *surface = NULL;
    IDirect3DSurface8 *previous = NULL;
    IDirect3DSurface8 *readback = NULL;
    BOOL scene_started = FALSE;
    HRESULT hr;

    hr = IDirect3DDevice8_CreateTexture(g_device, 32, 32, 1,
            D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT, &texture);
    if (FAILED(hr))
    {
        resource_failure("render-target texture create", hr, 1);
        return;
    }
    hr = IDirect3DTexture8_GetSurfaceLevel(texture, 0, &surface);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_GetRenderTarget(g_device, &previous);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_CreateImageSurface(g_device, 32, 32,
                D3DFMT_A8R8G8B8, &readback);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_SetRenderTarget(g_device, surface, NULL);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
                D3DCOLOR_ARGB(255, 24, 196, 72), 1.0f, 0);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_CopyRects(g_device, surface, NULL, 0,
                readback, NULL);
    if (SUCCEEDED(hr))
    {
        D3DLOCKED_RECT lock;
        hr = IDirect3DSurface8_LockRect(readback, &lock, NULL,
                D3DLOCK_READONLY);
        if (SUCCEEDED(hr))
        {
            DWORD pixel = *(const DWORD *)lock.pBits;
            IDirect3DSurface8_UnlockRect(readback);
            if ((pixel & 0x00ffffffu) !=
                    (D3DCOLOR_XRGB(24, 196, 72) & 0x00ffffffu))
                hr = E_FAIL;
        }
    }
    if (previous)
    {
        HRESULT restore_hr = IDirect3DDevice8_SetRenderTarget(g_device,
                previous, NULL);
        if (SUCCEEDED(hr) && FAILED(restore_hr)) hr = restore_hr;
    }
    if (SUCCEEDED(hr))
    {
        hr = IDirect3DDevice8_BeginScene(g_device);
        scene_started = SUCCEEDED(hr);
    }
    if (scene_started)
    {
        HRESULT draw_hr = draw_texture((IDirect3DBaseTexture8 *)texture);
        HRESULT end_hr = IDirect3DDevice8_EndScene(g_device);
        if (SUCCEEDED(hr) && FAILED(draw_hr)) hr = draw_hr;
        if (SUCCEEDED(hr) && FAILED(end_hr)) hr = end_hr;
    }
    if (FAILED(hr)) resource_failure("render-target texture render/sample", hr, 1);

    if (previous) IDirect3DSurface8_Release(previous);
    if (readback) IDirect3DSurface8_Release(readback);
    if (surface) IDirect3DSurface8_Release(surface);
    IDirect3DTexture8_Release(texture);
}

static void audit_dxt_format(D3DFORMAT format, UINT block_bytes, const char *name)
{
    IDirect3DTexture8 *texture = NULL;
    UINT level;
    HRESULT hr = IDirect3DDevice8_CreateTexture(g_device, 8, 8, 4, 0,
            format, D3DPOOL_MANAGED, &texture);
    if (FAILED(hr))
    {
        resource_failure(name, hr, 1);
        return;
    }
    for (level = 0; level < 4; ++level)
    {
        D3DLOCKED_RECT lock;
        UINT width = 8u >> level;
        UINT height = 8u >> level;
        UINT blocks_x;
        UINT blocks_y;
        UINT row;
        if (!width) width = 1;
        if (!height) height = 1;
        blocks_x = (width + 3u) / 4u;
        blocks_y = (height + 3u) / 4u;
        hr = IDirect3DTexture8_LockRect(texture, level, &lock, NULL, 0);
        if (FAILED(hr))
        {
            resource_failure(name, hr, 1);
            break;
        }
        for (row = 0; row < blocks_y; ++row)
            FillMemory((BYTE *)lock.pBits + row * lock.Pitch,
                    blocks_x * block_bytes, (BYTE)(0x20u + level * 17u));
        IDirect3DTexture8_UnlockRect(texture, level);
    }
    if (SUCCEEDED(hr))
    {
        hr = draw_texture((IDirect3DBaseTexture8 *)texture);
        if (FAILED(hr)) resource_failure(name, hr, 1);
    }
    IDirect3DTexture8_Release(texture);
}

static void audit_cube_texture(void)
{
    IDirect3DCubeTexture8 *texture = NULL;
    UINT face;
    UINT level;
    HRESULT hr = IDirect3DDevice8_CreateCubeTexture(g_device, 8, 4, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture);
    if (FAILED(hr))
    {
        resource_failure("cube texture create", hr, 1);
        return;
    }
    for (face = 0; face < 6 && SUCCEEDED(hr); ++face)
    {
        for (level = 0; level < 4; ++level)
        {
            D3DLOCKED_RECT lock;
            UINT extent = 8u >> level;
            UINT y;
            if (!extent) extent = 1;
            hr = IDirect3DCubeTexture8_LockRect(texture,
                    (D3DCUBEMAP_FACES)face, level, &lock, NULL, 0);
            if (FAILED(hr)) break;
            for (y = 0; y < extent; ++y)
            {
                DWORD *row = (DWORD *)((BYTE *)lock.pBits + y * lock.Pitch);
                UINT x;
                for (x = 0; x < extent; ++x)
                    row[x] = 0xff000000u | (face * 37u << 16)
                            | (level * 61u << 8) | x * 13u;
            }
            IDirect3DCubeTexture8_UnlockRect(texture,
                    (D3DCUBEMAP_FACES)face, level);
        }
    }
    if (FAILED(hr)) resource_failure("cube face/mip update", hr, 1);
    else
    {
        hr = draw_texture((IDirect3DBaseTexture8 *)texture);
        if (FAILED(hr)) resource_failure("cube texture draw", hr, 1);
    }
    IDirect3DCubeTexture8_Release(texture);
}

static void audit_volume_texture(void)
{
    IDirect3DVolumeTexture8 *texture = NULL;
    UINT level;
    HRESULT hr = IDirect3DDevice8_CreateVolumeTexture(g_device, 4, 4, 4, 3, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture);
    if (FAILED(hr))
    {
        resource_failure("volume texture create", hr, 1);
        return;
    }
    for (level = 0; level < 3; ++level)
    {
        D3DLOCKED_BOX lock;
        UINT extent = 4u >> level;
        UINT z;
        if (!extent) extent = 1;
        hr = IDirect3DVolumeTexture8_LockBox(texture, level, &lock, NULL, 0);
        if (FAILED(hr)) break;
        for (z = 0; z < extent; ++z)
        {
            UINT y;
            for (y = 0; y < extent; ++y)
            {
                DWORD *row = (DWORD *)((BYTE *)lock.pBits
                        + z * lock.SlicePitch + y * lock.RowPitch);
                UINT x;
                for (x = 0; x < extent; ++x)
                    row[x] = 0xff000000u | (z * 53u << 16)
                            | (y * 47u << 8) | x * 41u;
            }
        }
        IDirect3DVolumeTexture8_UnlockBox(texture, level);
    }
    if (FAILED(hr)) resource_failure("volume mip update", hr, 1);
    else
    {
        hr = draw_texture((IDirect3DBaseTexture8 *)texture);
        if (FAILED(hr)) resource_failure("volume texture draw", hr, 1);
    }
    IDirect3DVolumeTexture8_Release(texture);
}

static void audit_shader_pipeline(void)
{
    static const DWORD declaration[] =
    {
        D3DVSD_STREAM(0),
        D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT4),
        D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR),
        D3DVSD_END()
    };
    static const DWORD vertex_shader[] =
    {
        D3DVS_VERSION(1, 1),
        D3DSIO_MOV, AUDIT_SHADER_PARAM | D3DSPR_RASTOUT | D3DSRO_POSITION | D3DSP_WRITEMASK_ALL,
                AUDIT_SHADER_PARAM | D3DSPR_INPUT | D3DVS_NOSWIZZLE,
        D3DSIO_MOV, AUDIT_SHADER_PARAM | D3DSPR_ATTROUT | D3DSP_WRITEMASK_ALL,
                AUDIT_SHADER_PARAM | D3DSPR_INPUT | D3DVS_NOSWIZZLE | 1,
        D3DVS_END()
    };
    static const DWORD pixel_shader[] =
    {
        D3DPS_VERSION(1, 1),
        D3DSIO_MOV, AUDIT_SHADER_PARAM | D3DSPR_TEMP | D3DSP_WRITEMASK_ALL,
                AUDIT_SHADER_PARAM | D3DSPR_INPUT | D3DSP_NOSWIZZLE,
        D3DPS_END()
    };
    static const struct { float x, y, z, w; DWORD color; } vertices[3] =
    {
        {-0.8f, -0.8f, 0.0f, 1.0f, 0xffff0000},
        {-0.4f, -0.8f, 0.0f, 1.0f, 0xff00ff00},
        {-0.8f, -0.4f, 0.0f, 1.0f, 0xff0000ff}
    };
    DWORD vs = 0;
    DWORD ps = 0;
    HRESULT hr = IDirect3DDevice8_CreateVertexShader(g_device, declaration,
            vertex_shader, &vs, 0);
    if (FAILED(hr)) resource_failure("vertex shader create", hr, 3);
    if (SUCCEEDED(hr))
    {
        hr = IDirect3DDevice8_CreatePixelShader(g_device, pixel_shader, &ps);
        if (FAILED(hr)) resource_failure("pixel shader create", hr, 3);
    }
    if (SUCCEEDED(hr)) hr = IDirect3DDevice8_SetVertexShader(g_device, vs);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice8_SetPixelShader(g_device, ps);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 1, vertices, sizeof(vertices[0]));
    if (FAILED(hr) && vs && ps) resource_failure("shader draw", hr, 3);
    IDirect3DDevice8_SetPixelShader(g_device, 0);
    IDirect3DDevice8_SetVertexShader(g_device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (ps) IDirect3DDevice8_DeletePixelShader(g_device, ps);
    if (vs) IDirect3DDevice8_DeleteVertexShader(g_device, vs);
}

typedef struct EightStageVertex
{
    float x, y, z, rhw;
    DWORD color;
    float texcoord[8][2];
} EightStageVertex;

static void audit_eight_texture_stages(void)
{
    IDirect3DTexture8 *textures[8];
    EightStageVertex vertices[3];
    UINT stage;
    UINT vertex;
    HRESULT hr = D3D_OK;

    ZeroMemory(textures, sizeof(textures));
    ZeroMemory(vertices, sizeof(vertices));
    vertices[0].x = 100.0f; vertices[0].y = 40.0f;
    vertices[1].x = 140.0f; vertices[1].y = 40.0f;
    vertices[2].x = 100.0f; vertices[2].y = 80.0f;
    for (vertex = 0; vertex < 3; ++vertex)
    {
        vertices[vertex].z = 0.0f;
        vertices[vertex].rhw = 1.0f;
        vertices[vertex].color = 0xffffffff;
        for (stage = 0; stage < 8; ++stage)
        {
            vertices[vertex].texcoord[stage][0] = vertex == 1 ? 1.0f : 0.0f;
            vertices[vertex].texcoord[stage][1] = vertex == 2 ? 1.0f : 0.0f;
        }
    }

    for (stage = 0; stage < 8; ++stage)
    {
        D3DLOCKED_RECT lock;
        hr = IDirect3DDevice8_CreateTexture(g_device, 1, 1, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &textures[stage]);
        if (FAILED(hr)) break;
        hr = IDirect3DTexture8_LockRect(textures[stage], 0, &lock, NULL, 0);
        if (FAILED(hr)) break;
        *(DWORD *)lock.pBits = 0xff808080u + stage * 0x00080808u;
        IDirect3DTexture8_UnlockRect(textures[stage], 0);
        IDirect3DDevice8_SetTexture(g_device, stage,
                (IDirect3DBaseTexture8 *)textures[stage]);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_TEXCOORDINDEX, stage);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_COLOROP, D3DTOP_MODULATE);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_COLORARG1, D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_COLORARG2, stage ? D3DTA_CURRENT : D3DTA_DIFFUSE);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(g_device, stage,
                D3DTSS_ALPHAARG2, stage ? D3DTA_CURRENT : D3DTA_DIFFUSE);
    }
    if (SUCCEEDED(hr))
    {
        IDirect3DDevice8_SetPixelShader(g_device, 0);
        hr = IDirect3DDevice8_SetVertexShader(g_device,
                D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX8);
    }
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice8_DrawPrimitiveUP(g_device,
                D3DPT_TRIANGLELIST, 1, vertices, sizeof(vertices[0]));
    if (FAILED(hr)) resource_failure("eight texture stages draw", hr, 3);

    for (stage = 0; stage < 8; ++stage)
    {
        IDirect3DDevice8_SetTexture(g_device, stage, NULL);
        if (textures[stage]) IDirect3DTexture8_Release(textures[stage]);
    }
    IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
}

static void audit_extended_resources(void)
{
    HRESULT hr;
    begin_stage("exercise extended texture and shader paths");
    audit_render_target_texture();
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr))
    {
        resource_failure("extended-path BeginScene", hr, 3);
        return;
    }
    audit_dxt_format(D3DFMT_DXT1, 8, "DXT1 mip-chain upload/draw");
    audit_dxt_format(D3DFMT_DXT3, 16, "DXT3 mip-chain upload/draw");
    audit_dxt_format(D3DFMT_DXT5, 16, "DXT5 mip-chain upload/draw");
    audit_cube_texture();
    audit_volume_texture();
    audit_eight_texture_stages();
    audit_shader_pipeline();
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) resource_failure("extended-path EndScene", hr, 3);
}

static HRESULT create_dashboard_device(void)
{
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_WIDTH;
    pp.BackBufferHeight = TEST_HEIGHT;
    pp.BackBufferFormat = g_adapter_format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = g_window;
    pp.Windowed = TRUE;
    begin_stage("CreateDevice for audit dashboard");
    return IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, g_window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
}

static HRESULT render_dashboard(void)
{
    D3DRECT panels[4] =
    {
        {35, 65, 155, 315}, {195, 65, 315, 315},
        {355, 65, 475, 315}, {515, 65, 635, 315}
    };
    DWORD colors[4];
    UINT i;
    HRESULT hr;
    char title[250];
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(3, 6, 18), 1.0f, 0);
    if (FAILED(hr)) return fail_stage("dashboard Clear background", hr);
    for (i = 0; i < 4; ++i)
    {
        colors[i] = g_category_mismatches[i]
                ? D3DCOLOR_XRGB(230, 35, 35) : D3DCOLOR_XRGB(35, 220, 90);
        hr = IDirect3DDevice8_Clear(g_device, 1, &panels[i],
                D3DCLEAR_TARGET, colors[i], 1.0f, 0);
        if (FAILED(hr)) return fail_stage("dashboard panel Clear", hr);
    }
    begin_stage("Present audit dashboard");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return fail_stage("Present", hr);
    if (!g_mismatch_count)
        SetWindowTextA(g_window,
                "D3D8 caps audit: PASS - core formats | extended textures | fixed caps | extended caps");
    else
    {
        wsprintfA(title, "D3D8 caps audit: %u MISMATCHES - first: %s",
                g_mismatch_count, g_first_mismatch);
        SetWindowTextA(g_window, title);
    }
    return D3D_OK;
}

static void release_all(void)
{
    if (g_device) IDirect3DDevice8_Release(g_device);
    if (g_d3d) IDirect3D8_Release(g_d3d);
    g_device = NULL;
    g_d3d = NULL;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    D3DDISPLAYMODE mode;
    MSG message;
    HRESULT hr = D3D_OK;
    RECT rect = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    (void)previous; (void)command_line; (void)show_command;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = g_class_name;
    if (!RegisterClassA(&wc)) return 1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_class_name, "D3D8 capability audit: starting",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 1;

    begin_stage("Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) hr = fail_stage("Direct3DCreate8 returned NULL", E_FAIL);
    if (SUCCEEDED(hr))
    {
        hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
        if (FAILED(hr)) fail_stage("GetAdapterDisplayMode", hr);
        else g_adapter_format = mode.Format;
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(&g_caps, sizeof(g_caps));
        begin_stage("GetDeviceCaps");
        hr = IDirect3D8_GetDeviceCaps(g_d3d, 0, D3DDEVTYPE_HAL, &g_caps);
        if (FAILED(hr)) fail_stage("GetDeviceCaps", hr);
    }
    if (SUCCEEDED(hr))
    {
        dump_caps();
        audit_caps();
        audit_formats();
        hr = create_dashboard_device();
        if (FAILED(hr)) fail_stage("CreateDevice dashboard", hr);
    }
    if (SUCCEEDED(hr)) audit_extended_resources();
    if (SUCCEEDED(hr)) hr = render_dashboard();
    if (SUCCEEDED(hr) && g_mismatch_count)
    {
        char message_text[300];
        wsprintfA(message_text,
                "Capability audit found %u mismatches. The first is:\r\n%s\r\n\r\nSee guest debug output for the complete caps/format matrix.",
                g_mismatch_count, g_first_mismatch);
        MessageBoxA(g_window, message_text, "D3D8 capability audit",
                MB_OK | MB_ICONWARNING);
        hr = E_FAIL;
    }
    else if (FAILED(hr))
        MessageBoxA(g_window,
                "The D3D8 capability audit could not complete. Check the window title and guest debug output.",
                "D3D8 capability audit", MB_OK | MB_ICONERROR);

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    release_all();
    return FAILED(hr) ? 1 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleA(NULL), NULL,
            GetCommandLineA(), SW_SHOWDEFAULT));
}
