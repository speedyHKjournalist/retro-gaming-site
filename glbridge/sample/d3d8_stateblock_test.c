// Direct3D 8 Begin/EndStateBlock and CreateStateBlock restoration test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_WIDTH  780
#define TEST_HEIGHT 420
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} TestVertex;

static const char g_class_name[] = "V86GLD3D8StateBlockTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DTexture8 *g_texture;
static HWND g_window;
static DWORD g_recorded_block;
static DWORD g_all_block;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-stateblock] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void begin_stage(const char *stage)
{
    char title[240];
    trace_text(stage);
    wsprintfA(title, "D3D8 state block: calling %s", stage);
    if (g_window) SetWindowTextA(g_window, title);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    char title[240];
    char line[240];
    wsprintfA(line, "[d3d8-stateblock] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
    wsprintfA(title, "D3D8 state block: %s (0x%08lX)",
            stage, (unsigned long)hr);
    if (g_window) SetWindowTextA(g_window, title);
    return hr;
}

static void release_all(void)
{
    if (g_device && g_recorded_block)
        IDirect3DDevice8_DeleteStateBlock(g_device, g_recorded_block);
    if (g_device && g_all_block)
        IDirect3DDevice8_DeleteStateBlock(g_device, g_all_block);
    g_recorded_block = 0;
    g_all_block = 0;
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

static HRESULT draw_quad(FLOAT x0, FLOAT y0, FLOAT x1, FLOAT y1,
        FLOAT z, DWORD color)
{
    TestVertex v[6] =
    {
        {x0, y0, z, 1.0f, color, 0.0f, 0.0f},
        {x1, y0, z, 1.0f, color, 1.0f, 0.0f},
        {x1, y1, z, 1.0f, color, 1.0f, 1.0f},
        {x0, y0, z, 1.0f, color, 0.0f, 0.0f},
        {x1, y1, z, 1.0f, color, 1.0f, 1.0f},
        {x0, y1, z, 1.0f, color, 0.0f, 1.0f}
    };
    return IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 2, v, sizeof(TestVertex));
}

static HRESULT set_good_state(void)
{
    HRESULT hr;
#define SET(call) do { hr = (call); if (FAILED(hr)) return hr; } while (0)
    SET(IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)g_texture));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ALPHABLENDENABLE, TRUE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, TRUE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZWRITEENABLE, TRUE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC, D3DCMP_LESS));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_MODULATE));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_TEXTURE));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG2, D3DTA_DIFFUSE));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG2));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 1,
            D3DTSS_COLOROP, D3DTOP_DISABLE));
#undef SET
    return D3D_OK;
}

static HRESULT set_bad_state(void)
{
    HRESULT hr;
#define SET(call) do { hr = (call); if (FAILED(hr)) return hr; } while (0)
    SET(IDirect3DDevice8_SetTexture(g_device, 0, NULL));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ALPHABLENDENABLE, FALSE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_SRCBLEND, D3DBLEND_ONE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_DESTBLEND, D3DBLEND_ZERO));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, FALSE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZWRITEENABLE, FALSE));
    SET(IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZFUNC, D3DCMP_ALWAYS));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG2));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_CURRENT));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG2, D3DTA_DIFFUSE));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG2));
    SET(IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
#undef SET
    return D3D_OK;
}

static HRESULT expect_rs(D3DRENDERSTATETYPE state, DWORD expected,
        const char *name)
{
    DWORD value = 0;
    HRESULT hr = IDirect3DDevice8_GetRenderState(g_device, state, &value);
    if (FAILED(hr)) return fail_stage(name, hr);
    return value == expected ? D3D_OK : fail_stage(name, E_FAIL);
}

static HRESULT expect_tss(D3DTEXTURESTAGESTATETYPE state, DWORD expected,
        const char *name)
{
    DWORD value = 0;
    HRESULT hr = IDirect3DDevice8_GetTextureStageState(g_device, 0,
            state, &value);
    if (FAILED(hr)) return fail_stage(name, hr);
    return value == expected ? D3D_OK : fail_stage(name, E_FAIL);
}

static HRESULT verify_good_state(const char *prefix)
{
    IDirect3DBaseTexture8 *texture = NULL;
    HRESULT hr;
    begin_stage(prefix);
    hr = IDirect3DDevice8_GetTexture(g_device, 0, &texture);
    if (FAILED(hr)) return fail_stage("GetTexture after ApplyStateBlock", hr);
    if (texture != (IDirect3DBaseTexture8 *)g_texture)
    {
        if (texture) IDirect3DBaseTexture8_Release(texture);
        return fail_stage("texture pointer was not restored", E_FAIL);
    }
    if (texture) IDirect3DBaseTexture8_Release(texture);
    if (FAILED(hr = expect_rs(D3DRS_ALPHABLENDENABLE, TRUE,
            "ALPHABLENDENABLE was not restored"))) return hr;
    if (FAILED(hr = expect_rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA,
            "SRCBLEND was not restored"))) return hr;
    if (FAILED(hr = expect_rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA,
            "DESTBLEND was not restored"))) return hr;
    if (FAILED(hr = expect_rs(D3DRS_ZENABLE, TRUE,
            "ZENABLE was not restored"))) return hr;
    if (FAILED(hr = expect_rs(D3DRS_ZWRITEENABLE, TRUE,
            "ZWRITEENABLE was not restored"))) return hr;
    if (FAILED(hr = expect_rs(D3DRS_ZFUNC, D3DCMP_LESS,
            "ZFUNC was not restored"))) return hr;
    if (FAILED(hr = expect_tss(D3DTSS_COLOROP, D3DTOP_MODULATE,
            "stage COLOROP was not restored"))) return hr;
    if (FAILED(hr = expect_tss(D3DTSS_COLORARG1, D3DTA_TEXTURE,
            "stage COLORARG1 was not restored"))) return hr;
    if (FAILED(hr = expect_tss(D3DTSS_COLORARG2, D3DTA_DIFFUSE,
            "stage COLORARG2 was not restored"))) return hr;
    return D3D_OK;
}

static HRESULT create_texture(void)
{
    D3DLOCKED_RECT lock;
    HRESULT hr;
    UINT x, y;
    begin_stage("create managed checker texture");
    hr = IDirect3DDevice8_CreateTexture(g_device, 8, 8, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture);
    if (FAILED(hr)) return fail_stage("CreateTexture", hr);
    hr = IDirect3DTexture8_LockRect(g_texture, 0, &lock, NULL, 0);
    if (FAILED(hr)) return fail_stage("Texture LockRect", hr);
    for (y = 0; y < 8; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)lock.pBits + y * lock.Pitch);
        for (x = 0; x < 8; ++x)
            row[x] = ((x ^ y) & 1) ? D3DCOLOR_ARGB(255, 255, 220, 24)
                    : D3DCOLOR_ARGB(255, 24, 100, 255);
    }
    hr = IDirect3DTexture8_UnlockRect(g_texture, 0);
    return FAILED(hr) ? fail_stage("Texture UnlockRect", hr) : hr;
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;
    begin_stage("Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) return fail_stage("Direct3DCreate8 returned NULL", E_FAIL);
    hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
    if (FAILED(hr)) return fail_stage("GetAdapterDisplayMode", hr);
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_WIDTH;
    pp.BackBufferHeight = TEST_HEIGHT;
    pp.BackBufferFormat = mode.Format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    begin_stage("CreateDevice D24S8");
    hr = IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    return FAILED(hr) ? fail_stage("CreateDevice", hr) : hr;
}

static HRESULT configure_and_capture(void)
{
    HRESULT hr;
    begin_stage("SetVertexShader XYZRHW|DIFFUSE|TEX1");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return fail_stage("SetVertexShader", hr);
    IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE, D3DCULL_NONE);

    begin_stage("BeginStateBlock");
    hr = IDirect3DDevice8_BeginStateBlock(g_device);
    if (FAILED(hr)) return fail_stage("BeginStateBlock", hr);
    hr = set_good_state();
    if (FAILED(hr)) return fail_stage("record good state setters", hr);
    begin_stage("EndStateBlock");
    hr = IDirect3DDevice8_EndStateBlock(g_device, &g_recorded_block);
    if (FAILED(hr) || !g_recorded_block)
        return fail_stage("EndStateBlock", FAILED(hr) ? hr : E_FAIL);

    hr = set_good_state();
    if (FAILED(hr)) return fail_stage("set good state before CreateStateBlock", hr);
    begin_stage("CreateStateBlock D3DSBT_ALL");
    hr = IDirect3DDevice8_CreateStateBlock(g_device, D3DSBT_ALL, &g_all_block);
    if (FAILED(hr) || !g_all_block)
        return fail_stage("CreateStateBlock", FAILED(hr) ? hr : E_FAIL);
    return D3D_OK;
}

static HRESULT draw_panel(FLOAT x0, FLOAT x1, DWORD block,
        const char *apply_name, const char *verify_name)
{
    HRESULT hr;
    hr = set_bad_state();
    if (FAILED(hr)) return fail_stage("set deliberately bad state", hr);
    hr = draw_quad(x0, 55, x1, 365, 0.95f, D3DCOLOR_XRGB(8, 30, 72));
    if (FAILED(hr)) return fail_stage("draw panel background", hr);
    if (block)
    {
        begin_stage(apply_name);
        hr = IDirect3DDevice8_ApplyStateBlock(g_device, block);
    }
    else
    {
        begin_stage(apply_name);
        hr = set_good_state();
    }
    if (FAILED(hr)) return fail_stage(apply_name, hr);
    hr = verify_good_state(verify_name);
    if (FAILED(hr)) return hr;
    hr = draw_quad(x0 + 14, 72, x1 - 14, 348, 0.30f,
            D3DCOLOR_ARGB(176, 255, 255, 255));
    if (FAILED(hr)) return fail_stage("draw restored checker", hr);
    /* Correct restored depth state rejects this farther red probe. */
    hr = draw_quad(x0 + 55, 132, x1 - 55, 288, 0.70f,
            D3DCOLOR_ARGB(255, 255, 0, 0));
    return FAILED(hr) ? fail_stage("draw depth restore probe", hr) : hr;
}

static HRESULT render(void)
{
    HRESULT hr;
    begin_stage("Clear target/depth");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_XRGB(2, 4, 14), 1.0f, 0);
    if (FAILED(hr)) return fail_stage("Clear", hr);
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return fail_stage("BeginScene", hr);
    hr = draw_panel(30, 240, 0,
            "direct good-state reference", "verify direct reference");
    if (FAILED(hr)) goto failed;
    hr = draw_panel(285, 495, g_recorded_block,
            "ApplyStateBlock recorded block", "verify recorded block");
    if (FAILED(hr)) goto failed;
    hr = draw_panel(540, 750, g_all_block,
            "ApplyStateBlock ALL block", "verify ALL block");
    if (FAILED(hr)) goto failed;
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return fail_stage("EndScene", hr);
    begin_stage("Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return fail_stage("Present", hr);
    SetWindowTextA(g_window,
            "D3D8 state blocks: Present S_OK - direct | Begin/End | ALL match (0x00000000)");
    return D3D_OK;
failed:
    IDirect3DDevice8_EndScene(g_device);
    return hr;
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
    MSG message;
    HRESULT hr;
    RECT rect = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    (void)previous; (void)command_line; (void)show_command;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = g_class_name;
    if (!RegisterClassA(&wc)) return 1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_class_name, "D3D8 state block: starting",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 1;
    hr = create_device(g_window);
    if (SUCCEEDED(hr)) hr = create_texture();
    if (SUCCEEDED(hr)) hr = configure_and_capture();
    if (SUCCEEDED(hr)) hr = render();
    if (FAILED(hr)) MessageBoxA(g_window,
            "The D3D8 state-block test failed. Check the window title and guest debug output.",
            "D3D8 state-block test", MB_OK | MB_ICONERROR);
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
