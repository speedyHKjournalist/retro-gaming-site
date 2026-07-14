// Direct3D 8 common texture-format, pitch, and sub-rectangle update test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH   720
#define TEST_CLIENT_HEIGHT  520
#define TEST_TEXTURE_SIZE    64
#define TEST_CASE_COUNT       8
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} TestVertex;

typedef struct TextureCase
{
    D3DFORMAT format;
    const char *name;
    IDirect3DTexture8 *texture;
} TextureCase;

static TextureCase g_cases[TEST_CASE_COUNT] =
{
    {D3DFMT_X8R8G8B8, "X8R8G8B8", NULL},
    {D3DFMT_A8R8G8B8, "A8R8G8B8", NULL},
    {D3DFMT_R5G6B5, "R5G6B5", NULL},
    {D3DFMT_A1R5G5B5, "A1R5G5B5", NULL},
    {D3DFMT_A4R4G4B4, "A4R4G4B4", NULL},
    {D3DFMT_L8, "L8", NULL},
    {D3DFMT_A8, "A8", NULL},
    {D3DFMT_A8R8G8B8, "A8R8G8B8 subrect", NULL},
};

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static TestVertex g_vertices[TEST_CASE_COUNT * 6];
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static const char g_window_class[] = "V86GLD3D8TextureFormatsTest";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-formats] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];
    wsprintfA(line, "[d3d8-formats] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    char title[240];
    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 texture formats: calling %s", stage);
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
    wsprintfA(title, "D3D8 texture formats: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void release_d3d8(void)
{
    UINT i;
    if (g_vertex_buffer)
    {
        IDirect3DVertexBuffer8_Release(g_vertex_buffer);
        g_vertex_buffer = NULL;
    }
    for (i = 0; i < TEST_CASE_COUNT; ++i)
    {
        if (g_cases[i].texture)
        {
            IDirect3DTexture8_Release(g_cases[i].texture);
            g_cases[i].texture = NULL;
        }
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
    for (row = 0; row < 2; ++row)
    {
        for (column = 0; column < 4; ++column)
        {
            UINT index = row * 4 + column;
            FLOAT x0 = 20.0f + column * 175.0f;
            FLOAT y0 = 25.0f + row * 250.0f;
            fill_quad(&g_vertices[index * 6],
                    x0, y0, x0 + 155.0f, y0 + 220.0f);
        }
    }
}

static WORD pack_r5g6b5(UINT r, UINT g, UINT b)
{
    return (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static WORD pack_a1r5g5b5(UINT a, UINT r, UINT g, UINT b)
{
    return (WORD)(((a >= 128 ? 1 : 0) << 15)
            | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static WORD pack_a4r4g4b4(UINT a, UINT r, UINT g, UINT b)
{
    return (WORD)(((a >> 4) << 12) | ((r >> 4) << 8)
            | ((g >> 4) << 4) | (b >> 4));
}

static void fill_full_texture(UINT case_index,
        const D3DLOCKED_RECT *locked)
{
    UINT x, y;
    for (y = 0; y < TEST_TEXTURE_SIZE; ++y)
    {
        BYTE *row = (BYTE *)locked->pBits + y * locked->Pitch;
        for (x = 0; x < TEST_TEXTURE_SIZE; ++x)
        {
            UINT r = x * 255 / (TEST_TEXTURE_SIZE - 1);
            UINT g = y * 255 / (TEST_TEXTURE_SIZE - 1);
            UINT b = (((x / 8) ^ (y / 8)) & 1) ? 32 : 240;
            UINT a = x * 255 / (TEST_TEXTURE_SIZE - 1);
            UINT luminance = (r * 3 + g * 6 + b) / 10;

            switch (g_cases[case_index].format)
            {
                case D3DFMT_X8R8G8B8:
                    ((DWORD *)row)[x] = D3DCOLOR_XRGB(r, g, b);
                    break;
                case D3DFMT_A8R8G8B8:
                    if (case_index == 7)
                        ((DWORD *)row)[x] = D3DCOLOR_ARGB(255, 112, 24, 160);
                    else
                        ((DWORD *)row)[x] = D3DCOLOR_ARGB(a, r, g, b);
                    break;
                case D3DFMT_R5G6B5:
                    ((WORD *)row)[x] = pack_r5g6b5(r, g, b);
                    break;
                case D3DFMT_A1R5G5B5:
                    ((WORD *)row)[x] = pack_a1r5g5b5(a, r, g, b);
                    break;
                case D3DFMT_A4R4G4B4:
                    ((WORD *)row)[x] = pack_a4r4g4b4(a, r, g, b);
                    break;
                case D3DFMT_L8:
                    row[x] = (BYTE)luminance;
                    break;
                case D3DFMT_A8:
                    row[x] = (BYTE)a;
                    break;
                default:
                    break;
            }
        }
    }
}

static void fill_subrect(const D3DLOCKED_RECT *locked)
{
    UINT x, y;
    for (y = 0; y < 32; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked->pBits + y * locked->Pitch);
        for (x = 0; x < 32; ++x)
        {
            row[x] = (((x / 4) ^ (y / 4)) & 1)
                    ? D3DCOLOR_ARGB(255, 32, 240, 96)
                    : D3DCOLOR_ARGB(255, 255, 224, 32);
        }
    }
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;
    UINT i;
    char stage[128];

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

    for (i = 0; i < 7; ++i)
    {
        wsprintfA(stage, "04 CheckDeviceFormat %s", g_cases[i].name);
        begin_stage(stage);
        hr = IDirect3D8_CheckDeviceFormat(g_d3d, 0, D3DDEVTYPE_HAL,
                mode.Format, 0, D3DRTYPE_TEXTURE, g_cases[i].format);
        if (FAILED(hr)) return failed(stage, hr);
    }

    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_CLIENT_WIDTH;
    pp.BackBufferHeight = TEST_CLIENT_HEIGHT;
    pp.BackBufferFormat = mode.Format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    begin_stage("05 CreateDevice");
    hr = IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    if (FAILED(hr)) return failed("CreateDevice", hr);
    return D3D_OK;
}

static HRESULT create_resources(void)
{
    D3DLOCKED_RECT locked;
    RECT update_rect;
    BYTE *destination;
    HRESULT hr;
    UINT i;
    char stage[128];

    for (i = 0; i < TEST_CASE_COUNT; ++i)
    {
        wsprintfA(stage, "06 Create/Lock/fill %s", g_cases[i].name);
        begin_stage(stage);
        hr = IDirect3DDevice8_CreateTexture(g_device,
                TEST_TEXTURE_SIZE, TEST_TEXTURE_SIZE, 1, 0,
                g_cases[i].format, D3DPOOL_MANAGED, &g_cases[i].texture);
        if (FAILED(hr)) return failed(stage, hr);
        ZeroMemory(&locked, sizeof(locked));
        hr = IDirect3DTexture8_LockRect(g_cases[i].texture, 0,
                &locked, NULL, 0);
        if (FAILED(hr)) return failed(stage, hr);
        fill_full_texture(i, &locked);
        hr = IDirect3DTexture8_UnlockRect(g_cases[i].texture, 0);
        if (FAILED(hr)) return failed(stage, hr);
    }

    SetRect(&update_rect, 16, 16, 48, 48);
    begin_stage("07 LockRect subrect 16,16..48,48");
    ZeroMemory(&locked, sizeof(locked));
    hr = IDirect3DTexture8_LockRect(g_cases[7].texture, 0,
            &locked, &update_rect, 0);
    if (FAILED(hr)) return failed("LockRect(subrect)", hr);
    fill_subrect(&locked);
    hr = IDirect3DTexture8_UnlockRect(g_cases[7].texture, 0);
    if (FAILED(hr)) return failed("UnlockRect(subrect)", hr);

    build_vertices();
    begin_stage("08 CreateVertexBuffer eight panels");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer", hr);
    destination = NULL;
    begin_stage("09 VertexBuffer Lock/copy/unlock");
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
    CHECK_CALL("10 LIGHTING false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_LIGHTING, FALSE));
    CHECK_CALL("11 ZENABLE false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_ZENABLE, FALSE));
    CHECK_CALL("12 CULLMODE none", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_CULLMODE, D3DCULL_NONE));
    CHECK_CALL("13 ALPHABLEND true", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_ALPHABLENDENABLE, TRUE));
    CHECK_CALL("14 SRCBLEND SRCALPHA", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    CHECK_CALL("15 DESTBLEND INVSRCALPHA", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
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
    CHECK_CALL("20 stage0 filters POINT",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MINFILTER, D3DTEXF_POINT));
    CHECK_CALL("21 stage0 MAGFILTER POINT",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MAGFILTER, D3DTEXF_POINT));
    CHECK_CALL("22 stage0 MIPFILTER NONE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MIPFILTER, D3DTEXF_NONE));
    CHECK_CALL("23 stage0 CLAMP U",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP));
    CHECK_CALL("24 stage0 CLAMP V",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP));
    CHECK_CALL("25 stage1 DISABLE",
            IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE));
    CHECK_CALL("26 SetStreamSource", IDirect3DDevice8_SetStreamSource(
            g_device, 0, g_vertex_buffer, sizeof(TestVertex)));
    CHECK_CALL("27 SetVertexShader TEX1",
            IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF));
#undef CHECK_CALL
    return D3D_OK;
}

static HRESULT render(HWND hwnd)
{
    HRESULT hr;
    UINT i;
    char stage[128];
    begin_stage("28 Clear dark blue");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(8, 16, 48), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_stage("29 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    for (i = 0; i < TEST_CASE_COUNT; ++i)
    {
        wsprintfA(stage, "30 Draw panel %lu %s",
                (unsigned long)i, g_cases[i].name);
        begin_stage(stage);
        hr = IDirect3DDevice8_SetTexture(g_device, 0,
                (IDirect3DBaseTexture8 *)g_cases[i].texture);
        if (FAILED(hr)) goto scene_failed;
        if (i == 6)
        {
            hr = IDirect3DDevice8_SetRenderState(g_device,
                    D3DRS_ALPHABLENDENABLE, FALSE);
            if (FAILED(hr)) goto scene_failed;
            hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
                    D3DTSS_COLORARG1,
                    D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
            if (FAILED(hr)) goto scene_failed;
        }
        else
        {
            hr = IDirect3DDevice8_SetRenderState(g_device,
                    D3DRS_ALPHABLENDENABLE, TRUE);
            if (FAILED(hr)) goto scene_failed;
            hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
                    D3DTSS_COLORARG1, D3DTA_TEXTURE);
            if (FAILED(hr)) goto scene_failed;
        }
        hr = IDirect3DDevice8_DrawPrimitive(g_device,
                D3DPT_TRIANGLELIST, i * 6, 2);
        if (FAILED(hr)) goto scene_failed;
    }

    begin_stage("31 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_stage("32 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return failed("Present", hr);
    set_result_title(hwnd,
            "Present S_OK - 7 formats plus green/yellow subrect", hr);
    return hr;

scene_failed:
    failed(g_failed_stage, hr);
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
    g_window = CreateWindowA(g_window_class, "D3D8 texture formats: starting",
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
                "The D3D8 texture-formats test failed. Check the title and "
                "v86gl logs for the last call.",
                "D3D8 texture-formats test", MB_OK | MB_ICONERROR);
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
