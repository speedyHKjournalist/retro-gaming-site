// Direct3D 8 mipmap, min/mag filtering, and address-mode regression test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
#define TEST_TEXTURE_SIZE  128
#define TEST_MIP_LEVELS      8
#define TEST_PANEL_COUNT     6
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} TestVertex;

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DTexture8 *g_texture;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static const char g_window_class[] = "V86GLD3D8MipmapFilterTest";
static TestVertex g_vertices[TEST_PANEL_COUNT * 6];

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-mipmap] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[224];
    wsprintfA(line, "[d3d8-mipmap] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    char title[224];
    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 mipmap/filter: calling %s", stage);
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
    wsprintfA(title, "D3D8 mipmap/filter: %s (0x%08lX)",
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
    if (g_texture)
    {
        IDirect3DTexture8_Release(g_texture);
        g_texture = NULL;
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

static void fill_panel_vertices(TestVertex *vertices, FLOAT x0, FLOAT y0,
        FLOAT x1, FLOAT y1, FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1)
{
    const DWORD white = D3DCOLOR_XRGB(255, 255, 255);
    const TestVertex values[6] =
    {
        {x0, y0, 0.5f, 1.0f, white, u0, v0},
        {x1, y0, 0.5f, 1.0f, white, u1, v0},
        {x1, y1, 0.5f, 1.0f, white, u1, v1},
        {x0, y0, 0.5f, 1.0f, white, u0, v0},
        {x1, y1, 0.5f, 1.0f, white, u1, v1},
        {x0, y1, 0.5f, 1.0f, white, u0, v1},
    };
    CopyMemory(vertices, values, sizeof(values));
}

static void build_vertices(void)
{
    fill_panel_vertices(&g_vertices[0],
            20.0f, 30.0f, 200.0f, 210.0f, 0.0f, 0.0f, 16.0f, 16.0f);
    fill_panel_vertices(&g_vertices[6],
            230.0f, 30.0f, 410.0f, 210.0f, 0.0f, 0.0f, 16.0f, 16.0f);
    fill_panel_vertices(&g_vertices[12],
            440.0f, 30.0f, 620.0f, 210.0f, 0.0f, 0.0f, 16.0f, 16.0f);
    fill_panel_vertices(&g_vertices[18],
            20.0f, 270.0f, 200.0f, 450.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    fill_panel_vertices(&g_vertices[24],
            230.0f, 270.0f, 410.0f, 450.0f,
            -0.5f, -0.5f, 1.5f, 1.5f);
    fill_panel_vertices(&g_vertices[30],
            440.0f, 270.0f, 620.0f, 450.0f,
            -0.5f, -0.5f, 1.5f, 1.5f);
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DCAPS8 caps;
    D3DPRESENT_PARAMETERS pp;
    DWORD required_filters;
    HRESULT hr;
    char line[224];

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
    begin_stage("04 GetDeviceCaps filtering");
    hr = IDirect3D8_GetDeviceCaps(g_d3d, 0, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return failed("GetDeviceCaps", hr);
    wsprintfA(line, "[d3d8-mipmap] TextureCaps=0x%08lX "
            "FilterCaps=0x%08lX AddressCaps=0x%08lX\r\n",
            (unsigned long)caps.TextureCaps,
            (unsigned long)caps.TextureFilterCaps,
            (unsigned long)caps.TextureAddressCaps);
    OutputDebugStringA(line);
    required_filters = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MINFLINEAR
            | D3DPTFILTERCAPS_MAGFPOINT
            | D3DPTFILTERCAPS_MAGFLINEAR
            | D3DPTFILTERCAPS_MIPFPOINT
            | D3DPTFILTERCAPS_MIPFLINEAR;
    if (!(caps.TextureCaps & D3DPTEXTURECAPS_MIPMAP)
            || (caps.TextureFilterCaps & required_filters) != required_filters)
        return failed("GetDeviceCaps requires mip point/linear filters",
                D3DERR_NOTAVAILABLE);
    if (!(caps.TextureAddressCaps & D3DPTADDRESSCAPS_WRAP)
            || !(caps.TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP))
        return failed("GetDeviceCaps requires WRAP and CLAMP",
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

static DWORD mip_color(UINT level)
{
    static const DWORD colors[TEST_MIP_LEVELS] =
    {
        0, /* Level zero is a checkerboard. */
        D3DCOLOR_ARGB(255, 255, 48, 48),
        D3DCOLOR_ARGB(255, 48, 224, 64),
        D3DCOLOR_ARGB(255, 32, 96, 255),
        D3DCOLOR_ARGB(255, 255, 224, 32),
        D3DCOLOR_ARGB(255, 32, 224, 224),
        D3DCOLOR_ARGB(255, 224, 32, 224),
        D3DCOLOR_ARGB(255, 240, 240, 240),
    };
    return colors[level];
}

static void fill_mip_level(UINT level, const D3DLOCKED_RECT *locked)
{
    UINT size = TEST_TEXTURE_SIZE >> level;
    UINT x, y;
    if (!size) size = 1;
    for (y = 0; y < size; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked->pBits + y * locked->Pitch);
        for (x = 0; x < size; ++x)
        {
            if (!level)
            {
                BOOL alternate = ((x / 16) ^ (y / 16)) & 1;
                row[x] = alternate
                        ? D3DCOLOR_ARGB(255, 255, 224, 32)
                        : D3DCOLOR_ARGB(255, 32, 96, 255);
            }
            else
            {
                row[x] = mip_color(level);
            }
        }
    }
}

static HRESULT create_resources(void)
{
    D3DLOCKED_RECT locked;
    BYTE *destination;
    UINT level;
    HRESULT hr;
    char stage[96];

    begin_stage("07 CreateTexture full mip chain");
    hr = IDirect3DDevice8_CreateTexture(g_device,
            TEST_TEXTURE_SIZE, TEST_TEXTURE_SIZE, TEST_MIP_LEVELS, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture);
    if (FAILED(hr)) return failed("CreateTexture(8 mip levels)", hr);
    if (IDirect3DTexture8_GetLevelCount(g_texture) != TEST_MIP_LEVELS)
        return failed("Texture level count is not 8", E_FAIL);

    for (level = 0; level < TEST_MIP_LEVELS; ++level)
    {
        wsprintfA(stage, "08 Lock/fill/unlock mip level %lu",
                (unsigned long)level);
        begin_stage(stage);
        ZeroMemory(&locked, sizeof(locked));
        hr = IDirect3DTexture8_LockRect(g_texture, level,
                &locked, NULL, 0);
        if (FAILED(hr)) return failed(stage, hr);
        fill_mip_level(level, &locked);
        hr = IDirect3DTexture8_UnlockRect(g_texture, level);
        if (FAILED(hr)) return failed(stage, hr);
    }

    build_vertices();
    begin_stage("09 CreateVertexBuffer six panels");
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

static HRESULT set_tss(D3DTEXTURESTAGESTATETYPE state, DWORD value,
        const char *name)
{
    HRESULT hr;
    begin_stage(name);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0, state, value);
    if (FAILED(hr)) return failed(name, hr);
    return D3D_OK;
}

static HRESULT configure_pipeline(void)
{
    HRESULT hr;
    begin_stage("11 LIGHTING false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("LIGHTING=FALSE", hr);
    begin_stage("12 CULLMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("CULLMODE=NONE", hr);
    begin_stage("13 ZENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, FALSE);
    if (FAILED(hr)) return failed("ZENABLE=FALSE", hr);
    begin_stage("14 SetTexture stage0");
    hr = IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)g_texture);
    if (FAILED(hr)) return failed("SetTexture(0)", hr);
    if (FAILED(hr = set_tss(D3DTSS_COLOROP, D3DTOP_MODULATE,
            "15 COLOROP MODULATE"))) return hr;
    if (FAILED(hr = set_tss(D3DTSS_COLORARG1, D3DTA_TEXTURE,
            "16 COLORARG1 TEXTURE"))) return hr;
    if (FAILED(hr = set_tss(D3DTSS_COLORARG2, D3DTA_DIFFUSE,
            "17 COLORARG2 DIFFUSE"))) return hr;
    if (FAILED(hr = set_tss(D3DTSS_ALPHAOP, D3DTOP_SELECTARG1,
            "18 ALPHAOP SELECTARG1"))) return hr;
    if (FAILED(hr = set_tss(D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
            "19 ALPHAARG1 TEXTURE"))) return hr;
    begin_stage("20 Disable texture stage1");
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (FAILED(hr)) return failed("Disable stage1", hr);
    begin_stage("21 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);
    begin_stage("22 SetVertexShader TEX1");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(TEX1)", hr);
    return D3D_OK;
}

static HRESULT set_filters(DWORD min_filter, DWORD mag_filter,
        DWORD mip_filter, DWORD address, const char *name)
{
    HRESULT hr;
    begin_stage(name);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MINFILTER, min_filter);
    if (FAILED(hr)) return failed(name, hr);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MAGFILTER, mag_filter);
    if (FAILED(hr)) return failed(name, hr);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MIPFILTER, mip_filter);
    if (FAILED(hr)) return failed(name, hr);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSU, address);
    if (FAILED(hr)) return failed(name, hr);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSV, address);
    if (FAILED(hr)) return failed(name, hr);
    return D3D_OK;
}

static HRESULT draw_panel(UINT panel, DWORD min_filter, DWORD mag_filter,
        DWORD mip_filter, DWORD address, const char *stage)
{
    HRESULT hr = set_filters(min_filter, mag_filter, mip_filter,
            address, stage);
    if (FAILED(hr)) return hr;
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, panel * 6, 2);
    if (FAILED(hr)) return failed(stage, hr);
    return D3D_OK;
}

static HRESULT render(HWND hwnd)
{
    HRESULT hr;
    begin_stage("23 Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_stage("24 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    hr = draw_panel(0, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE,
            D3DTADDRESS_WRAP, "25 top-left POINT no mip WRAP");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_panel(1, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT,
            D3DTADDRESS_WRAP, "26 top-middle LINEAR mip POINT");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_panel(2, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR,
            D3DTADDRESS_WRAP, "27 top-right LINEAR mip LINEAR");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_panel(3, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE,
            D3DTADDRESS_CLAMP, "28 bottom-left MAG POINT CLAMP");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_panel(4, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE,
            D3DTADDRESS_CLAMP, "29 bottom-middle MAG LINEAR CLAMP");
    if (FAILED(hr)) goto scene_failed;
    hr = draw_panel(5, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE,
            D3DTADDRESS_WRAP, "30 bottom-right POINT WRAP repeat");
    if (FAILED(hr)) goto scene_failed;

    begin_stage("31 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);
    begin_stage("32 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return failed("Present", hr);
    set_result_title(hwnd,
            "Present S_OK - top min/mip, bottom mag/address", hr);
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
    g_window = CreateWindowA(g_window_class, "D3D8 mipmap/filter: starting",
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
                "The D3D8 mipmap/filter test failed. Check the title and "
                "v86gl logs for the last call.",
                "D3D8 mipmap/filter test", MB_OK | MB_ICONERROR);
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
