// Direct3D 8 alpha-test / alpha-blend test for WineD3D -> v86 GL.
//
// The left panel validates alpha discard and its depth-write semantics.  An
// opaque checker diamond with a low-alpha magenta fringe is drawn first, then
// a farther cyan probe quad.  The probe must fill only the discarded pixels.
//
// The right panel validates SRCALPHA / INVSRCALPHA and transparent-pass depth
// writes.  An orange alpha-gradient quad is drawn with ZWRITE disabled, then a
// farther green probe stripe.  The stripe must remain visible through the
// depth test because the blended pass did not modify Z.
//
// Build for Windows XP as documented in ../winproxy/README.md.  The command
// uses a 32-bit MinGW compiler and avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH   640
#define TEST_CLIENT_HEIGHT  480
#define TEST_TEXTURE_WIDTH   64
#define TEST_TEXTURE_HEIGHT  64
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    FLOAT rhw;
    DWORD color;
    FLOAT u;
    FLOAT v;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8AlphaBlendTest";

/* Five independent two-triangle quads, rendered in the order documented. */
static const TestVertex g_vertices[] =
{
    /* 0: opaque dark-blue background, z = 0.9. */
    { 70.0f,  70.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 0.0f, 0.0f},
    {570.0f,  70.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 1.0f, 0.0f},
    {570.0f, 410.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 1.0f, 1.0f},
    { 70.0f,  70.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 0.0f, 0.0f},
    {570.0f, 410.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 1.0f, 1.0f},
    { 70.0f, 410.0f, 0.9f, 1.0f, D3DCOLOR_XRGB(16, 40, 112), 0.0f, 1.0f},

    /* 1: alpha-tested checker diamond, z = 0.3. */
    {100.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
    {300.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0.0f},
    {300.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    {100.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
    {300.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    {100.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},

    /* 2: farther cyan alpha-test depth probe, z = 0.5. */
    {100.0f, 130.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 0.0f, 0.0f},
    {300.0f, 130.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 1.0f, 0.0f},
    {300.0f, 350.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 1.0f, 1.0f},
    {100.0f, 130.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 0.0f, 0.0f},
    {300.0f, 350.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 1.0f, 1.0f},
    {100.0f, 350.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 192, 208), 0.0f, 1.0f},

    /* 3: blended orange alpha gradient, z = 0.3, ZWRITE disabled. */
    {340.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
    {540.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0.0f},
    {540.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    {340.0f, 130.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
    {540.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    {340.0f, 350.0f, 0.3f, 1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},

    /* 4: farther green ZWRITE probe stripe, z = 0.5. */
    {420.0f, 110.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 0.0f, 0.0f},
    {460.0f, 110.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 1.0f, 0.0f},
    {460.0f, 370.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 1.0f, 1.0f},
    {420.0f, 110.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 0.0f, 0.0f},
    {460.0f, 370.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 1.0f, 1.0f},
    {420.0f, 370.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(32, 240, 80), 0.0f, 1.0f},
};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DTexture8 *g_cutout_texture;
static IDirect3DTexture8 *g_blend_texture;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-alpha] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];

    wsprintfA(line, "[d3d8-alpha] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[224];

    wsprintfA(title, "D3D8 alpha: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[224];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 alpha: calling %s", stage);
    if (g_window)
    {
        SetWindowTextA(g_window, title);
        RedrawWindow(g_window, NULL, NULL,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    }
}

static HRESULT failed(const char *stage, HRESULT hr)
{
    g_failed_stage = stage;
    trace_hresult(stage, hr);
    return hr;
}

static void release_d3d8(void)
{
    if (g_blend_texture)
    {
        IDirect3DTexture8_Release(g_blend_texture);
        g_blend_texture = NULL;
    }

    if (g_cutout_texture)
    {
        IDirect3DTexture8_Release(g_cutout_texture);
        g_cutout_texture = NULL;
    }

    if (g_vertex_buffer)
    {
        IDirect3DVertexBuffer8_Release(g_vertex_buffer);
        g_vertex_buffer = NULL;
    }

    if (g_device)
    {
        IDirect3DDevice8_Release(g_device);
        g_device = NULL;
    }

    if (g_d3d)
    {
        IDirect3D8_Release(g_d3d);
        g_d3d = NULL;
    }
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS present_parameters;
    HRESULT hr;

    begin_stage("01 Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d)
    {
        trace_text("Direct3DCreate8 returned NULL");
        g_failed_stage = "Direct3DCreate8 returned NULL";
        return E_FAIL;
    }

    ZeroMemory(&mode, sizeof(mode));
    begin_stage("02 GetAdapterDisplayMode");
    hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, D3DADAPTER_DEFAULT, &mode);
    if (FAILED(hr)) return failed("GetAdapterDisplayMode", hr);

    begin_stage("03 CheckDeviceType");
    hr = IDirect3D8_CheckDeviceType(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, mode.Format, TRUE);
    if (FAILED(hr)) return failed("CheckDeviceType", hr);

    begin_stage("04 CheckDeviceFormat A8R8G8B8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, 0, D3DRTYPE_TEXTURE,
            D3DFMT_A8R8G8B8);
    if (FAILED(hr)) return failed("CheckDeviceFormat(A8R8G8B8)", hr);

    begin_stage("05 CheckDeviceFormat D24S8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, D3DUSAGE_DEPTHSTENCIL,
            D3DRTYPE_SURFACE, D3DFMT_D24S8);
    if (FAILED(hr)) return failed("CheckDeviceFormat(D24S8)", hr);

    begin_stage("06 CheckDepthStencilMatch D24S8");
    hr = IDirect3D8_CheckDepthStencilMatch(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, mode.Format, D3DFMT_D24S8);
    if (FAILED(hr)) return failed("CheckDepthStencilMatch(D24S8)", hr);

    ZeroMemory(&present_parameters, sizeof(present_parameters));
    present_parameters.BackBufferWidth = TEST_CLIENT_WIDTH;
    present_parameters.BackBufferHeight = TEST_CLIENT_HEIGHT;
    present_parameters.BackBufferFormat = mode.Format;
    present_parameters.BackBufferCount = 1;
    present_parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.hDeviceWindow = hwnd;
    present_parameters.Windowed = TRUE;
    present_parameters.EnableAutoDepthStencil = TRUE;
    present_parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    present_parameters.FullScreen_PresentationInterval =
            D3DPRESENT_INTERVAL_DEFAULT;

    begin_stage("07 CreateDevice auto D24S8");
    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    if (FAILED(hr)) return failed("CreateDevice(auto D24S8)", hr);

    return D3D_OK;
}

static void fill_cutout_texture(const D3DLOCKED_RECT *locked_rect)
{
    UINT x;
    UINT y;

    for (y = 0; y < TEST_TEXTURE_HEIGHT; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked_rect->pBits
                + y * locked_rect->Pitch);

        for (x = 0; x < TEST_TEXTURE_WIDTH; ++x)
        {
            int dx = (int)x - 31;
            int dy = (int)y - 31;
            int distance;
            DWORD alpha;
            DWORD color;

            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            distance = dx + dy;

            if (distance <= 22)
            {
                BOOL alternate = ((x / 8) ^ (y / 8)) & 1;
                alpha = 255;
                color = alternate ? 0x00ffe020u : 0x002060ffu;
            }
            else
            {
                /* The alpha=64 fringe must also be rejected by ref=127. */
                alpha = distance <= 27 ? 64 : 0;
                color = 0x00ff00ffu;
            }

            row[x] = (alpha << 24) | color;
        }
    }
}

static void fill_blend_texture(const D3DLOCKED_RECT *locked_rect)
{
    UINT x;
    UINT y;

    for (y = 0; y < TEST_TEXTURE_HEIGHT; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked_rect->pBits
                + y * locked_rect->Pitch);

        for (x = 0; x < TEST_TEXTURE_WIDTH; ++x)
        {
            DWORD alpha = 32 + (x * 192) / (TEST_TEXTURE_WIDTH - 1);
            DWORD green = 48 + ((y / 8) & 1) * 32;
            row[x] = D3DCOLOR_ARGB(alpha, 255, green, 24);
        }
    }
}

static HRESULT create_textures(void)
{
    D3DLOCKED_RECT locked_rect;
    HRESULT hr;

    begin_stage("08 CreateTexture cutout");
    hr = IDirect3DDevice8_CreateTexture(g_device,
            TEST_TEXTURE_WIDTH, TEST_TEXTURE_HEIGHT, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_cutout_texture);
    if (FAILED(hr)) return failed("CreateTexture(cutout)", hr);

    ZeroMemory(&locked_rect, sizeof(locked_rect));
    begin_stage("09 Cutout Texture::LockRect");
    hr = IDirect3DTexture8_LockRect(g_cutout_texture, 0,
            &locked_rect, NULL, 0);
    if (FAILED(hr)) return failed("Cutout Texture::LockRect", hr);
    fill_cutout_texture(&locked_rect);

    begin_stage("10 Cutout Texture::UnlockRect");
    hr = IDirect3DTexture8_UnlockRect(g_cutout_texture, 0);
    if (FAILED(hr)) return failed("Cutout Texture::UnlockRect", hr);

    begin_stage("11 CreateTexture blend");
    hr = IDirect3DDevice8_CreateTexture(g_device,
            TEST_TEXTURE_WIDTH, TEST_TEXTURE_HEIGHT, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_blend_texture);
    if (FAILED(hr)) return failed("CreateTexture(blend)", hr);

    ZeroMemory(&locked_rect, sizeof(locked_rect));
    begin_stage("12 Blend Texture::LockRect");
    hr = IDirect3DTexture8_LockRect(g_blend_texture, 0,
            &locked_rect, NULL, 0);
    if (FAILED(hr)) return failed("Blend Texture::LockRect", hr);
    fill_blend_texture(&locked_rect);

    begin_stage("13 Blend Texture::UnlockRect");
    hr = IDirect3DTexture8_UnlockRect(g_blend_texture, 0);
    if (FAILED(hr)) return failed("Blend Texture::UnlockRect", hr);

    return D3D_OK;
}

static HRESULT create_vertex_buffer(void)
{
    BYTE *destination;
    HRESULT hr;

    begin_stage("14 CreateVertexBuffer");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer", hr);

    destination = NULL;
    begin_stage("15 VertexBuffer::Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));

    begin_stage("16 VertexBuffer::Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    HRESULT hr;

    begin_stage("17 SetRenderState LIGHTING");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(LIGHTING=FALSE)", hr);

    begin_stage("18 SetRenderState CULLMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("SetRenderState(CULLMODE=NONE)", hr);

    begin_stage("19 SetRenderState SHADEMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_SHADEMODE,
            D3DSHADE_GOURAUD);
    if (FAILED(hr)) return failed("SetRenderState(SHADEMODE=GOURAUD)", hr);

    begin_stage("20 SetRenderState ZENABLE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, TRUE);
    if (FAILED(hr)) return failed("SetRenderState(ZENABLE=TRUE)", hr);

    begin_stage("21 SetRenderState ZFUNC");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC,
            D3DCMP_LESSEQUAL);
    if (FAILED(hr)) return failed("SetRenderState(ZFUNC=LESSEQUAL)", hr);

    begin_stage("22 TextureStage MAGFILTER POINT");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MAGFILTER, D3DTEXF_POINT);
    if (FAILED(hr)) return failed("TextureStage MAGFILTER", hr);

    begin_stage("23 TextureStage MINFILTER POINT");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MINFILTER, D3DTEXF_POINT);
    if (FAILED(hr)) return failed("TextureStage MINFILTER", hr);

    begin_stage("24 TextureStage MIPFILTER NONE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MIPFILTER, D3DTEXF_NONE);
    if (FAILED(hr)) return failed("TextureStage MIPFILTER", hr);

    begin_stage("25 TextureStage ADDRESSU CLAMP");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    if (FAILED(hr)) return failed("TextureStage ADDRESSU", hr);

    begin_stage("26 TextureStage ADDRESSV CLAMP");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    if (FAILED(hr)) return failed("TextureStage ADDRESSV", hr);

    begin_stage("27 Disable texture stage 1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("Disable texture stage 1", hr);

    begin_stage("28 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);

    begin_stage("29 SetVertexShader FVF TEX1");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(FVF TEX1)", hr);

    return D3D_OK;
}

static HRESULT set_untextured_opaque_state(const char *stage_prefix)
{
    HRESULT hr;

    begin_stage(stage_prefix);
    hr = IDirect3DDevice8_SetTexture(g_device, 0, NULL);
    if (FAILED(hr)) return failed("SetTexture(NULL)", hr);

    begin_stage("TextureStage COLOROP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage COLOROP SELECTARG1", hr);

    begin_stage("TextureStage COLORARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage COLORARG1 DIFFUSE", hr);

    begin_stage("SetRenderState ALPHATEST FALSE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHATESTENABLE, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(ALPHATEST=FALSE)", hr);

    begin_stage("SetRenderState ALPHABLEND FALSE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHABLENDENABLE, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(ALPHABLEND=FALSE)", hr);

    begin_stage("SetRenderState ZWRITE TRUE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ZWRITEENABLE, TRUE);
    if (FAILED(hr)) return failed("SetRenderState(ZWRITE=TRUE)", hr);

    return D3D_OK;
}

static HRESULT set_textured_color_state(IDirect3DTexture8 *texture,
        const char *stage)
{
    HRESULT hr;

    begin_stage(stage);
    hr = IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)texture);
    if (FAILED(hr)) return failed("SetTexture(textured pass)", hr);

    begin_stage("TextureStage COLOROP MODULATE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_MODULATE);
    if (FAILED(hr)) return failed("TextureStage COLOROP MODULATE", hr);

    begin_stage("TextureStage COLORARG1 TEXTURE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_TEXTURE);
    if (FAILED(hr)) return failed("TextureStage COLORARG1 TEXTURE", hr);

    begin_stage("TextureStage COLORARG2 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage COLORARG2 DIFFUSE", hr);

    begin_stage("TextureStage ALPHAOP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage ALPHAOP SELECTARG1", hr);

    begin_stage("TextureStage ALPHAARG1 TEXTURE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    if (FAILED(hr)) return failed("TextureStage ALPHAARG1 TEXTURE", hr);

    return D3D_OK;
}

static HRESULT render_alpha_test(HWND hwnd)
{
    HRESULT hr;

    begin_stage("30 Clear target and Z");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear(target + Z)", hr);

    begin_stage("31 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    hr = set_untextured_opaque_state("32 SetTexture NULL background");
    if (FAILED(hr)) goto end_scene_after_failure;

    begin_stage("38 DrawPrimitive background");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 2);
    if (FAILED(hr))
    {
        failed("DrawPrimitive(background)", hr);
        goto end_scene_after_failure;
    }

    hr = set_textured_color_state(g_cutout_texture,
            "39 SetTexture cutout");
    if (FAILED(hr)) goto end_scene_after_failure;

    begin_stage("45 SetRenderState ALPHATEST TRUE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHATESTENABLE, TRUE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHATEST=TRUE)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("46 SetRenderState ALPHAREF 127");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ALPHAREF, 127);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHAREF=127)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("47 SetRenderState ALPHAFUNC GREATER");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHAFUNC=GREATER)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("48 SetRenderState ALPHABLEND FALSE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHABLENDENABLE, FALSE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHABLEND=FALSE cutout)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("49 SetRenderState ZWRITE TRUE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ZWRITEENABLE, TRUE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ZWRITE=TRUE cutout)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("50 DrawPrimitive cutout");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 6, 2);
    if (FAILED(hr))
    {
        failed("DrawPrimitive(cutout)", hr);
        goto end_scene_after_failure;
    }

    hr = set_untextured_opaque_state("51 SetTexture NULL cutout probe");
    if (FAILED(hr)) goto end_scene_after_failure;

    begin_stage("57 DrawPrimitive cutout depth probe");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 12, 2);
    if (FAILED(hr))
    {
        failed("DrawPrimitive(cutout depth probe)", hr);
        goto end_scene_after_failure;
    }

    hr = set_textured_color_state(g_blend_texture,
            "58 SetTexture blend gradient");
    if (FAILED(hr)) goto end_scene_after_failure;

    begin_stage("64 SetRenderState ALPHATEST FALSE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHATESTENABLE, FALSE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHATEST=FALSE blend)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("65 SetRenderState ALPHABLEND TRUE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ALPHABLENDENABLE, TRUE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ALPHABLEND=TRUE)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("66 SetRenderState SRCBLEND SRCALPHA");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    if (FAILED(hr))
    {
        failed("SetRenderState(SRCBLEND=SRCALPHA)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("67 SetRenderState DESTBLEND INVSRCALPHA");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    if (FAILED(hr))
    {
        failed("SetRenderState(DESTBLEND=INVSRCALPHA)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("68 SetRenderState ZWRITE FALSE");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_ZWRITEENABLE, FALSE);
    if (FAILED(hr))
    {
        failed("SetRenderState(ZWRITE=FALSE blend)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("69 DrawPrimitive alpha blend");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 18, 2);
    if (FAILED(hr))
    {
        failed("DrawPrimitive(alpha blend)", hr);
        goto end_scene_after_failure;
    }

    hr = set_untextured_opaque_state("70 SetTexture NULL ZWRITE probe");
    if (FAILED(hr)) goto end_scene_after_failure;

    begin_stage("76 DrawPrimitive ZWRITE probe stripe");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 24, 2);
    if (FAILED(hr))
    {
        failed("DrawPrimitive(ZWRITE probe stripe)", hr);
        goto end_scene_after_failure;
    }

    begin_stage("77 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);

    begin_stage("78 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);

    set_result_title(hwnd,
            "Present S_OK - left cutout, right blend, probes visible", hr);
    return hr;

end_scene_after_failure:
    IDirect3DDevice8_EndScene(g_device);
    return hr;
}

static HRESULT initialise_and_render(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_textures();
    if (FAILED(hr)) return hr;
    hr = create_vertex_buffer();
    if (FAILED(hr)) return hr;
    hr = configure_fixed_pipeline();
    if (FAILED(hr)) return hr;
    return render_alpha_test(hwnd);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
        LPARAM lparam)
{
    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_DESTROY:
            release_d3d8();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int run_test(HINSTANCE instance, int show_command)
{
    WNDCLASSA window_class;
    RECT window_rect;
    HWND hwnd;
    MSG message;
    HRESULT hr;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = g_window_class;
    if (!RegisterClassA(&window_class))
    {
        trace_text("RegisterClass failed");
        return 1;
    }

    SetRect(&window_rect, 0, 0, TEST_CLIENT_WIDTH, TEST_CLIENT_HEIGHT);
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA(g_window_class, "D3D8 alpha: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            NULL, NULL, instance, NULL);
    if (!hwnd)
    {
        trace_text("CreateWindow failed");
        return 2;
    }

    g_window = hwnd;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    hr = initialise_and_render(hwnd);
    if (FAILED(hr))
    {
        set_result_title(hwnd, g_failed_stage, hr);
        MessageBoxA(hwnd,
                "The D3D8 alpha test failed. Check the window title, guest "
                "debug output, and v86gl logs for the HRESULT and last call.",
                "D3D8 alpha test", MB_OK | MB_ICONERROR);
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return FAILED(hr) ? 3 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    int result = run_test(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    ExitProcess((UINT)result);
}
