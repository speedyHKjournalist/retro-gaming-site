// Direct3D 8 fixed-pipeline lighting test for WineD3D -> v86 GL.
//
// Two indexed cubes share one XYZ | NORMAL vertex buffer and one material.
// The left cube has only global ambient light.  The right cube enables one
// warm directional light.  This makes missing material, ambient, light-enable,
// or normal transformation semantics visually distinct without involving
// textures, fog, alpha blending, or programmable shaders.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
#define TEST_FVF (D3DFVF_XYZ | D3DFVF_NORMAL)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    FLOAT nx;
    FLOAT ny;
    FLOAT nz;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8LightingTest";

/* Four vertices per face give every cube face one unambiguous flat normal. */
static const TestVertex g_vertices[] =
{
    /* Near face, z = -1. */
    {-1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f},
    {-1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f},

    /* Far face, z = +1. */
    { 1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f},
    {-1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f},

    /* Left face, x = -1. */
    {-1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f},
    {-1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f},
    {-1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f},
    {-1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f},

    /* Right face, x = +1. */
    { 1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f},
    { 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f},
    { 1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f},
    { 1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f},

    /* Top face, y = +1. */
    {-1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f},
    { 1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f},
    { 1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f},
    {-1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f},

    /* Bottom face, y = -1. */
    {-1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f},
    { 1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f},
    { 1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f},
    {-1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f},
};

static const WORD g_indices[] =
{
     0,  1,  2,   0,  2,  3,  /* near */
     8,  9, 10,   8, 10, 11,  /* left */
    12, 13, 14,  12, 14, 15,  /* right */
    16, 17, 18,  16, 18, 19,  /* top */
    20, 21, 22,  20, 22, 23,  /* bottom */
     4,  5,  6,   4,  6,  7,  /* far */
};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DIndexBuffer8 *g_index_buffer;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-lighting] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];

    wsprintfA(line, "[d3d8-lighting] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[224];

    wsprintfA(title, "D3D8 lighting: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[224];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 lighting: calling %s", stage);
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

static void make_world_matrix(D3DMATRIX *matrix, FLOAT translate_x)
{
    /* Row-vector composition: rotate Y by 30 degrees, then tilt X by 20. */
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
    matrix->_41 = translate_x;
    matrix->_44 = 1.0f;
}

static void make_view_matrix(D3DMATRIX *matrix)
{
    /* Left-handed camera at (0, 0, -6), looking at the origin. */
    make_identity(matrix);
    matrix->_43 = 6.0f;
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
    D3DCAPS8 caps;
    D3DPRESENT_PARAMETERS present_parameters;
    HRESULT hr;
    char line[224];

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
    begin_stage("04 GetDeviceCaps lighting");
    hr = IDirect3D8_GetDeviceCaps(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return failed("GetDeviceCaps", hr);
    wsprintfA(line, "[d3d8-lighting] caps: max_lights=%lu vtx=0x%08lX\r\n",
            (unsigned long)caps.MaxActiveLights,
            (unsigned long)caps.VertexProcessingCaps);
    OutputDebugStringA(line);
    if (!caps.MaxActiveLights
            || !(caps.VertexProcessingCaps & D3DVTXPCAPS_DIRECTIONALLIGHTS))
        return failed("GetDeviceCaps requires directional lighting",
                D3DERR_NOTAVAILABLE);

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

static HRESULT create_buffers(void)
{
    BYTE *destination;
    HRESULT hr;

    begin_stage("08 CreateVertexBuffer XYZ NORMAL");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer(XYZ NORMAL)", hr);

    destination = NULL;
    begin_stage("09 VertexBuffer Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));
    begin_stage("10 VertexBuffer Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    begin_stage("11 CreateIndexBuffer");
    hr = IDirect3DDevice8_CreateIndexBuffer(g_device, sizeof(g_indices),
            D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
            &g_index_buffer);
    if (FAILED(hr)) return failed("CreateIndexBuffer", hr);

    destination = NULL;
    begin_stage("12 IndexBuffer Lock");
    hr = IDirect3DIndexBuffer8_Lock(g_index_buffer, 0,
            sizeof(g_indices), &destination, 0);
    if (FAILED(hr)) return failed("IndexBuffer::Lock", hr);
    CopyMemory(destination, g_indices, sizeof(g_indices));
    begin_stage("13 IndexBuffer Unlock");
    hr = IDirect3DIndexBuffer8_Unlock(g_index_buffer);
    if (FAILED(hr)) return failed("IndexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    D3DMATRIX view;
    D3DMATRIX projection;
    D3DMATERIAL8 material;
    D3DLIGHT8 light;
    HRESULT hr;

    begin_stage("14 LIGHTING true");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, TRUE);
    if (FAILED(hr)) return failed("LIGHTING=TRUE", hr);
    begin_stage("15 COLORVERTEX false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_COLORVERTEX, FALSE);
    if (FAILED(hr)) return failed("COLORVERTEX=FALSE", hr);
    begin_stage("16 NORMALIZENORMALS true");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_NORMALIZENORMALS, TRUE);
    if (FAILED(hr)) return failed("NORMALIZENORMALS=TRUE", hr);
    begin_stage("17 AMBIENT blue-gray");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_AMBIENT,
            D3DCOLOR_XRGB(96, 96, 96));
    if (FAILED(hr)) return failed("AMBIENT", hr);
    begin_stage("18 SPECULARENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device,
            D3DRS_SPECULARENABLE, FALSE);
    if (FAILED(hr)) return failed("SPECULARENABLE=FALSE", hr);
    begin_stage("19 CULLMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("CULLMODE=NONE", hr);
    begin_stage("20 ZENABLE true");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, TRUE);
    if (FAILED(hr)) return failed("ZENABLE=TRUE", hr);
    begin_stage("21 ZWRITEENABLE true");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZWRITEENABLE, TRUE);
    if (FAILED(hr)) return failed("ZWRITEENABLE=TRUE", hr);
    begin_stage("22 ZFUNC LESSEQUAL");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC,
            D3DCMP_LESSEQUAL);
    if (FAILED(hr)) return failed("ZFUNC=LESSEQUAL", hr);

    begin_stage("23 stage0 SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("stage0 COLOROP SELECTARG1", hr);
    begin_stage("24 stage0 COLORARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("stage0 COLORARG1 DIFFUSE", hr);
    begin_stage("25 stage0 ALPHA SELECT DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("stage0 ALPHAOP SELECTARG1", hr);
    begin_stage("26 stage0 ALPHAARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("stage0 ALPHAARG1 DIFFUSE", hr);
    begin_stage("27 stage1 DISABLE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("stage1 COLOROP DISABLE", hr);

    begin_stage("28 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);
    begin_stage("29 SetIndices");
    hr = IDirect3DDevice8_SetIndices(g_device, g_index_buffer, 0);
    if (FAILED(hr)) return failed("SetIndices", hr);
    begin_stage("30 SetVertexShader XYZ NORMAL");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(XYZ NORMAL)", hr);

    make_view_matrix(&view);
    begin_stage("31 SetTransform VIEW");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_VIEW, &view);
    if (FAILED(hr)) return failed("SetTransform(VIEW)", hr);
    make_projection_matrix(&projection);
    begin_stage("32 SetTransform PROJECTION");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_PROJECTION,
            &projection);
    if (FAILED(hr)) return failed("SetTransform(PROJECTION)", hr);

    ZeroMemory(&material, sizeof(material));
    material.Ambient.r = 0.15f;
    material.Ambient.g = 0.35f;
    material.Ambient.b = 0.80f;
    material.Ambient.a = 1.0f;
    material.Diffuse.r = 0.95f;
    material.Diffuse.g = 0.55f;
    material.Diffuse.b = 0.12f;
    material.Diffuse.a = 1.0f;
    begin_stage("33 SetMaterial blue ambient orange diffuse");
    hr = IDirect3DDevice8_SetMaterial(g_device, &material);
    if (FAILED(hr)) return failed("SetMaterial", hr);

    ZeroMemory(&light, sizeof(light));
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = 1.0f;
    light.Diffuse.g = 0.95f;
    light.Diffuse.b = 0.80f;
    light.Diffuse.a = 1.0f;
    /* Direction in which rays travel: from camera-upper-right into scene. */
    light.Direction.x = 0.35f;
    light.Direction.y = -0.55f;
    light.Direction.z = 0.75f;
    begin_stage("34 SetLight 0 directional");
    hr = IDirect3DDevice8_SetLight(g_device, 0, &light);
    if (FAILED(hr)) return failed("SetLight(0, directional)", hr);
    begin_stage("35 LightEnable 0 false");
    hr = IDirect3DDevice8_LightEnable(g_device, 0, FALSE);
    if (FAILED(hr)) return failed("LightEnable(0, FALSE)", hr);

    return D3D_OK;
}

static HRESULT render_lighting_comparison(HWND hwnd)
{
    D3DMATRIX world;
    HRESULT hr;

    begin_stage("36 Clear target and Z");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(4, 6, 12), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear(target + Z)", hr);
    begin_stage("37 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    make_world_matrix(&world, -1.7f);
    begin_stage("38 SetTransform WORLD left");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_WORLD, &world);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("39 Draw left ambient only");
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 24, 0, 12);
    if (FAILED(hr)) goto scene_failed;

    begin_stage("40 LightEnable 0 true");
    hr = IDirect3DDevice8_LightEnable(g_device, 0, TRUE);
    if (FAILED(hr)) goto scene_failed;
    make_world_matrix(&world, 1.7f);
    begin_stage("41 SetTransform WORLD right");
    hr = IDirect3DDevice8_SetTransform(g_device, D3DTS_WORLD, &world);
    if (FAILED(hr)) goto scene_failed;
    begin_stage("42 Draw right directional");
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 24, 0, 12);
    if (FAILED(hr)) goto scene_failed;

    begin_stage("43 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_stage("44 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);

    set_result_title(hwnd,
            "Present S_OK - left ambient blue, right directional gold", hr);
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
    hr = create_buffers();
    if (FAILED(hr)) return hr;
    hr = configure_fixed_pipeline();
    if (FAILED(hr)) return hr;
    return render_lighting_comparison(hwnd);
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
    hwnd = CreateWindowA(g_window_class, "D3D8 lighting: starting",
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
                "The D3D8 lighting test failed. Check the window title, "
                "guest debug output, and v86gl logs for the HRESULT and "
                "last call.",
                "D3D8 lighting test", MB_OK | MB_ICONERROR);
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
