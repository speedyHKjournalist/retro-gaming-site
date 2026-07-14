// Direct3D 8 two-stage fixed-function texture-operation regression test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  720
#define TEST_CLIENT_HEIGHT 600
#define TEST_PANEL_COUNT     9
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} TestVertex;

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DTexture8 *g_base_texture;
static IDirect3DTexture8 *g_operand_texture;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static TestVertex g_vertices[TEST_PANEL_COUNT * 6];
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static const char g_window_class[] = "V86GLD3D8TextureStageOpsTest";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-stage-ops] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];
    wsprintfA(line, "[d3d8-stage-ops] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    char title[240];
    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 texture-stage ops: calling %s", stage);
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

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[240];
    wsprintfA(title, "D3D8 texture-stage ops: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void release_d3d8(void)
{
    if (g_vertex_buffer)
    {
        IDirect3DVertexBuffer8_Release(g_vertex_buffer);
        g_vertex_buffer = NULL;
    }
    if (g_operand_texture)
    {
        IDirect3DTexture8_Release(g_operand_texture);
        g_operand_texture = NULL;
    }
    if (g_base_texture)
    {
        IDirect3DTexture8_Release(g_base_texture);
        g_base_texture = NULL;
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

static void fill_quad(TestVertex *vertices, FLOAT x0, FLOAT y0,
        FLOAT x1, FLOAT y1)
{
    const DWORD white = D3DCOLOR_XRGB(255, 255, 255);
    TestVertex values[6] =
    {
        {x0, y0, 0.5f, 1.0f, white, 0.0f, 0.0f},
        {x1, y0, 0.5f, 1.0f, white, 1.0f, 0.0f},
        {x1, y1, 0.5f, 1.0f, white, 1.0f, 1.0f},
        {x0, y0, 0.5f, 1.0f, white, 0.0f, 0.0f},
        {x1, y1, 0.5f, 1.0f, white, 1.0f, 1.0f},
        {x0, y1, 0.5f, 1.0f, white, 0.0f, 1.0f},
    };
    CopyMemory(vertices, values, sizeof(values));
}

static void build_vertices(void)
{
    UINT row, column;
    for (row = 0; row < 3; ++row)
    {
        for (column = 0; column < 3; ++column)
        {
            UINT index = row * 3 + column;
            FLOAT x0 = 30.0f + column * 230.0f;
            FLOAT y0 = 25.0f + row * 190.0f;
            fill_quad(&g_vertices[index * 6],
                    x0, y0, x0 + 200.0f, y0 + 160.0f);
        }
    }
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DCAPS8 caps;
    D3DPRESENT_PARAMETERS pp;
    DWORD required_ops;
    HRESULT hr;
    char line[192];

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

    ZeroMemory(&caps, sizeof(caps));
    begin_stage("04 GetDeviceCaps texture ops");
    hr = IDirect3D8_GetDeviceCaps(g_d3d, 0, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return failed("GetDeviceCaps", hr);
    wsprintfA(line, "[d3d8-stage-ops] stages=%lu textures=%lu ops=0x%08lX\r\n",
            (unsigned long)caps.MaxTextureBlendStages,
            (unsigned long)caps.MaxSimultaneousTextures,
            (unsigned long)caps.TextureOpCaps);
    OutputDebugStringA(line);
    required_ops = D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_MODULATE2X
            | D3DTEXOPCAPS_ADD
            | D3DTEXOPCAPS_ADDSIGNED
            | D3DTEXOPCAPS_SUBTRACT
            | D3DTEXOPCAPS_BLENDTEXTUREALPHA
            | D3DTEXOPCAPS_BLENDCURRENTALPHA;
    if (caps.MaxTextureBlendStages < 2
            || caps.MaxSimultaneousTextures < 2
            || (caps.TextureOpCaps & required_ops) != required_ops)
        return failed("GetDeviceCaps missing required stage operation",
                D3DERR_NOTAVAILABLE);

    begin_stage("05 CheckDeviceFormat A8R8G8B8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, 0, D3DDEVTYPE_HAL,
            mode.Format, 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
    if (FAILED(hr)) return failed("CheckDeviceFormat(A8R8G8B8)", hr);

    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_CLIENT_WIDTH;
    pp.BackBufferHeight = TEST_CLIENT_HEIGHT;
    pp.BackBufferFormat = mode.Format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    begin_stage("06 CreateDevice");
    hr = IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    if (FAILED(hr)) return failed("CreateDevice", hr);
    return D3D_OK;
}

static HRESULT create_solid_texture(IDirect3DTexture8 **texture,
        DWORD color, const char *stage)
{
    D3DLOCKED_RECT locked;
    HRESULT hr;
    UINT x, y;
    begin_stage(stage);
    hr = IDirect3DDevice8_CreateTexture(g_device, 4, 4, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture);
    if (FAILED(hr)) return failed(stage, hr);
    ZeroMemory(&locked, sizeof(locked));
    hr = IDirect3DTexture8_LockRect(*texture, 0, &locked, NULL, 0);
    if (FAILED(hr)) return failed(stage, hr);
    for (y = 0; y < 4; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked.pBits + y * locked.Pitch);
        for (x = 0; x < 4; ++x) row[x] = color;
    }
    hr = IDirect3DTexture8_UnlockRect(*texture, 0);
    if (FAILED(hr)) return failed(stage, hr);
    return D3D_OK;
}

static HRESULT create_resources(void)
{
    BYTE *destination;
    HRESULT hr;
    hr = create_solid_texture(&g_base_texture,
            D3DCOLOR_ARGB(64, 64, 128, 192),
            "07 Create base RGBA(64,128,192,64)");
    if (FAILED(hr)) return hr;
    hr = create_solid_texture(&g_operand_texture,
            D3DCOLOR_ARGB(192, 128, 64, 32),
            "08 Create operand RGBA(128,64,32,192)");
    if (FAILED(hr)) return hr;
    build_vertices();
    begin_stage("09 CreateVertexBuffer nine panels");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer", hr);
    destination = NULL;
    begin_stage("10 VertexBuffer Lock/copy/unlock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);
    return D3D_OK;
}

static HRESULT configure_pipeline(void)
{
    HRESULT hr;
#define CHECK_CALL(label, call) do { begin_stage(label); hr = (call); \
    if (FAILED(hr)) return failed(label, hr); } while (0)
    CHECK_CALL("11 LIGHTING false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_LIGHTING, FALSE));
    CHECK_CALL("12 ZENABLE false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_ZENABLE, FALSE));
    CHECK_CALL("13 CULLMODE none", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_CULLMODE, D3DCULL_NONE));
    CHECK_CALL("14 SetTexture stage0 base", IDirect3DDevice8_SetTexture(
            g_device, 0, (IDirect3DBaseTexture8 *)g_base_texture));
    CHECK_CALL("15 SetTexture stage1 operand", IDirect3DDevice8_SetTexture(
            g_device, 1, (IDirect3DBaseTexture8 *)g_operand_texture));
    CHECK_CALL("16 stage0 COLOR SELECT TEXTURE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    CHECK_CALL("17 stage0 COLORARG1 TEXTURE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_TEXTURE));
    CHECK_CALL("18 stage0 ALPHA SELECT TEXTURE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    CHECK_CALL("19 stage0 ALPHAARG1 TEXTURE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
    CHECK_CALL("20 stage1 TEXCOORDINDEX 0",
            IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_TEXCOORDINDEX, 0));
    CHECK_CALL("21 stage1 ALPHA SELECT CURRENT",
            IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    CHECK_CALL("22 stage1 ALPHAARG1 CURRENT",
            IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_ALPHAARG1, D3DTA_CURRENT));
    CHECK_CALL("23 stage2 DISABLE",
            IDirect3DDevice8_SetTextureStageState(g_device, 2,
            D3DTSS_COLOROP, D3DTOP_DISABLE));
    CHECK_CALL("24 SetStreamSource", IDirect3DDevice8_SetStreamSource(
            g_device, 0, g_vertex_buffer, sizeof(TestVertex)));
    CHECK_CALL("25 SetVertexShader TEX1",
            IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF));
#undef CHECK_CALL
    return D3D_OK;
}

static HRESULT draw_op(UINT panel, DWORD op, DWORD arg1, DWORD arg2,
        const char *stage)
{
    HRESULT hr;
    begin_stage(stage);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, op);
    if (FAILED(hr)) return failed(stage, hr);
    if (op != D3DTOP_DISABLE)
    {
        hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
                D3DTSS_COLORARG1, arg1);
        if (FAILED(hr)) return failed(stage, hr);
        hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
                D3DTSS_COLORARG2, arg2);
        if (FAILED(hr)) return failed(stage, hr);
    }
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, panel * 6, 2);
    if (FAILED(hr)) return failed(stage, hr);
    return D3D_OK;
}

static HRESULT render(HWND hwnd)
{
    HRESULT hr;
    begin_stage("26 Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_stage("27 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    hr = draw_op(0, D3DTOP_DISABLE, 0, 0,
            "28 panel0 base reference");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(1, D3DTOP_MODULATE2X, D3DTA_CURRENT, D3DTA_TEXTURE,
            "29 panel1 MODULATE2X");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(2, D3DTOP_ADD, D3DTA_CURRENT, D3DTA_TEXTURE,
            "30 panel2 ADD");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(3, D3DTOP_ADDSIGNED, D3DTA_CURRENT, D3DTA_TEXTURE,
            "31 panel3 ADDSIGNED");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(4, D3DTOP_SUBTRACT, D3DTA_CURRENT, D3DTA_TEXTURE,
            "32 panel4 SUBTRACT current-texture");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(5, D3DTOP_BLENDTEXTUREALPHA,
            D3DTA_TEXTURE, D3DTA_CURRENT,
            "33 panel5 BLENDTEXTUREALPHA");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(6, D3DTOP_BLENDCURRENTALPHA,
            D3DTA_TEXTURE, D3DTA_CURRENT,
            "34 panel6 BLENDCURRENTALPHA");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(7, D3DTOP_SELECTARG1,
            D3DTA_TEXTURE | D3DTA_COMPLEMENT, D3DTA_CURRENT,
            "35 panel7 TEXTURE COMPLEMENT");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_op(8, D3DTOP_SELECTARG1,
            D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE, D3DTA_CURRENT,
            "36 panel8 TEXTURE ALPHAREPLICATE");
    if (FAILED(hr)) goto scene_failed;

    begin_stage("37 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_stage("38 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return failed("Present", hr);
    set_result_title(hwnd,
            "Present S_OK - 3x3 stage-op colour grid", hr);
    return hr;

scene_failed:
    IDirect3DDevice8_EndScene(g_device);
    return hr;
}

static HRESULT init_and_render(HWND hwnd)
{
    HRESULT hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_resources();
    if (FAILED(hr)) return hr;
    hr = configure_pipeline();
    if (FAILED(hr)) return hr;
    return render(hwnd);
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
        case WM_DESTROY:
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
            "D3D8 texture-stage ops: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 2;
    ShowWindow(g_window, show_command);
    UpdateWindow(g_window);
    hr = init_and_render(g_window);
    if (FAILED(hr))
    {
        set_result_title(g_window, g_failed_stage, hr);
        MessageBoxA(g_window,
                "The D3D8 texture-stage-ops test failed. Check the title "
                "and v86gl logs for the last call.",
                "D3D8 texture-stage-ops test", MB_OK | MB_ICONERROR);
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
    ExitProcess((UINT)run_test(GetModuleHandleA(NULL), SW_SHOWDEFAULT));
}
