// Direct3D 8 dynamic VB/IB DISCARD + NOOVERWRITE multi-frame test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH   640
#define TEST_CLIENT_HEIGHT  480
#define TEST_SLOT_COUNT       3
#define TEST_VERTICES_PER_SLOT 4
#define TEST_INDICES_PER_SLOT  6
#define TEST_TIMER_ID          1
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
} TestVertex;

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DIndexBuffer8 *g_index_buffer;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static const char g_window_class[] = "V86GLD3D8DynamicResourcesTest";
static DWORD g_frame;
static HRESULT g_last_result = D3D_OK;
static BOOL g_stopped;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-dynamic] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];
    wsprintfA(line, "[d3d8-dynamic] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    char title[224];
    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 dynamic resources: calling %s", stage);
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
    wsprintfA(title, "D3D8 dynamic: frame %lu calling %s",
            (unsigned long)frame, stage);
    SetWindowTextA(g_window, title);
}

static HRESULT failed(const char *stage, HRESULT hr)
{
    g_failed_stage = stage;
    trace_hresult(stage, hr);
    return hr;
}

static void set_failure_title(HRESULT hr)
{
    char title[240];
    wsprintfA(title, "D3D8 dynamic: frame %lu %s (0x%08lX)",
            (unsigned long)g_frame, g_failed_stage, (unsigned long)hr);
    SetWindowTextA(g_window, title);
}

static void set_success_title(HRESULT hr)
{
    char title[240];
    wsprintfA(title, "D3D8 dynamic: frame %lu Present S_OK - "
            "VB+IB DISCARD then 2x NOOVERWRITE (0x%08lX)",
            (unsigned long)g_frame, (unsigned long)hr);
    SetWindowTextA(g_window, title);
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

static DWORD triangle_wave(DWORD frame)
{
    DWORD phase = frame % 240;
    return phase <= 120 ? phase : 240 - phase;
}

static void fill_slot_vertices(UINT slot, DWORD frame, TestVertex *vertices)
{
    static const DWORD colors[TEST_SLOT_COUNT][4] =
    {
        {
            D3DCOLOR_XRGB(255, 32, 32), D3DCOLOR_XRGB(255, 224, 32),
            D3DCOLOR_XRGB(255, 32, 224), D3DCOLOR_XRGB(96, 0, 0)
        },
        {
            D3DCOLOR_XRGB(32, 255, 64), D3DCOLOR_XRGB(32, 255, 224),
            D3DCOLOR_XRGB(224, 255, 32), D3DCOLOR_XRGB(0, 96, 24)
        },
        {
            D3DCOLOR_XRGB(32, 96, 255), D3DCOLOR_XRGB(32, 240, 255),
            D3DCOLOR_XRGB(224, 32, 255), D3DCOLOR_XRGB(0, 24, 96)
        }
    };
    DWORD wave = triangle_wave(frame);
    FLOAT x0 = 20.0f + wave * 4.0f;
    FLOAT y0 = 35.0f + slot * 150.0f;
    if (slot == 1) x0 = 500.0f - wave * 4.0f;
    vertices[0].x = x0;
    vertices[0].y = y0;
    vertices[0].z = 0.5f;
    vertices[0].rhw = 1.0f;
    vertices[0].color = colors[slot][0];
    vertices[1].x = x0 + 120.0f;
    vertices[1].y = y0;
    vertices[1].z = 0.5f;
    vertices[1].rhw = 1.0f;
    vertices[1].color = colors[slot][1];
    vertices[2].x = x0 + 120.0f;
    vertices[2].y = y0 + 95.0f;
    vertices[2].z = 0.5f;
    vertices[2].rhw = 1.0f;
    vertices[2].color = colors[slot][2];
    vertices[3].x = x0;
    vertices[3].y = y0 + 95.0f;
    vertices[3].z = 0.5f;
    vertices[3].rhw = 1.0f;
    vertices[3].color = colors[slot][3];
}

static void fill_slot_indices(UINT slot, DWORD frame, WORD *indices)
{
    WORD base = (WORD)(slot * TEST_VERTICES_PER_SLOT);
    if ((frame / 30) & 1)
    {
        indices[0] = base;
        indices[1] = base + 1;
        indices[2] = base + 3;
        indices[3] = base + 1;
        indices[4] = base + 2;
        indices[5] = base + 3;
    }
    else
    {
        indices[0] = base;
        indices[1] = base + 1;
        indices[2] = base + 2;
        indices[3] = base;
        indices[4] = base + 2;
        indices[5] = base + 3;
    }
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;
    begin_stage("01 Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) return failed("Direct3DCreate8 returned NULL", E_FAIL);
    ZeroMemory(&mode, sizeof(mode));
    begin_stage("02 GetAdapterDisplayMode");
    hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
    if (FAILED(hr)) return failed("GetAdapterDisplayMode", hr);
    begin_stage("03 CheckDeviceType");
    hr = IDirect3D8_CheckDeviceType(g_d3d, 0, D3DDEVTYPE_HAL,
            mode.Format, mode.Format, TRUE);
    if (FAILED(hr)) return failed("CheckDeviceType", hr);
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_CLIENT_WIDTH;
    pp.BackBufferHeight = TEST_CLIENT_HEIGHT;
    pp.BackBufferFormat = mode.Format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    begin_stage("04 CreateDevice");
    hr = IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    if (FAILED(hr)) return failed("CreateDevice", hr);
    return D3D_OK;
}

static HRESULT create_resources(void)
{
    HRESULT hr;
    begin_stage("05 CreateVertexBuffer DYNAMIC");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device,
            TEST_SLOT_COUNT * TEST_VERTICES_PER_SLOT * sizeof(TestVertex),
            D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY
            | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer(DYNAMIC)", hr);
    begin_stage("06 CreateIndexBuffer DYNAMIC");
    hr = IDirect3DDevice8_CreateIndexBuffer(g_device,
            TEST_SLOT_COUNT * TEST_INDICES_PER_SLOT * sizeof(WORD),
            D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
            D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_index_buffer);
    if (FAILED(hr)) return failed("CreateIndexBuffer(DYNAMIC)", hr);
    return D3D_OK;
}

static HRESULT configure_pipeline(void)
{
    HRESULT hr;
#define CHECK_CALL(label, call) do { begin_stage(label); hr = (call); \
    if (FAILED(hr)) return failed(label, hr); } while (0)
    CHECK_CALL("07 LIGHTING false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_LIGHTING, FALSE));
    CHECK_CALL("08 ZENABLE false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_ZENABLE, FALSE));
    CHECK_CALL("09 CULLMODE none", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_CULLMODE, D3DCULL_NONE));
    CHECK_CALL("10 stage0 COLOR SELECT DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    CHECK_CALL("11 stage0 COLORARG1 DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    CHECK_CALL("12 stage0 ALPHA SELECT DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    CHECK_CALL("13 stage0 ALPHAARG1 DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
    CHECK_CALL("14 stage1 DISABLE",
            IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE));
    CHECK_CALL("15 SetStreamSource dynamic VB",
            IDirect3DDevice8_SetStreamSource(g_device, 0,
            g_vertex_buffer, sizeof(TestVertex)));
    CHECK_CALL("16 SetIndices dynamic IB",
            IDirect3DDevice8_SetIndices(g_device, g_index_buffer, 0));
    CHECK_CALL("17 SetVertexShader XYZRHW DIFFUSE",
            IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF));
#undef CHECK_CALL
    return D3D_OK;
}

static HRESULT update_and_draw_slot(UINT slot, DWORD frame)
{
    TestVertex vertices[TEST_VERTICES_PER_SLOT];
    WORD indices[TEST_INDICES_PER_SLOT];
    BYTE *vb_destination = NULL;
    BYTE *ib_destination = NULL;
    DWORD flags = slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD;
    UINT vb_offset = slot * TEST_VERTICES_PER_SLOT * sizeof(TestVertex);
    UINT ib_offset = slot * TEST_INDICES_PER_SLOT * sizeof(WORD);
    HRESULT hr;
    char stage[96];

    fill_slot_vertices(slot, frame, vertices);
    fill_slot_indices(slot, frame, indices);
    wsprintfA(stage, "slot %lu VB %s", (unsigned long)slot,
            slot ? "NOOVERWRITE" : "DISCARD");
    begin_frame_stage(frame, stage);
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, vb_offset,
            sizeof(vertices), &vb_destination, flags);
    if (FAILED(hr)) return failed(stage, hr);
    CopyMemory(vb_destination, vertices, sizeof(vertices));
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed(stage, hr);

    wsprintfA(stage, "slot %lu IB %s", (unsigned long)slot,
            slot ? "NOOVERWRITE" : "DISCARD");
    begin_frame_stage(frame, stage);
    hr = IDirect3DIndexBuffer8_Lock(g_index_buffer, ib_offset,
            sizeof(indices), &ib_destination, flags);
    if (FAILED(hr)) return failed(stage, hr);
    CopyMemory(ib_destination, indices, sizeof(indices));
    hr = IDirect3DIndexBuffer8_Unlock(g_index_buffer);
    if (FAILED(hr)) return failed(stage, hr);

    wsprintfA(stage, "DrawIndexedPrimitive slot %lu", (unsigned long)slot);
    begin_frame_stage(frame, stage);
    hr = IDirect3DDevice8_DrawIndexedPrimitive(g_device,
            D3DPT_TRIANGLELIST,
            slot * TEST_VERTICES_PER_SLOT, TEST_VERTICES_PER_SLOT,
            slot * TEST_INDICES_PER_SLOT, 2);
    if (FAILED(hr)) return failed(stage, hr);
    return D3D_OK;
}

static HRESULT render_frame(DWORD frame)
{
    HRESULT hr;
    UINT slot;
    begin_frame_stage(frame, "Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_frame_stage(frame, "BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);
    for (slot = 0; slot < TEST_SLOT_COUNT; ++slot)
    {
        hr = update_and_draw_slot(slot, frame);
        if (FAILED(hr))
        {
            IDirect3DDevice8_EndScene(g_device);
            return hr;
        }
    }
    begin_frame_stage(frame, "EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_frame_stage(frame, "Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return failed("Present", hr);
    set_success_title(hr);
    return hr;
}

static HRESULT initialize(HWND hwnd)
{
    HRESULT hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_resources();
    if (FAILED(hr)) return hr;
    return configure_pipeline();
}

static void stop_on_runtime_failure(HRESULT hr)
{
    if (g_stopped) return;
    g_stopped = TRUE;
    g_last_result = hr;
    KillTimer(g_window, TEST_TIMER_ID);
    set_failure_title(hr);
    MessageBoxA(g_window,
            "The D3D8 dynamic-resource test failed. The title identifies "
            "the frame, resource, lock mode, or draw call.",
            "D3D8 dynamic-resource test", MB_OK | MB_ICONERROR);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
        LPARAM lparam)
{
    switch (message)
    {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_TIMER:
            if (wparam == TEST_TIMER_ID && !g_stopped)
            {
                ++g_frame;
                g_last_result = render_frame(g_frame);
                if (FAILED(g_last_result))
                    stop_on_runtime_failure(g_last_result);
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, TEST_TIMER_ID);
            release_d3d8();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int run_test(HINSTANCE instance, int show_command)
{
    WNDCLASSA wc;
    RECT rect;
    MSG message;
    HRESULT hr;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = g_window_class;
    if (!RegisterClassA(&wc)) return 1;
    SetRect(&rect, 0, 0, TEST_CLIENT_WIDTH, TEST_CLIENT_HEIGHT);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_window_class,
            "D3D8 dynamic resources: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 2;
    ShowWindow(g_window, show_command);
    UpdateWindow(g_window);
    hr = initialize(g_window);
    g_last_result = hr;
    if (FAILED(hr))
    {
        set_failure_title(hr);
        MessageBoxA(g_window,
                "The D3D8 dynamic-resource setup failed. Check the title "
                "and v86gl logs for the last call.",
                "D3D8 dynamic-resource test", MB_OK | MB_ICONERROR);
        g_stopped = TRUE;
    }
    else
    {
        g_frame = 1;
        g_last_result = render_frame(g_frame);
        if (FAILED(g_last_result))
            stop_on_runtime_failure(g_last_result);
        else if (!SetTimer(g_window, TEST_TIMER_ID, 16, NULL))
            stop_on_runtime_failure(E_FAIL);
    }
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return FAILED(g_last_result) ? 3 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ExitProcess((UINT)run_test(GetModuleHandleA(NULL), SW_SHOWDEFAULT));
}
