// Direct3D 8 two-stage fixed-pipeline texture test for WineD3D -> v86 GL.
//
// Three pre-transformed quads use two independent texture-coordinate sets:
//   left   - stage 0 blue/yellow checker only
//   centre - stage 0 CURRENT modulated by a stage 1 grayscale lightmap
//   right  - stage 0 CURRENT added to the same stage 1 lightmap
//
// This isolates TEX2, SetTexture(0/1), TEXCOORDINDEX, CURRENT propagation,
// D3DTOP_MODULATE, and D3DTOP_ADD without matrices, depth, or blending.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH    640
#define TEST_CLIENT_HEIGHT   480
#define TEST_TEXTURE_WIDTH    64
#define TEST_TEXTURE_HEIGHT   64
#define TEST_CHECKER_SIZE      8
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2)

typedef struct TestVertex
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
    FLOAT rhw;
    DWORD color;
    FLOAT u0;
    FLOAT v0;
    FLOAT u1;
    FLOAT v1;
} TestVertex;

#define WHITE D3DCOLOR_XRGB(255, 255, 255)
#define QUAD(x0, x1) \
    {x0, 100.0f, 0.5f, 1.0f, WHITE, 0.0f, 0.0f, 0.0f, 0.0f}, \
    {x1, 100.0f, 0.5f, 1.0f, WHITE, 2.0f, 0.0f, 1.0f, 0.0f}, \
    {x1, 380.0f, 0.5f, 1.0f, WHITE, 2.0f, 2.0f, 1.0f, 1.0f}, \
    {x0, 100.0f, 0.5f, 1.0f, WHITE, 0.0f, 0.0f, 0.0f, 0.0f}, \
    {x1, 380.0f, 0.5f, 1.0f, WHITE, 2.0f, 2.0f, 1.0f, 1.0f}, \
    {x0, 380.0f, 0.5f, 1.0f, WHITE, 0.0f, 2.0f, 0.0f, 1.0f}

static const TestVertex g_vertices[] =
{
    QUAD(30.0f, 200.0f),
    QUAD(235.0f, 405.0f),
    QUAD(440.0f, 610.0f),
};

#undef QUAD
#undef WHITE

static const char g_window_class[] = "V86GLD3D8MultitextureTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static IDirect3DVertexBuffer8 *g_vertex_buffer;
static IDirect3DTexture8 *g_base_texture;
static IDirect3DTexture8 *g_lightmap_texture;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-multitexture] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[240];

    wsprintfA(line, "[d3d8-multitexture] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[240];

    wsprintfA(title, "D3D8 multitexture: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void begin_stage(const char *stage)
{
    char title[240];

    g_failed_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 multitexture: calling %s", stage);
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
    if (g_lightmap_texture)
    {
        IDirect3DTexture8_Release(g_lightmap_texture);
        g_lightmap_texture = NULL;
    }
    if (g_base_texture)
    {
        IDirect3DTexture8_Release(g_base_texture);
        g_base_texture = NULL;
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
    begin_stage("04 GetDeviceCaps");
    hr = IDirect3D8_GetDeviceCaps(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return failed("GetDeviceCaps", hr);
    wsprintfA(line, "[d3d8-multitexture] caps: textures=%lu stages=%lu "
            "texture_ops=0x%08lX\r\n",
            (unsigned long)caps.MaxSimultaneousTextures,
            (unsigned long)caps.MaxTextureBlendStages,
            (unsigned long)caps.TextureOpCaps);
    OutputDebugStringA(line);
    if (caps.MaxSimultaneousTextures < 2 || caps.MaxTextureBlendStages < 2)
        return failed("GetDeviceCaps requires 2 textures/stages",
                D3DERR_NOTAVAILABLE);
    if ((caps.TextureOpCaps
            & (D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_ADD))
            != (D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_ADD))
        return failed("GetDeviceCaps requires MODULATE and ADD",
                D3DERR_NOTAVAILABLE);

    begin_stage("05 CheckDeviceFormat A8R8G8B8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, 0, D3DRTYPE_TEXTURE,
            D3DFMT_A8R8G8B8);
    if (FAILED(hr)) return failed("CheckDeviceFormat(A8R8G8B8)", hr);

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

    begin_stage("06 CreateDevice");
    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    if (FAILED(hr)) return failed("CreateDevice", hr);

    return D3D_OK;
}

static void fill_base_texture(const D3DLOCKED_RECT *locked_rect)
{
    UINT x;
    UINT y;

    for (y = 0; y < TEST_TEXTURE_HEIGHT; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked_rect->pBits
                + y * locked_rect->Pitch);
        for (x = 0; x < TEST_TEXTURE_WIDTH; ++x)
        {
            BOOL alternate = ((x / TEST_CHECKER_SIZE)
                    ^ (y / TEST_CHECKER_SIZE)) & 1;
            row[x] = alternate
                    ? D3DCOLOR_ARGB(255, 255, 224, 32)
                    : D3DCOLOR_ARGB(255, 32, 96, 255);
        }
    }
}

static void fill_lightmap_texture(const D3DLOCKED_RECT *locked_rect)
{
    UINT x;
    UINT y;

    for (y = 0; y < TEST_TEXTURE_HEIGHT; ++y)
    {
        DWORD *row = (DWORD *)((BYTE *)locked_rect->pBits
                + y * locked_rect->Pitch);
        for (x = 0; x < TEST_TEXTURE_WIDTH; ++x)
        {
            UINT value = 32 + (x / 16) * 64;
            if ((y / 16) & 1)
                value += 16;
            row[x] = D3DCOLOR_ARGB(255, value, value, value);
        }
    }
}

static HRESULT create_one_texture(IDirect3DTexture8 **texture,
        void (*fill)(const D3DLOCKED_RECT *), const char *create_stage,
        const char *lock_stage, const char *unlock_stage)
{
    D3DLOCKED_RECT locked_rect;
    HRESULT hr;

    begin_stage(create_stage);
    hr = IDirect3DDevice8_CreateTexture(g_device,
            TEST_TEXTURE_WIDTH, TEST_TEXTURE_HEIGHT, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture);
    if (FAILED(hr)) return failed(create_stage, hr);

    ZeroMemory(&locked_rect, sizeof(locked_rect));
    begin_stage(lock_stage);
    hr = IDirect3DTexture8_LockRect(*texture, 0, &locked_rect, NULL, 0);
    if (FAILED(hr)) return failed(lock_stage, hr);
    fill(&locked_rect);

    begin_stage(unlock_stage);
    hr = IDirect3DTexture8_UnlockRect(*texture, 0);
    if (FAILED(hr)) return failed(unlock_stage, hr);

    return D3D_OK;
}

static HRESULT create_resources(void)
{
    BYTE *destination;
    HRESULT hr;

    hr = create_one_texture(&g_base_texture, fill_base_texture,
            "07 CreateTexture base", "08 Base LockRect",
            "09 Base UnlockRect");
    if (FAILED(hr)) return hr;

    hr = create_one_texture(&g_lightmap_texture, fill_lightmap_texture,
            "10 CreateTexture lightmap", "11 Lightmap LockRect",
            "12 Lightmap UnlockRect");
    if (FAILED(hr)) return hr;

    begin_stage("13 CreateVertexBuffer TEX2");
    hr = IDirect3DDevice8_CreateVertexBuffer(g_device, sizeof(g_vertices),
            D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING,
            TEST_FVF, D3DPOOL_DEFAULT, &g_vertex_buffer);
    if (FAILED(hr)) return failed("CreateVertexBuffer(TEX2)", hr);

    destination = NULL;
    begin_stage("14 VertexBuffer Lock");
    hr = IDirect3DVertexBuffer8_Lock(g_vertex_buffer, 0,
            sizeof(g_vertices), &destination, 0);
    if (FAILED(hr)) return failed("VertexBuffer::Lock", hr);
    CopyMemory(destination, g_vertices, sizeof(g_vertices));

    begin_stage("15 VertexBuffer Unlock");
    hr = IDirect3DVertexBuffer8_Unlock(g_vertex_buffer);
    if (FAILED(hr)) return failed("VertexBuffer::Unlock", hr);

    return D3D_OK;
}

static HRESULT set_tss(DWORD stage, D3DTEXTURESTAGESTATETYPE state,
        DWORD value, const char *name)
{
    HRESULT hr;

    begin_stage(name);
    hr = IDirect3DDevice8_SetTextureStageState(g_device, stage, state, value);
    if (FAILED(hr)) return failed(name, hr);
    return D3D_OK;
}

static HRESULT configure_fixed_pipeline(void)
{
    HRESULT hr;

    begin_stage("16 LIGHTING false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
    if (FAILED(hr)) return failed("LIGHTING=FALSE", hr);
    begin_stage("17 ZENABLE false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ZENABLE, FALSE);
    if (FAILED(hr)) return failed("ZENABLE=FALSE", hr);
    begin_stage("18 CULLMODE none");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_CULLMODE,
            D3DCULL_NONE);
    if (FAILED(hr)) return failed("CULLMODE=NONE", hr);
    begin_stage("19 ALPHABLEND false");
    hr = IDirect3DDevice8_SetRenderState(g_device, D3DRS_ALPHABLENDENABLE,
            FALSE);
    if (FAILED(hr)) return failed("ALPHABLEND=FALSE", hr);

    begin_stage("20 SetTexture stage 0 base");
    hr = IDirect3DDevice8_SetTexture(g_device, 0,
            (IDirect3DBaseTexture8 *)g_base_texture);
    if (FAILED(hr)) return failed("SetTexture(0, base)", hr);
    if (FAILED(hr = set_tss(0, D3DTSS_COLOROP, D3DTOP_MODULATE,
            "21 stage0 COLOROP MODULATE"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE,
            "22 stage0 COLORARG1 TEXTURE"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE,
            "23 stage0 COLORARG2 DIFFUSE"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1,
            "24 stage0 ALPHAOP SELECTARG1"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
            "25 stage0 ALPHAARG1 TEXTURE"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_TEXCOORDINDEX, 0,
            "26 stage0 TEXCOORDINDEX 0"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_MAGFILTER, D3DTEXF_POINT,
            "27 stage0 MAGFILTER POINT"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_MINFILTER, D3DTEXF_POINT,
            "28 stage0 MINFILTER POINT"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_MIPFILTER, D3DTEXF_NONE,
            "29 stage0 MIPFILTER NONE"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP,
            "30 stage0 ADDRESSU WRAP"))) return hr;
    if (FAILED(hr = set_tss(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP,
            "31 stage0 ADDRESSV WRAP"))) return hr;

    begin_stage("32 SetTexture stage 1 lightmap");
    hr = IDirect3DDevice8_SetTexture(g_device, 1,
            (IDirect3DBaseTexture8 *)g_lightmap_texture);
    if (FAILED(hr)) return failed("SetTexture(1, lightmap)", hr);
    if (FAILED(hr = set_tss(1, D3DTSS_TEXCOORDINDEX, 1,
            "33 stage1 TEXCOORDINDEX 1"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_MAGFILTER, D3DTEXF_POINT,
            "34 stage1 MAGFILTER POINT"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_MINFILTER, D3DTEXF_POINT,
            "35 stage1 MINFILTER POINT"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_MIPFILTER, D3DTEXF_NONE,
            "36 stage1 MIPFILTER NONE"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP,
            "37 stage1 ADDRESSU CLAMP"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP,
            "38 stage1 ADDRESSV CLAMP"))) return hr;
    if (FAILED(hr = set_tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE,
            "39 stage1 COLOROP DISABLE"))) return hr;
    if (FAILED(hr = set_tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE,
            "40 stage2 COLOROP DISABLE"))) return hr;

    begin_stage("41 SetStreamSource");
    hr = IDirect3DDevice8_SetStreamSource(g_device, 0, g_vertex_buffer,
            sizeof(TestVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);
    begin_stage("42 SetVertexShader FVF TEX2");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) return failed("SetVertexShader(FVF TEX2)", hr);

    return D3D_OK;
}

static HRESULT render_panels(HWND hwnd)
{
    HRESULT hr;

    begin_stage("43 Clear");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    if (FAILED(hr)) return failed("Clear", hr);
    begin_stage("44 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return failed("BeginScene", hr);

    begin_stage("45 Draw left stage0 only");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 0, 2);
    if (FAILED(hr)) goto draw_failed;

    if (FAILED(hr = set_tss(1, D3DTSS_COLOROP, D3DTOP_MODULATE,
            "46 stage1 COLOROP MODULATE"))) goto scene_failed;
    if (FAILED(hr = set_tss(1, D3DTSS_COLORARG1, D3DTA_CURRENT,
            "47 stage1 COLORARG1 CURRENT"))) goto scene_failed;
    if (FAILED(hr = set_tss(1, D3DTSS_COLORARG2, D3DTA_TEXTURE,
            "48 stage1 COLORARG2 TEXTURE"))) goto scene_failed;
    if (FAILED(hr = set_tss(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1,
            "49 stage1 ALPHAOP SELECTARG1"))) goto scene_failed;
    if (FAILED(hr = set_tss(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT,
            "50 stage1 ALPHAARG1 CURRENT"))) goto scene_failed;

    begin_stage("51 Draw centre MODULATE");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 6, 2);
    if (FAILED(hr)) goto draw_failed;

    if (FAILED(hr = set_tss(1, D3DTSS_COLOROP, D3DTOP_ADD,
            "52 stage1 COLOROP ADD"))) goto scene_failed;
    begin_stage("53 Draw right ADD");
    hr = IDirect3DDevice8_DrawPrimitive(g_device,
            D3DPT_TRIANGLELIST, 12, 2);
    if (FAILED(hr)) goto draw_failed;

    begin_stage("54 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return failed("EndScene", hr);

    begin_stage("55 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (FAILED(hr)) return failed("Present", hr);
    set_result_title(hwnd,
            "Present S_OK - base | MODULATE lightmap | ADD lightmap", hr);
    return hr;

draw_failed:
    failed(g_failed_stage, hr);
scene_failed:
    IDirect3DDevice8_EndScene(g_device);
    return hr;
}

static HRESULT init_and_render(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr)) return hr;
    hr = create_resources();
    if (FAILED(hr)) return hr;
    hr = configure_fixed_pipeline();
    if (FAILED(hr)) return hr;
    return render_panels(hwnd);
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
    hwnd = CreateWindowA(g_window_class, "D3D8 multitexture: starting",
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
                "The D3D8 multitexture test failed. Check the window title, "
                "guest debug output, and v86gl logs for the HRESULT and "
                "last call.",
                "D3D8 multitexture test", MB_OK | MB_ICONERROR);
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
