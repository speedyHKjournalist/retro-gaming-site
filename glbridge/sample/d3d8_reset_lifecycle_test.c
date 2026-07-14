// Direct3D 8 Reset, pool lifetime, resize and device/context churn test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define OLD_WIDTH  480
#define OLD_HEIGHT 360
#define NEW_WIDTH  720
#define NEW_HEIGHT 480
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} TestVertex;

static const char g_class_name[] = "V86GLD3D8ResetLifecycleTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DTexture8 *g_managed_texture;
static IDirect3DVertexBuffer8 *g_default_vb;
static HWND g_window;
static D3DFORMAT g_backbuffer_format;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-reset-lifecycle] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void begin_stage(const char *stage)
{
    char title[250];
    trace_text(stage);
    wsprintfA(title, "D3D8 Reset/lifecycle: calling %s", stage);
    if (g_window) SetWindowTextA(g_window, title);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    char title[250];
    char line[250];
    wsprintfA(line, "[d3d8-reset-lifecycle] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
    wsprintfA(title, "D3D8 Reset/lifecycle: %s (0x%08lX)",
            stage, (unsigned long)hr);
    if (g_window) SetWindowTextA(g_window, title);
    return hr;
}

static void fill_pp(D3DPRESENT_PARAMETERS *pp, UINT width, UINT height,
        BOOL depth)
{
    ZeroMemory(pp, sizeof(*pp));
    pp->BackBufferWidth = width;
    pp->BackBufferHeight = height;
    pp->BackBufferFormat = g_backbuffer_format;
    pp->BackBufferCount = 1;
    pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp->hDeviceWindow = g_window;
    pp->Windowed = TRUE;
    pp->EnableAutoDepthStencil = depth;
    pp->AutoDepthStencilFormat = depth ? D3DFMT_D24S8 : D3DFMT_UNKNOWN;
    pp->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
}

static void release_all(void)
{
    if (g_default_vb)
    {
        IDirect3DVertexBuffer8_Release(g_default_vb);
        g_default_vb = NULL;
    }
    if (g_managed_texture)
    {
        IDirect3DTexture8_Release(g_managed_texture);
        g_managed_texture = NULL;
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

static HRESULT create_device(D3DPRESENT_PARAMETERS *pp,
        IDirect3DDevice8 **device)
{
    return IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, g_window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, pp, device);
}

static HRESULT churn_devices(void)
{
    D3DPRESENT_PARAMETERS pp;
    unsigned int cycle;
    HRESULT hr;
    fill_pp(&pp, 192, 128, FALSE);
    for (cycle = 1; cycle <= 4; ++cycle)
    {
        IDirect3DDevice8 *device = NULL;
        ULONG refs;
        char stage[100];
        wsprintfA(stage, "device/context cycle %u CreateDevice", cycle);
        begin_stage(stage);
        hr = create_device(&pp, &device);
        if (FAILED(hr)) return fail_stage(stage, hr);
        wsprintfA(stage, "device/context cycle %u Clear+Present", cycle);
        begin_stage(stage);
        hr = IDirect3DDevice8_Clear(device, 0, NULL, D3DCLEAR_TARGET,
                D3DCOLOR_XRGB(12 * cycle, 20, 96 + 24 * cycle), 1.0f, 0);
        if (SUCCEEDED(hr))
            hr = IDirect3DDevice8_Present(device, NULL, NULL, NULL, NULL);
        if (FAILED(hr))
        {
            IDirect3DDevice8_Release(device);
            return fail_stage(stage, hr);
        }
        refs = IDirect3DDevice8_Release(device);
        if (refs != 0)
        {
            wsprintfA(stage, "device/context cycle %u leaked references", cycle);
            return fail_stage(stage, E_FAIL);
        }
        wsprintfA(stage, "device/context cycle %u released to zero", cycle);
        trace_text(stage);
    }
    return D3D_OK;
}

static HRESULT create_managed_texture(void)
{
    D3DLOCKED_RECT lock;
    UINT x, y;
    HRESULT hr;
    begin_stage("CreateTexture D3DPOOL_MANAGED");
    hr = IDirect3DDevice8_CreateTexture(g_device, 16, 16, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_managed_texture);
    if (FAILED(hr)) return fail_stage("CreateTexture MANAGED", hr);
    hr = IDirect3DTexture8_LockRect(g_managed_texture, 0, &lock, NULL, 0);
    if (FAILED(hr)) return fail_stage("managed texture LockRect", hr);
    for (y = 0; y < 16; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)lock.pBits + y * lock.Pitch);
        for (x = 0; x < 16; ++x)
            row[x] = (((x >> 1) ^ (y >> 1)) & 1)
                    ? D3DCOLOR_ARGB(255, 255, 210, 24)
                    : D3DCOLOR_ARGB(255, 28, 92, 255);
    }
    hr = IDirect3DTexture8_UnlockRect(g_managed_texture, 0);
    return FAILED(hr) ? fail_stage("managed texture UnlockRect", hr) : hr;
}

static HRESULT create_default_vb(UINT width, UINT height)
{
    TestVertex *v;
    FLOAT x0 = 54.0f, y0 = 46.0f;
    FLOAT x1 = (FLOAT)width - 54.0f;
    FLOAT y1 = (FLOAT)height - 46.0f;
    const TestVertex source[6] =
    {
        {x0, y0, 0.4f, 1.0f, 0xffffffff, 0.0f, 0.0f},
        {x1, y0, 0.4f, 1.0f, 0xffffffff, 8.0f, 0.0f},
        {x1, y1, 0.4f, 1.0f, 0xffffffff, 8.0f, 6.0f},
        {x0, y0, 0.4f, 1.0f, 0xffffffff, 0.0f, 0.0f},
        {x1, y1, 0.4f, 1.0f, 0xffffffff, 8.0f, 6.0f},
        {x0, y1, 0.4f, 1.0f, 0xffffffff, 0.0f, 6.0f}
    };
    HRESULT hr;
    begin_stage("CreateVertexBuffer D3DPOOL_DEFAULT");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(source), 0,
            TEST_FVF, D3DPOOL_DEFAULT, &g_default_vb);
    if (FAILED(hr)) return fail_stage("CreateVertexBuffer DEFAULT", hr);
    hr = IDirect3DVertexBuffer8_Lock(g_default_vb, 0, sizeof(source),
            (BYTE **)&v, 0);
    if (FAILED(hr)) return fail_stage("default VB Lock", hr);
    CopyMemory(v, source, sizeof(source));
    hr = IDirect3DVertexBuffer8_Unlock(g_default_vb);
    return FAILED(hr) ? fail_stage("default VB Unlock", hr) : hr;
}

static HRESULT configure_pipeline(void)
{
    HRESULT hr;
#define SET(call, name) do { hr = (call); if (FAILED(hr)) return fail_stage(name, hr); } while (0)
    SET(IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF), "SetVertexShader");
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE), "LIGHTING false");
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE, D3DCULL_NONE), "CULL none");
    SET(IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)g_managed_texture), "SetTexture managed");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_MODULATE), "stage COLOROP MODULATE");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_TEXTURE), "stage COLORARG1 TEXTURE");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG2, D3DTA_DIFFUSE), "stage COLORARG2 DIFFUSE");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE), "disable stage1");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSU, D3DTADDRESS_WRAP), "ADDRESSU wrap");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ADDRESSV, D3DTADDRESS_WRAP), "ADDRESSV wrap");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MINFILTER, D3DTEXF_POINT), "MINFILTER point");
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_MAGFILTER, D3DTEXF_POINT), "MAGFILTER point");
    SET(IDirect3DDevice8_SetStreamSource(g_device, 0, g_default_vb,
            sizeof(TestVertex)), "SetStreamSource recreated VB");
#undef SET
    return D3D_OK;
}

static HRESULT render_present(const char *present_stage)
{
    HRESULT hr;
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(3, 7, 20), 1.0f, 0);
    if (FAILED(hr)) return fail_stage("Clear", hr);
    hr = configure_pipeline();
    if (FAILED(hr)) return hr;
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return fail_stage("BeginScene", hr);
    hr = IDirect3DDevice8_DrawPrimitive(g_device, D3DPT_TRIANGLELIST, 0, 2);
    if (FAILED(hr))
    {
        IDirect3DDevice8_EndScene(g_device);
        return fail_stage("DrawPrimitive DEFAULT VB", hr);
    }
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return fail_stage("EndScene", hr);
    begin_stage(present_stage);
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    return FAILED(hr) ? fail_stage(present_stage, hr) : hr;
}

static HRESULT run_reset_test(void)
{
    D3DPRESENT_PARAMETERS pp;
    D3DSURFACE_DESC before, after;
    RECT rect = {0, 0, NEW_WIDTH, NEW_HEIGHT};
    ULONG refs;
    HRESULT hr;

    fill_pp(&pp, OLD_WIDTH, OLD_HEIGHT, TRUE);
    begin_stage("long-lived CreateDevice 480x360");
    hr = create_device(&pp, &g_device);
    if (FAILED(hr)) return fail_stage("CreateDevice 480x360", hr);
    hr = create_managed_texture();
    if (FAILED(hr)) return hr;
    ZeroMemory(&before, sizeof(before));
    hr = IDirect3DTexture8_GetLevelDesc(g_managed_texture, 0, &before);
    if (FAILED(hr)) return fail_stage("managed GetLevelDesc before Reset", hr);
    hr = create_default_vb(OLD_WIDTH, OLD_HEIGHT);
    if (FAILED(hr)) return hr;
    hr = render_present("pre-Reset Present 480x360");
    if (FAILED(hr)) return hr;

    begin_stage("unbind DEFAULT-pool VB before Reset");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, NULL, 0);
    if (FAILED(hr)) return fail_stage("unbind default VB before Reset", hr);
    begin_stage("release DEFAULT-pool VB before Reset");
    refs = IDirect3DVertexBuffer8_Release(g_default_vb);
    g_default_vb = NULL;
    if (refs != 0) return fail_stage("default VB leaked before Reset", E_FAIL);

    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    begin_stage("resize client window to 720x480");
    if (!SetWindowPos(g_window, NULL, 0, 0, rect.right - rect.left,
            rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        return fail_stage("SetWindowPos 720x480", HRESULT_FROM_WIN32(GetLastError()));

    fill_pp(&pp, NEW_WIDTH, NEW_HEIGHT, TRUE);
    begin_stage("Reset backbuffer 480x360 -> 720x480");
    hr = IDirect3DDevice8_Reset(g_device, &pp);
    if (FAILED(hr)) return fail_stage("Reset 720x480", hr);

    ZeroMemory(&after, sizeof(after));
    begin_stage("verify managed texture survived Reset");
    hr = IDirect3DTexture8_GetLevelDesc(g_managed_texture, 0, &after);
    if (FAILED(hr)) return fail_stage("managed GetLevelDesc after Reset", hr);
    if (before.Width != after.Width || before.Height != after.Height
            || before.Format != after.Format)
        return fail_stage("managed texture description changed across Reset", E_FAIL);

    begin_stage("recreate DEFAULT-pool VB after Reset");
    hr = create_default_vb(NEW_WIDTH, NEW_HEIGHT);
    if (FAILED(hr)) return hr;
    hr = render_present("post-Reset Present 720x480");
    if (FAILED(hr)) return hr;
    SetWindowTextA(g_window,
            "D3D8 Reset: Present S_OK - 4 device cycles | 480x360->720x480 | managed kept | default rebuilt");
    return D3D_OK;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    D3DDISPLAYMODE mode;
    MSG message;
    HRESULT hr;
    RECT rect = {0, 0, OLD_WIDTH, OLD_HEIGHT};
    (void)previous; (void)command_line; (void)show_command;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = g_class_name;
    if (!RegisterClassA(&wc)) return 1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_class_name, "D3D8 Reset/lifecycle: starting",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 1;
    begin_stage("Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    hr = g_d3d ? D3D_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
    if (SUCCEEDED(hr)) g_backbuffer_format = mode.Format;
    if (FAILED(hr)) fail_stage("Direct3DCreate8/GetAdapterDisplayMode", hr);
    if (SUCCEEDED(hr)) hr = churn_devices();
    if (SUCCEEDED(hr)) hr = run_reset_test();
    if (FAILED(hr)) MessageBoxA(g_window,
            "The D3D8 Reset/lifecycle test failed. Check the window title and browser cleanup logs.",
            "D3D8 Reset/lifecycle test", MB_OK | MB_ICONERROR);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    release_all();
    return FAILED(hr) ? 1 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleA(NULL), NULL,
            GetCommandLineA(), SW_SHOWDEFAULT));
}
