// Direct3D 8 multi-frame textured cube test for WineD3D -> v86 GL.
//
// Run d3d8_texture_test.exe and d3d8_transform_depth_test.exe first.  This
// integration test combines their proven paths: a managed checkerboard,
// XYZ | DIFFUSE | TEX1 vertices, a 16-bit index buffer, World/View/Projection
// transforms, automatic D24S8 depth, and repeated Draw/Present calls.  The
// cube rotates until its window is closed.
//
// Build for Windows XP as documented in ../winproxy/README.md.  The command
// uses a 32-bit MinGW compiler and avoids the MinGW C runtime and libm.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH   640
#define TEST_CLIENT_HEIGHT  480
#define TEST_TEXTURE_WIDTH   64
#define TEST_TEXTURE_HEIGHT  64
#define TEST_CHECKER_SIZE     8
#define TEST_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    DWORD color;
    FLOAT u;
    FLOAT v;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8TexturedCubeTest";

/* Four vertices per face are required because each face has independent UVs. */
static const TestVertex g_vertices[] =
{
    /* Near face, z = -1. */
    {-1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
    { 1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0.0f},
    { 1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    {-1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},

    /* Far face, z = +1. */
    { 1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 255), 0.0f, 0.0f},
    {-1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 255), 1.0f, 0.0f},
    {-1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 255), 1.0f, 1.0f},
    { 1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 255), 0.0f, 1.0f},

    /* Left face, x = -1. */
    {-1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 192), 0.0f, 0.0f},
    {-1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 192, 192), 1.0f, 0.0f},
    {-1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(255, 192, 192), 1.0f, 1.0f},
    {-1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(255, 192, 192), 0.0f, 1.0f},

    /* Right face, x = +1. */
    { 1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(192, 255, 192), 0.0f, 0.0f},
    { 1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(192, 255, 192), 1.0f, 0.0f},
    { 1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(192, 255, 192), 1.0f, 1.0f},
    { 1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(192, 255, 192), 0.0f, 1.0f},

    /* Top face, y = +1. */
    {-1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 255, 192), 0.0f, 0.0f},
    { 1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 255, 192), 1.0f, 0.0f},
    { 1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 192), 1.0f, 1.0f},
    {-1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 255, 192), 0.0f, 1.0f},

    /* Bottom face, y = -1. */
    {-1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(192, 192, 255), 0.0f, 0.0f},
    { 1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(192, 192, 255), 1.0f, 0.0f},
    { 1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(192, 192, 255), 1.0f, 1.0f},
    {-1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(192, 192, 255), 0.0f, 1.0f},
};

/* Submit the far face last so correct visibility still depends on Z testing. */
static const WORD g_indices[] =
{
     0,  1,  2,   0,  2,  3,  /* near */
     8,  9, 10,   8, 10, 11,  /* left */
    12, 13, 14,  12, 14, 15,  /* right */
    16, 17, 18,  16, 18, 19,  /* top */
    20, 21, 22,  20, 22, 23,  /* bottom */
     4,  5,  6,   4,  6,  7,  /* far, submitted last */
};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DIndexBuffer8 *g_index_buffer;
static IDirect3DTexture8 *g_texture;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static DWORD g_failed_frame;
static FLOAT g_cos_y = 1.0f;
static FLOAT g_sin_y = 0.0f;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-textured-cube] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];

    wsprintfA(line, "[d3d8-textured-cube] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    char title[224];

    g_failed_stage = stage;
    g_failed_frame = 0;
    trace_text(stage);
    wsprintfA(title, "D3D8 textured cube: calling %s", stage);
    if (g_window)
    {
        SetWindowTextA(g_window, title);
        RedrawWindow(g_window, NULL, NULL,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    }
}

static void begin_frame_stage(DWORD frame, const char *stage)
{
    char title[224];

    g_failed_stage = stage;
    g_failed_frame = frame;
    wsprintfA(title, "D3D8 textured cube: frame %lu calling %s",
            (unsigned long)frame, stage);
    SetWindowTextA(g_window, title);
}

static HRESULT failed(const char *stage, HRESULT hr)
{
    g_failed_stage = stage;
    trace_hresult(stage, hr);
    return hr;
}

static HRESULT frame_failed(DWORD frame, const char *stage, HRESULT hr)
{
    char line[224];

    g_failed_frame = frame;
    g_failed_stage = stage;
    wsprintfA(line, "frame %lu %s", (unsigned long)frame, stage);
    trace_hresult(line, hr);
    return hr;
}

static void set_failure_title(HWND hwnd, HRESULT hr)
{
    char title[224];

    if (g_failed_frame)
    {
        wsprintfA(title, "D3D8 textured cube: frame %lu %s (0x%08lX)",
                (unsigned long)g_failed_frame, g_failed_stage,
                (unsigned long)hr);
    }
    else
    {
        wsprintfA(title, "D3D8 textured cube: %s (0x%08lX)",
                g_failed_stage, (unsigned long)hr);
    }
    SetWindowTextA(hwnd, title);
}

static void set_success_title(HWND hwnd, DWORD frame, HRESULT hr)
{
    char title[224];

    wsprintfA(title,
            "D3D8 textured cube: frame %lu Present S_OK - rotating "
            "checker cube (0x%08lX)",
            (unsigned long)frame, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void release_d3d8(void)
{
    if (g_texture)
    {
        IDirect3DTexture8_Release(g_texture);
        g_texture = NULL;
    }

    if (g_index_buffer)
    {
        IDirect3DIndexBuffer8_Release(g_index_buffer);
        g_index_buffer = NULL;
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

static void make_identity(D3DMATRIX *matrix)
{
    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = 1.0f;
    matrix->_22 = 1.0f;
    matrix->_33 = 1.0f;
    matrix->_44 = 1.0f;
}

static void make_world_matrix(D3DMATRIX *matrix)
{
    const FLOAT cos_x = 0.9396926f;
    const FLOAT sin_x = 0.3420201f;

    /* Row-vector composition: rotate around Y, then tilt 20 degrees on X. */
    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = g_cos_y;
    matrix->_12 = g_sin_y * sin_x;
    matrix->_13 = -g_sin_y * cos_x;
    matrix->_22 = cos_x;
    matrix->_23 = sin_x;
    matrix->_31 = g_sin_y;
    matrix->_32 = -g_cos_y * sin_x;
    matrix->_33 = g_cos_y * cos_x;
    matrix->_44 = 1.0f;
}

static void advance_rotation(DWORD frame)
{
    const FLOAT cos_step = 0.9993908f; /* cos(2 degrees) */
    const FLOAT sin_step = 0.0348995f; /* sin(2 degrees) */
    FLOAT next_cos;
    FLOAT next_sin;

    /* Reset each full revolution to avoid accumulating floating-point drift. */
    if (!(frame % 180))
    {
        g_cos_y = 1.0f;
        g_sin_y = 0.0f;
        return;
    }

    next_cos = g_cos_y * cos_step - g_sin_y * sin_step;
    next_sin = g_sin_y * cos_step + g_cos_y * sin_step;
    g_cos_y = next_cos;
    g_sin_y = next_sin;
}

static void make_view_matrix(D3DMATRIX *matrix)
{
    make_identity(matrix);
    matrix->_43 = 5.0f;
}

static void make_projection_matrix(D3DMATRIX *matrix)
{
    const FLOAT z_scale = 100.0f / 99.0f;

    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = 0.75f;
    matrix->_22 = 1.0f;
    matrix->_33 = z_scale;
    matrix->_34 = 1.0f;
    matrix->_43 = -z_scale;
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

static void fill_checkerboard(const D3DLOCKED_RECT *locked_rect)
{
    UINT x;
    UINT y;

    for (y = 0; y < TEST_TEXTURE_HEIGHT; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked_rect->pBits
                + y * locked_rect->Pitch);

        for (x = 0; x < TEST_TEXTURE_WIDTH; ++x)
        {
            BOOL alternate = ((x / TEST_CHECKER_SIZE)
                    ^ (y / TEST_CHECKER_SIZE)) & 1;
            row[x] = alternate
                    ? D3DCOLOR_ARGB(255, 255, 224, 32)
                    : D3DCOLOR_ARGB(255, 32, 96, 255);
        }
    }
}

static HRESULT create_texture(void)
{
    D3DLOCKED_RECT locked_rect;
    HRESULT hr;

    begin_stage("08 CreateTexture");
    hr = IDirect3DDevice8_CreateTexture(g_device,
            TEST_TEXTURE_WIDTH, TEST_TEXTURE_HEIGHT, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture);
    if (FAILED(hr)) return failed("CreateTexture", hr);

    ZeroMemory(&locked_rect, sizeof(locked_rect));
    begin_stage("09 Texture::LockRect");
    hr = IDirect3DTexture8_LockRect(g_texture, 0, &locked_rect, NULL, 0);
    if (FAILED(hr)) return failed("Texture::LockRect", hr);
    fill_checkerboard(&locked_rect);

    begin_stage("10 Texture::UnlockRect");
    hr = IDirect3DTexture8_UnlockRect(g_texture, 0);
    if (FAILED(hr)) return failed("Texture::UnlockRect", hr);

    return D3D_OK;
}

static HRESULT create_buffers(void)
{
    BYTE *destination;
    HRESULT hr;

    begin_stage("11 CreateVertexBuffer XYZ TEX1");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer(XYZ TEX1)", hr);

    destination = NULL;
    begin_stage("12 VertexBuffer::Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));

    begin_stage("13 VertexBuffer::Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    begin_stage("14 CreateIndexBuffer");
    hr = IDirect3DDevice8_CreateIndexBuffer(g_device, sizeof(g_indices),
            D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
            &g_index_buffer);
    if (FAILED(hr)) return failed("CreateIndexBuffer", hr);

    destination = NULL;
    begin_stage("15 IndexBuffer::Lock");
    hr = IDirect3DIndexBuffer8_Lock(g_index_buffer, 0,
            sizeof(g_indices), &destination, 0);
    if (FAILED(hr)) return failed("IndexBuffer::Lock", hr);
    CopyMemory(destination, g_indices, sizeof(g_indices));

    begin_stage("16 IndexBuffer::Unlock");
    hr = IDirect3DIndexBuffer8_Unlock(g_index_buffer);
    if (FAILED(hr)) return failed("IndexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    D3DMATRIX view;
    D3DMATRIX projection;
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

    begin_stage("21 SetRenderState ZWRITEENABLE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZWRITEENABLE, TRUE);
    if (FAILED(hr)) return failed("SetRenderState(ZWRITEENABLE=TRUE)", hr);

    begin_stage("22 SetRenderState ZFUNC");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC,
            D3DCMP_LESSEQUAL);
    if (FAILED(hr)) return failed("SetRenderState(ZFUNC=LESSEQUAL)", hr);

    begin_stage("23 SetTexture stage 0");
    hr = IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)g_texture);
    if (FAILED(hr)) return failed("SetTexture(stage 0)", hr);

    begin_stage("24 TextureStage COLOROP MODULATE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_MODULATE);
    if (FAILED(hr)) return failed("TextureStage COLOROP", hr);

    begin_stage("25 TextureStage COLORARG1 TEXTURE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_TEXTURE);
    if (FAILED(hr)) return failed("TextureStage COLORARG1", hr);

    begin_stage("26 TextureStage COLORARG2 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage COLORARG2", hr);

    begin_stage("27 TextureStage ALPHAOP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage ALPHAOP", hr);

    begin_stage("28 TextureStage ALPHAARG1 TEXTURE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    if (FAILED(hr)) return failed("TextureStage ALPHAARG1", hr);

    begin_stage("29 TextureStage MAGFILTER POINT");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MAGFILTER, D3DTEXF_POINT);
    if (FAILED(hr)) return failed("TextureStage MAGFILTER", hr);

    begin_stage("30 TextureStage MINFILTER POINT");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MINFILTER, D3DTEXF_POINT);
    if (FAILED(hr)) return failed("TextureStage MINFILTER", hr);

    begin_stage("31 TextureStage MIPFILTER NONE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MIPFILTER, D3DTEXF_NONE);
    if (FAILED(hr)) return failed("TextureStage MIPFILTER", hr);

    begin_stage("32 TextureStage ADDRESSU CLAMP");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    if (FAILED(hr)) return failed("TextureStage ADDRESSU", hr);

    begin_stage("33 TextureStage ADDRESSV CLAMP");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    if (FAILED(hr)) return failed("TextureStage ADDRESSV", hr);

    begin_stage("34 Disable texture stage 1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("Disable texture stage 1", hr);

    begin_stage("35 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);

    begin_stage("36 SetIndices");
    hr = IDirect3DDevice8_SetIndices(g_device, g_index_buffer, 0);
    if (FAILED(hr)) return failed("SetIndices", hr);

    begin_stage("37 SetVertexShader FVF XYZ TEX1");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(FVF XYZ TEX1)", hr);

    make_view_matrix(&view);
    begin_stage("38 SetTransform VIEW");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_VIEW, &view);
    if (FAILED(hr)) return failed("SetTransform(VIEW)", hr);

    make_projection_matrix(&projection);
    begin_stage("39 SetTransform PROJECTION");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_PROJECTION,
            &projection);
    if (FAILED(hr)) return failed("SetTransform(PROJECTION)", hr);

    return D3D_OK;
}

static HRESULT initialise_test(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_texture();
    if (FAILED(hr)) return hr;
    hr = create_buffers();
    if (FAILED(hr)) return hr;
    return configure_fixed_pipeline();
}

static HRESULT render_frame(HWND hwnd, DWORD frame)
{
    D3DMATRIX world;
    HRESULT hr;

    make_world_matrix(&world);
    begin_frame_stage(frame, "SetTransform WORLD");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_WORLD, &world);
    if (FAILED(hr)) return frame_failed(frame, "SetTransform(WORLD)", hr);

    begin_frame_stage(frame, "Clear target and Z");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return frame_failed(frame, "Clear(target + Z)", hr);

    begin_frame_stage(frame, "BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return frame_failed(frame, "BeginScene", hr);

    begin_frame_stage(frame, "DrawIndexedPrimitive cube");
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 24, 0, 12);
    if (FAILED(hr))
    {
        IDirect3DDevice8_EndScene(g_device);
        return frame_failed(frame, "DrawIndexedPrimitive(cube)", hr);
    }

    begin_frame_stage(frame, "EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return frame_failed(frame, "EndScene", hr);

    begin_frame_stage(frame, "Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return frame_failed(frame, "Present", hr);

    set_success_title(hwnd, frame, hr);
    if (frame == 1 || !(frame % 60))
    {
        char line[96];
        wsprintfA(line, "frame %lu Present S_OK", (unsigned long)frame);
        trace_text(line);
    }

    advance_rotation(frame);
    return hr;
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
    DWORD frame;
    BOOL rendering;
    BOOL done;

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
    hwnd = CreateWindowA(g_window_class, "D3D8 textured cube: starting",
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

    hr = initialise_test(hwnd);
    rendering = SUCCEEDED(hr);
    if (!rendering)
    {
        set_failure_title(hwnd, hr);
        MessageBoxA(hwnd,
                "The D3D8 textured cube test failed during setup. Check "
                "the window title, guest debug output, and v86gl logs for "
                "the HRESULT and last call.",
                "D3D8 textured cube test", MB_OK | MB_ICONERROR);
    }

    frame = 1;
    done = FALSE;
    while (!done)
    {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                done = TRUE;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (done)
            break;

        if (rendering)
        {
            hr = render_frame(hwnd, frame);
            if (FAILED(hr))
            {
                rendering = FALSE;
                set_failure_title(hwnd, hr);
                MessageBoxA(hwnd,
                        "The D3D8 textured cube test failed while rendering. "
                        "The title identifies the frame, call, and HRESULT.",
                        "D3D8 textured cube test", MB_OK | MB_ICONERROR);
            }
            else
            {
                ++frame;
                Sleep(16);
            }
        }
        else
        {
            WaitMessage();
        }
    }

    return FAILED(hr) ? 3 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    int result = run_test(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    ExitProcess((UINT)result);
}
