// Direct3D 8 transform + depth fixed-pipeline test for WineD3D -> v86 GL.
//
// Run d3d8_indexed_test.exe first.  This follow-up keeps the same 16-bit
// indexed draw path, then adds non-pretransformed XYZ vertices, World/View/
// Projection matrices, an automatic D24S8 depth-stencil surface, and Z testing.
// The result is one static, rotated, colour-interpolated cube.
//
// Build for Windows XP as documented in ../winproxy/README.md.  The command
// uses a 32-bit MinGW compiler and avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
#define TEST_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    DWORD color;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8TransformDepthTest";
static const TestVertex g_vertices[] =
{
    {-1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB(255,  32,  32)},
    {-1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB(255, 255,  32)},
    { 1.0f,  1.0f, -1.0f, D3DCOLOR_XRGB( 32, 255,  32)},
    { 1.0f, -1.0f, -1.0f, D3DCOLOR_XRGB( 32,  32, 255)},
    {-1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(255,  32, 255)},
    {-1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB(255, 255, 255)},
    { 1.0f,  1.0f,  1.0f, D3DCOLOR_XRGB( 32, 255, 255)},
    { 1.0f, -1.0f,  1.0f, D3DCOLOR_XRGB(255, 128,  32)},
};

/*
 * The near face is submitted first and the far face last.  Correct depth
 * testing therefore matters: submission order alone cannot produce the
 * expected visible surfaces after the cube is rotated.
 */
static const WORD g_indices[] =
{
    0, 1, 2,  0, 2, 3,       /* near (z = -1) */
    0, 4, 5,  0, 5, 1,       /* left */
    3, 2, 6,  3, 6, 7,       /* right */
    1, 5, 6,  1, 6, 2,       /* top */
    0, 3, 7,  0, 7, 4,       /* bottom */
    4, 7, 6,  4, 6, 5,       /* far (z = +1), submitted last */
};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DIndexBuffer8 *g_index_buffer;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-transform-depth] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[208];

    wsprintfA(line, "[d3d8-transform-depth] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[208];

    wsprintfA(title, "D3D8 transform+depth: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[208];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 transform+depth: calling %s", stage);
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
    /* 30-degree rotation around Y followed by a 20-degree X tilt. */
    const FLOAT cos_y = 0.8660254f;
    const FLOAT sin_y = 0.5f;
    const FLOAT cos_x = 0.9396926f;
    const FLOAT sin_x = 0.3420201f;

    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = cos_y;
    matrix->_12 = sin_y * sin_x;
    matrix->_13 = -sin_y * cos_x;
    matrix->_22 = cos_x;
    matrix->_23 = sin_x;
    matrix->_31 = sin_y;
    matrix->_32 = -cos_y * sin_x;
    matrix->_33 = cos_y * cos_x;
    matrix->_44 = 1.0f;
}

static void make_view_matrix(D3DMATRIX *matrix)
{
    /* Left-handed camera at (0, 0, -5), looking at the origin. */
    make_identity(matrix);
    matrix->_43 = 5.0f;
}

static void make_projection_matrix(D3DMATRIX *matrix)
{
    /* Left-handed 90-degree vertical FOV, aspect 4:3, z range 1..100. */
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
    trace_hresult("GetAdapterDisplayMode", hr);
    if (FAILED(hr)) return failed("GetAdapterDisplayMode", hr);

    begin_stage("03 CheckDeviceType");
    hr = IDirect3D8_CheckDeviceType(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, mode.Format, TRUE);
    trace_hresult("CheckDeviceType(windowed HAL)", hr);
    if (FAILED(hr)) return failed("CheckDeviceType", hr);

    /*
     * The bridge intentionally exposes one conservative WGL pixel format:
     * 24 depth bits plus 8 stencil bits.  Query that exact format instead of
     * D16; WineD3D correctly rejects D16 when no 16-bit drawable format is
     * advertised even though ordinary depth testing is available.
     */
    begin_stage("04 CheckDeviceFormat D24S8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, D3DUSAGE_DEPTHSTENCIL,
            D3DRTYPE_SURFACE, D3DFMT_D24S8);
    trace_hresult("CheckDeviceFormat(D24S8 depth-stencil)", hr);
    if (FAILED(hr)) return failed("CheckDeviceFormat(D24S8)", hr);

    begin_stage("05 CheckDepthStencilMatch D24S8");
    hr = IDirect3D8_CheckDepthStencilMatch(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, mode.Format, D3DFMT_D24S8);
    trace_hresult("CheckDepthStencilMatch(backbuffer, D24S8)", hr);
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

    begin_stage("06 CreateDevice auto D24S8");
    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    trace_hresult("CreateDevice(windowed, software VP, auto D24S8)", hr);
    if (FAILED(hr)) return failed("CreateDevice(auto D24S8)", hr);

    return D3D_OK;
}

static HRESULT create_buffers(void)
{
    BYTE *destination;
    HRESULT hr;

    begin_stage("07 CreateVertexBuffer XYZ");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    trace_hresult("CreateVertexBuffer(8 XYZ vertices)", hr);
    if (FAILED(hr)) return failed("CreateVertexBuffer(XYZ)", hr);

    destination = NULL;
    begin_stage("08 VertexBuffer::Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));

    begin_stage("09 VertexBuffer::Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    begin_stage("10 CreateIndexBuffer");
    hr = IDirect3DDevice8_CreateIndexBuffer(g_device, sizeof(g_indices),
            D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
            &g_index_buffer);
    trace_hresult("CreateIndexBuffer(36 INDEX16 entries)", hr);
    if (FAILED(hr)) return failed("CreateIndexBuffer", hr);

    destination = NULL;
    begin_stage("11 IndexBuffer::Lock");
    hr = IDirect3DIndexBuffer8_Lock(g_index_buffer, 0,
            sizeof(g_indices), &destination, 0);
    if (FAILED(hr)) return failed("IndexBuffer::Lock", hr);
    CopyMemory(destination, g_indices, sizeof(g_indices));

    begin_stage("12 IndexBuffer::Unlock");
    hr = IDirect3DIndexBuffer8_Unlock(g_index_buffer);
    if (FAILED(hr)) return failed("IndexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    D3DMATRIX world;
    D3DMATRIX view;
    D3DMATRIX projection;
    HRESULT hr;

    begin_stage("13 SetRenderState LIGHTING");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(LIGHTING=FALSE)", hr);

    begin_stage("14 SetRenderState CULLMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("SetRenderState(CULLMODE=NONE)", hr);

    begin_stage("15 SetRenderState SHADEMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_SHADEMODE,
            D3DSHADE_GOURAUD);
    if (FAILED(hr)) return failed("SetRenderState(SHADEMODE=GOURAUD)", hr);

    begin_stage("16 SetRenderState ZENABLE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, TRUE);
    if (FAILED(hr)) return failed("SetRenderState(ZENABLE=TRUE)", hr);

    begin_stage("17 SetRenderState ZWRITEENABLE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZWRITEENABLE, TRUE);
    if (FAILED(hr)) return failed("SetRenderState(ZWRITEENABLE=TRUE)", hr);

    begin_stage("18 SetRenderState ZFUNC");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC,
            D3DCMP_LESSEQUAL);
    if (FAILED(hr)) return failed("SetRenderState(ZFUNC=LESSEQUAL)", hr);

    begin_stage("19 TextureStage COLOROP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage COLOROP", hr);

    begin_stage("20 TextureStage COLORARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage COLORARG1", hr);

    begin_stage("21 TextureStage ALPHAOP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage ALPHAOP", hr);

    begin_stage("22 TextureStage ALPHAARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage ALPHAARG1", hr);

    begin_stage("23 Disable texture stage 1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("Disable texture stage 1", hr);

    begin_stage("24 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);

    begin_stage("25 SetIndices");
    hr = IDirect3DDevice8_SetIndices(g_device, g_index_buffer, 0);
    if (FAILED(hr)) return failed("SetIndices", hr);

    begin_stage("26 SetVertexShader FVF XYZ");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(FVF XYZ)", hr);

    make_world_matrix(&world);
    begin_stage("27 SetTransform WORLD");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_WORLD, &world);
    if (FAILED(hr)) return failed("SetTransform(WORLD)", hr);

    make_view_matrix(&view);
    begin_stage("28 SetTransform VIEW");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_VIEW, &view);
    if (FAILED(hr)) return failed("SetTransform(VIEW)", hr);

    make_projection_matrix(&projection);
    begin_stage("29 SetTransform PROJECTION");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_PROJECTION,
            &projection);
    if (FAILED(hr)) return failed("SetTransform(PROJECTION)", hr);

    return D3D_OK;
}

static HRESULT render_cube(HWND hwnd)
{
    HRESULT hr;

    begin_stage("30 Clear target and Z");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    trace_hresult("Clear(black, depth=1)", hr);
    if (FAILED(hr)) return failed("Clear(target + Z)", hr);

    begin_stage("31 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    begin_stage("32 DrawIndexedPrimitive cube");
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 8, 0, 12);
    trace_hresult("DrawIndexedPrimitive(8 vertices, 12 triangles)", hr);
    if (FAILED(hr))
    {
        IDirect3DDevice8_EndScene(g_device);
        return failed("DrawIndexedPrimitive(cube)", hr);
    }

    begin_stage("33 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);

    begin_stage("34 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);

    set_result_title(hwnd,
            "Present S_OK - expected depth-tested colour cube", hr);
    return hr;
}

static HRESULT init_and_render(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_buffers();
    if (FAILED(hr)) return hr;
    hr = configure_fixed_pipeline();
    if (FAILED(hr)) return hr;
    return render_cube(hwnd);
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
    hwnd = CreateWindowA(g_window_class, "D3D8 transform+depth: starting",
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

    hr = init_and_render(hwnd);
    if (FAILED(hr))
    {
        set_result_title(hwnd, g_failed_stage, hr);
        MessageBoxA(hwnd,
                "The D3D8 transform/depth test failed. Check the window "
                "title, guest debug output, and v86gl logs for the HRESULT "
                "and last call.",
                "D3D8 transform/depth test", MB_OK | MB_ICONERROR);
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
