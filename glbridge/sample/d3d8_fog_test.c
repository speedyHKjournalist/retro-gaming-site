// Direct3D 8 linear vertex-fog and table-fog test for WineD3D -> v86 GL.
//
// The top row draws five orange quads with FOGVERTEXMODE=LINEAR.  The bottom
// row draws five green quads at the same eye-space depths with
// FOGTABLEMODE=LINEAR.  Quad geometry is pre-scaled against perspective so
// every rectangle has the same screen size; only its fog contribution changes.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
#define TEST_QUAD_COUNT      5
#define TEST_VERTICES_PER_ROW (TEST_QUAD_COUNT * 6)
#define TEST_VERTEX_COUNT   (TEST_VERTICES_PER_ROW * 2)
#define TEST_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)
#define TEST_FOG_COLOR D3DCOLOR_XRGB(64, 72, 104)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    DWORD color;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8FogTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static TestVertex g_vertices[TEST_VERTEX_COUNT];
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static FLOAT g_table_fog_start = 2.0f;
static FLOAT g_table_fog_end = 10.0f;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-fog] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];

    wsprintfA(line, "[d3d8-fog] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[240];

    wsprintfA(title, "D3D8 fog: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[224];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 fog: calling %s", stage);
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

static DWORD float_as_dword(FLOAT value)
{
    DWORD result;

    CopyMemory(&result, &value, sizeof(result));
    return result;
}

static void release_d3d8(void)
{
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

static FLOAT eye_x_from_ndc(FLOAT ndc_x, FLOAT eye_z)
{
    return ndc_x * eye_z / 0.75f;
}

static FLOAT eye_y_from_ndc(FLOAT ndc_y, FLOAT eye_z)
{
    return ndc_y * eye_z;
}

static void fill_quad(TestVertex *vertices, FLOAT ndc_x0, FLOAT ndc_y0,
        FLOAT ndc_x1, FLOAT ndc_y1, FLOAT eye_z, DWORD color)
{
    FLOAT x0 = eye_x_from_ndc(ndc_x0, eye_z);
    FLOAT x1 = eye_x_from_ndc(ndc_x1, eye_z);
    FLOAT y0 = eye_y_from_ndc(ndc_y0, eye_z);
    FLOAT y1 = eye_y_from_ndc(ndc_y1, eye_z);

    vertices[0].x = x0;
    vertices[0].y = y0;
    vertices[0].z = eye_z;
    vertices[0].color = color;
    vertices[1].x = x1;
    vertices[1].y = y0;
    vertices[1].z = eye_z;
    vertices[1].color = color;
    vertices[2].x = x1;
    vertices[2].y = y1;
    vertices[2].z = eye_z;
    vertices[2].color = color;
    vertices[3] = vertices[0];
    vertices[4] = vertices[2];
    vertices[5].x = x0;
    vertices[5].y = y1;
    vertices[5].z = eye_z;
    vertices[5].color = color;
}

static void build_vertices(void)
{
    static const FLOAT centers[TEST_QUAD_COUNT] =
    {
        -0.80f, -0.40f, 0.0f, 0.40f, 0.80f
    };
    static const FLOAT depths[TEST_QUAD_COUNT] =
    {
        2.0f, 4.0f, 6.0f, 8.0f, 10.0f
    };
    UINT i;

    for (i = 0; i < TEST_QUAD_COUNT; ++i)
    {
        fill_quad(&g_vertices[i * 6],
                centers[i] - 0.14f, 0.20f,
                centers[i] + 0.14f, 0.65f,
                depths[i], D3DCOLOR_XRGB(255, 112, 32));
        fill_quad(&g_vertices[TEST_VERTICES_PER_ROW + i * 6],
                centers[i] - 0.14f, -0.65f,
                centers[i] + 0.14f, -0.20f,
                depths[i], D3DCOLOR_XRGB(32, 240, 112));
    }
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DCAPS8 caps;
    D3DPRESENT_PARAMETERS present_parameters;
    HRESULT hr;
    char line[192];

    begin_stage("01 Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d)
    {
        g_failed_stage = "Direct3DCreate8 returned NULL";
        trace_text(g_failed_stage);
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

    ZeroMemory(&caps, sizeof(caps));
    begin_stage("04 GetDeviceCaps fog");
    hr = IDirect3D8_GetDeviceCaps(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return failed("GetDeviceCaps", hr);
    wsprintfA(line, "[d3d8-fog] RasterCaps=0x%08lX\r\n",
            (unsigned long)caps.RasterCaps);
    OutputDebugStringA(line);
    if (!(caps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX))
        return failed("GetDeviceCaps requires FOGVERTEX",
                D3DERR_NOTAVAILABLE);
    if (!(caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE))
        return failed("GetDeviceCaps requires FOGTABLE",
                D3DERR_NOTAVAILABLE);
    if (!(caps.RasterCaps & (D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_WFOG)))
        return failed("GetDeviceCaps requires ZFOG or WFOG",
                D3DERR_NOTAVAILABLE);
    if (caps.RasterCaps & D3DPRASTERCAPS_WFOG)
    {
        trace_text("table fog uses WFOG range 2..10");
    }
    else
    {
        /* Project eye z=2 and z=10 into the standard D3D depth range. */
        g_table_fog_start = 50.0f / 99.0f;
        g_table_fog_end = 90.0f / 99.0f;
        trace_text("table fog uses projected ZFOG range");
    }

    ZeroMemory(&present_parameters, sizeof(present_parameters));
    present_parameters.BackBufferWidth = TEST_CLIENT_WIDTH;
    present_parameters.BackBufferHeight = TEST_CLIENT_HEIGHT;
    present_parameters.BackBufferFormat = mode.Format;
    present_parameters.BackBufferCount = 1;
    present_parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.hDeviceWindow = hwnd;
    present_parameters.Windowed = TRUE;
    present_parameters.EnableAutoDepthStencil = FALSE;
    present_parameters.FullScreen_PresentationInterval =
            D3DPRESENT_INTERVAL_DEFAULT;

    begin_stage("05 CreateDevice no depth");
    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    if (FAILED(hr)) return failed("CreateDevice(no depth)", hr);

    return D3D_OK;
}

static HRESULT create_vertex_buffer(void)
{
    BYTE *destination;
    HRESULT hr;

    build_vertices();
    begin_stage("06 CreateVertexBuffer fog rows");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer", hr);

    destination = NULL;
    begin_stage("07 VertexBuffer Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));
    begin_stage("08 VertexBuffer Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    D3DMATRIX identity;
    D3DMATRIX projection;
    HRESULT hr;

    begin_stage("09 LIGHTING false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("LIGHTING=FALSE", hr);
    begin_stage("10 CULLMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("CULLMODE=NONE", hr);
    begin_stage("11 ZENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, FALSE);
    if (FAILED(hr)) return failed("ZENABLE=FALSE", hr);
    begin_stage("12 SHADEMODE GOURAUD");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_SHADEMODE,
            D3DSHADE_GOURAUD);
    if (FAILED(hr)) return failed("SHADEMODE=GOURAUD", hr);

    begin_stage("13 stage0 COLOR SELECT DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("stage0 COLOROP SELECTARG1", hr);
    begin_stage("14 stage0 COLORARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("stage0 COLORARG1 DIFFUSE", hr);
    begin_stage("15 stage0 ALPHA SELECT DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("stage0 ALPHAOP SELECTARG1", hr);
    begin_stage("16 stage0 ALPHAARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("stage0 ALPHAARG1 DIFFUSE", hr);
    begin_stage("17 stage1 DISABLE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("stage1 COLOROP DISABLE", hr);

    begin_stage("18 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);
    begin_stage("19 SetVertexShader XYZ DIFFUSE");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(XYZ DIFFUSE)", hr);

    make_identity(&identity);
    begin_stage("20 SetTransform WORLD identity");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_WORLD, &identity);
    if (FAILED(hr)) return failed("SetTransform(WORLD)", hr);
    begin_stage("21 SetTransform VIEW identity");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_VIEW, &identity);
    if (FAILED(hr)) return failed("SetTransform(VIEW)", hr);
    make_projection_matrix(&projection);
    begin_stage("22 SetTransform PROJECTION");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_PROJECTION,
            &projection);
    if (FAILED(hr)) return failed("SetTransform(PROJECTION)", hr);

    begin_stage("23 FOGCOLOR blue-gray");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGCOLOR,
            TEST_FOG_COLOR);
    if (FAILED(hr)) return failed("FOGCOLOR", hr);
    begin_stage("24 FOGSTART 2.0");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGSTART,
            float_as_dword(2.0f));
    if (FAILED(hr)) return failed("FOGSTART=2", hr);
    begin_stage("25 FOGEND 10.0");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGEND,
            float_as_dword(10.0f));
    if (FAILED(hr)) return failed("FOGEND=10", hr);
    begin_stage("26 RANGEFOGENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_RANGEFOGENABLE, FALSE);
    if (FAILED(hr)) return failed("RANGEFOGENABLE=FALSE", hr);
    begin_stage("27 FOGENABLE true");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGENABLE, TRUE);
    if (FAILED(hr)) return failed("FOGENABLE=TRUE", hr);

    return D3D_OK;
}

static HRESULT render_fog_rows(HWND hwnd)
{
    HRESULT hr;

    begin_stage("28 Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(3, 4, 8), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_stage("29 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    begin_stage("30 FOGTABLEMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGTABLEMODE,
            D3DFOG_NONE);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("31 FOGVERTEXMODE linear");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGVERTEXMODE,
            D3DFOG_LINEAR);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("32 Draw top vertex fog");
    hr = IDirect3DDevice8_DrawPrimitive(g_device, D3DPT_TRIANGLELIST,
            0, TEST_QUAD_COUNT * 2);
    if (FAILED(hr)) goto scene_failed;

    begin_stage("33 FOGVERTEXMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGVERTEXMODE,
            D3DFOG_NONE);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("34 table FOGSTART selected range");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGSTART,
            float_as_dword(g_table_fog_start));
    if (FAILED(hr)) goto scene_failed;
    begin_stage("35 table FOGEND selected range");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGEND,
            float_as_dword(g_table_fog_end));
    if (FAILED(hr)) goto scene_failed;
    begin_stage("36 FOGTABLEMODE linear");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGTABLEMODE,
            D3DFOG_LINEAR);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("37 Draw bottom table fog");
    hr = IDirect3DDevice8_DrawPrimitive(g_device, D3DPT_TRIANGLELIST,
            TEST_VERTICES_PER_ROW, TEST_QUAD_COUNT * 2);
    if (FAILED(hr)) goto scene_failed;

    begin_stage("38 FOGENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_FOGENABLE, FALSE);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("39 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_stage("40 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);

    set_result_title(hwnd,
            "Present S_OK - top vertex orange, bottom table green, both fade",
            hr);
    return hr;

scene_failed:
    failed(g_failed_stage, hr);
    IDirect3DDevice8_EndScene(g_device);
    return hr;
}

static HRESULT init_and_render(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_vertex_buffer();
    if (FAILED(hr)) return hr;
    hr = configure_fixed_pipeline();
    if (FAILED(hr)) return hr;
    return render_fog_rows(hwnd);
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
    hwnd = CreateWindowA(g_window_class, "D3D8 fog: starting",
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
                "The D3D8 fog test failed. Check the window title, guest "
                "debug output, and v86gl logs for the HRESULT and last call.",
                "D3D8 fog test", MB_OK | MB_ICONERROR);
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
