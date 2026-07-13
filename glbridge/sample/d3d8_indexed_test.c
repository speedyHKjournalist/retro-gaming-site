// Direct3D 8 indexed fixed-pipeline test for WineD3D -> v86 OpenGL bridge.
//
// This test keeps the already proven XYZRHW + DIFFUSE path and adds only a
// 16-bit index buffer, SetIndices, and DrawIndexedPrimitive.  Keeping the
// vertices pre-transformed makes an index-buffer failure independent of the
// transform and depth paths exercised by d3d8_transform_depth_test.c.
//
// Build for Windows XP as documented in ../winproxy/README.md.  The command
// uses a 32-bit MinGW compiler and avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    FLOAT rhw;
    DWORD color;
} TestVertex;

static const char g_window_class[] = "V86GLD3D8IndexedTest";
static const TestVertex g_vertices[] =
{
    {120.0f,  80.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255,   0,   0)},
    {520.0f,  80.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(  0, 255,   0)},
    {520.0f, 400.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(  0,   0, 255)},
    {120.0f, 400.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255, 255,   0)},
};
static const WORD g_indices[] = {0, 1, 2, 0, 2, 3};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DIndexBuffer8 *g_index_buffer;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-indexed] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[192];

    wsprintfA(line, "[d3d8-indexed] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[192];

    wsprintfA(title, "D3D8 indexed: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[192];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 indexed: calling %s", stage);
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

    begin_stage("04 CreateDevice");
    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    trace_hresult("CreateDevice(windowed, software VP, no depth)", hr);
    if (FAILED(hr)) return failed("CreateDevice", hr);

    return D3D_OK;
}

static HRESULT create_buffers(void)
{
    BYTE *destination;
    HRESULT hr;

    begin_stage("05 CreateVertexBuffer");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    trace_hresult("CreateVertexBuffer(4 XYZRHW vertices)", hr);
    if (FAILED(hr)) return failed("CreateVertexBuffer", hr);

    destination = NULL;
    begin_stage("06 VertexBuffer::Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    trace_hresult("VertexBuffer::Lock", hr);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));

    begin_stage("07 VertexBuffer::Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    trace_hresult("VertexBuffer::Unlock", hr);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    begin_stage("08 CreateIndexBuffer");
    hr = IDirect3DDevice8_CreateIndexBuffer(g_device, sizeof(g_indices),
            D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
            &g_index_buffer);
    trace_hresult("CreateIndexBuffer(6 INDEX16 entries)", hr);
    if (FAILED(hr)) return failed("CreateIndexBuffer", hr);

    destination = NULL;
    begin_stage("09 IndexBuffer::Lock");
    hr = IDirect3DIndexBuffer8_Lock(g_index_buffer, 0,
            sizeof(g_indices), &destination, 0);
    trace_hresult("IndexBuffer::Lock", hr);
    if (FAILED(hr)) return failed("IndexBuffer::Lock", hr);
    CopyMemory(destination, g_indices, sizeof(g_indices));

    begin_stage("10 IndexBuffer::Unlock");
    hr = IDirect3DIndexBuffer8_Unlock(g_index_buffer);
    trace_hresult("IndexBuffer::Unlock", hr);
    if (FAILED(hr)) return failed("IndexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    HRESULT hr;

    begin_stage("11 SetRenderState LIGHTING");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(LIGHTING=FALSE)", hr);

    begin_stage("12 SetRenderState ZENABLE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, FALSE);
    if (FAILED(hr)) return failed("SetRenderState(ZENABLE=FALSE)", hr);

    begin_stage("13 SetRenderState CULLMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("SetRenderState(CULLMODE=NONE)", hr);

    begin_stage("14 SetRenderState SHADEMODE");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_SHADEMODE,
            D3DSHADE_GOURAUD);
    if (FAILED(hr)) return failed("SetRenderState(SHADEMODE=GOURAUD)", hr);

    begin_stage("15 TextureStage COLOROP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage COLOROP", hr);

    begin_stage("16 TextureStage COLORARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage COLORARG1", hr);

    begin_stage("17 TextureStage ALPHAOP SELECTARG1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    if (FAILED(hr)) return failed("TextureStage ALPHAOP", hr);

    begin_stage("18 TextureStage ALPHAARG1 DIFFUSE");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    if (FAILED(hr)) return failed("TextureStage ALPHAARG1", hr);

    begin_stage("19 Disable texture stage 1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("Disable texture stage 1", hr);

    begin_stage("20 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);

    begin_stage("21 SetIndices");
    hr = IDirect3DDevice8_SetIndices(g_device, g_index_buffer, 0);
    if (FAILED(hr)) return failed("SetIndices", hr);

    begin_stage("22 SetVertexShader FVF");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(FVF)", hr);

    return D3D_OK;
}

static HRESULT render_indexed_quad(HWND hwnd)
{
    HRESULT hr;

    begin_stage("23 Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);

    begin_stage("24 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    begin_stage("25 DrawIndexedPrimitive");
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 4, 0, 2);
    trace_hresult("DrawIndexedPrimitive(4 vertices, 2 triangles)", hr);
    if (FAILED(hr))
    {
        IDirect3DDevice8_EndScene(g_device);
        return failed("DrawIndexedPrimitive", hr);
    }

    begin_stage("26 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);

    begin_stage("27 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);

    set_result_title(hwnd, "Present S_OK - expected indexed RGB quad", hr);
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
    return render_indexed_quad(hwnd);
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
    hwnd = CreateWindowA(g_window_class, "D3D8 indexed: starting",
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
                "The D3D8 indexed test failed. Check the window title, "
                "guest debug output, and v86gl logs for the HRESULT and "
                "last call.",
                "D3D8 indexed test", MB_OK | MB_ICONERROR);
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
