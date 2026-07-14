// Direct3D 8 raster, viewport, colour-mask, depth-bias and stencil test.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_WIDTH  840
#define TEST_HEIGHT 600
#define TEST_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
#define CLIP_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)

typedef struct TestVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
} TestVertex;

typedef struct ClipVertex
{
    FLOAT x, y, z;
    DWORD color;
} ClipVertex;

static const char g_class_name[] = "V86GLD3D8RasterStencilTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static HWND g_window;
static const char *g_stage = "unknown";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-raster-stencil] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void begin_stage(const char *stage)
{
    char title[240];
    g_stage = stage;
    trace_text(stage);
    wsprintfA(title, "D3D8 raster/stencil: calling %s", stage);
    if (g_window) SetWindowTextA(g_window, title);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    char title[240];
    char line[240];
    g_stage = stage;
    wsprintfA(line, "[d3d8-raster-stencil] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
    wsprintfA(title, "D3D8 raster/stencil: %s (0x%08lX)",
            stage, (unsigned long)hr);
    if (g_window) SetWindowTextA(g_window, title);
    return hr;
}

static void release_all(void)
{
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

static HRESULT set_rs(D3DRENDERSTATETYPE state, DWORD value, const char *name)
{
    HRESULT hr;
    begin_stage(name);
    hr = IDirect3DDevice8_SetRenderState(g_device, state, value);
    return FAILED(hr) ? fail_stage(name, hr) : hr;
}

static HRESULT draw_quad(FLOAT x0, FLOAT y0, FLOAT x1, FLOAT y1,
        FLOAT z, DWORD color)
{
    TestVertex v[6] =
    {
        {x0, y0, z, 1.0f, color}, {x1, y0, z, 1.0f, color},
        {x1, y1, z, 1.0f, color}, {x0, y0, z, 1.0f, color},
        {x1, y1, z, 1.0f, color}, {x0, y1, z, 1.0f, color}
    };
    return IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 2, v, sizeof(TestVertex));
}

static HRESULT draw_triangle(FLOAT x0, FLOAT y0, FLOAT x1, FLOAT y1,
        FLOAT x2, FLOAT y2, FLOAT z, DWORD color, BOOL reverse)
{
    TestVertex v[3];
    v[0].x = x0; v[0].y = y0; v[0].z = z; v[0].rhw = 1.0f; v[0].color = color;
    v[1].x = reverse ? x2 : x1;
    v[1].y = reverse ? y2 : y1;
    v[1].z = z; v[1].rhw = 1.0f; v[1].color = color;
    v[2].x = reverse ? x1 : x2;
    v[2].y = reverse ? y1 : y2;
    v[2].z = z; v[2].rhw = 1.0f; v[2].color = color;
    return IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 1, v, sizeof(TestVertex));
}

static HRESULT draw_clip_quad(DWORD color)
{
    ClipVertex v[6] =
    {
        {-1.0f,  1.0f, 0.5f, color}, { 1.0f,  1.0f, 0.5f, color},
        { 1.0f, -1.0f, 0.5f, color}, {-1.0f,  1.0f, 0.5f, color},
        { 1.0f, -1.0f, 0.5f, color}, {-1.0f, -1.0f, 0.5f, color}
    };
    return IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 2, v, sizeof(ClipVertex));
}

static HRESULT draw_diamond(FLOAT cx, FLOAT cy, FLOAT rx, FLOAT ry,
        FLOAT z, DWORD color)
{
    TestVertex v[6] =
    {
        {cx, cy - ry, z, 1.0f, color}, {cx + rx, cy, z, 1.0f, color},
        {cx, cy + ry, z, 1.0f, color}, {cx, cy - ry, z, 1.0f, color},
        {cx, cy + ry, z, 1.0f, color}, {cx - rx, cy, z, 1.0f, color}
    };
    return IDirect3DDevice8_DrawPrimitiveUP(g_device,
            D3DPT_TRIANGLELIST, 2, v, sizeof(TestVertex));
}

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS pp;
    D3DCAPS8 caps;
    HRESULT hr;

    begin_stage("01 Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) return fail_stage("Direct3DCreate8 returned NULL", E_FAIL);
    begin_stage("02 GetAdapterDisplayMode");
    hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
    if (FAILED(hr)) return fail_stage("GetAdapterDisplayMode", hr);
    begin_stage("03 GetDeviceCaps raster/stencil");
    ZeroMemory(&caps, sizeof(caps));
    hr = IDirect3D8_GetDeviceCaps(g_d3d, 0, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) return fail_stage("GetDeviceCaps", hr);
    if (!(caps.RasterCaps & D3DPRASTERCAPS_ZBIAS)
            || !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE)
            || (caps.StencilCaps & (D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_REPLACE))
                    != (D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_REPLACE))
        return fail_stage("caps missing ZBIAS/color-write/stencil", E_FAIL);
    begin_stage("04 CheckDeviceFormat D24S8");
    hr = IDirect3D8_CheckDeviceFormat(g_d3d, 0, D3DDEVTYPE_HAL,
            mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
            D3DFMT_D24S8);
    if (FAILED(hr)) return fail_stage("CheckDeviceFormat(D24S8)", hr);

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
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    begin_stage("05 CreateDevice D24S8");
    hr = IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    return FAILED(hr) ? fail_stage("CreateDevice", hr) : hr;
}

static HRESULT configure_fixed_pipeline(void)
{
    D3DMATRIX identity;
    HRESULT hr;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = 1.0f;
    identity._22 = 1.0f;
    identity._33 = 1.0f;
    identity._44 = 1.0f;
#define CALL(label, call) do { begin_stage(label); hr = (call); \
    if (FAILED(hr)) return fail_stage(label, hr); } while (0)
    CALL("06 SetVertexShader XYZRHW", IDirect3DDevice8_SetVertexShader(
            g_device, TEST_FVF));
    CALL("07 LIGHTING false", IDirect3DDevice8_SetRenderState(
            g_device, D3DRS_LIGHTING, FALSE));
    CALL("08 stage0 COLOR SELECT DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    CALL("09 stage0 COLORARG1 DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    CALL("10 stage0 ALPHA SELECT DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    CALL("11 stage0 ALPHAARG1 DIFFUSE",
            IDirect3DDevice8_SetTextureStageState(g_device, 0,
            D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
    CALL("12 disable stage1", IDirect3DDevice8_SetTextureStageState(
            g_device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE));
    CALL("12a identity WORLD", IDirect3DDevice8_SetTransform(
            g_device, D3DTS_WORLD, &identity));
    CALL("12b identity VIEW", IDirect3DDevice8_SetTransform(
            g_device, D3DTS_VIEW, &identity));
    CALL("12c identity PROJECTION", IDirect3DDevice8_SetTransform(
            g_device, D3DTS_PROJECTION, &identity));
#undef CALL
    return D3D_OK;
}

static HRESULT render(void)
{
    D3DVIEWPORT8 full = {0, 0, TEST_WIDTH, TEST_HEIGHT, 0.0f, 1.0f};
    D3DVIEWPORT8 clipped = {300, 45, 220, 205, 0.0f, 1.0f};
    HRESULT hr;
#define DRAW(label, call) do { begin_stage(label); hr = (call); \
    if (FAILED(hr)) goto draw_failed; } while (0)

    begin_stage("13 Clear target+depth+stencil");
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
            D3DCOLOR_XRGB(3, 6, 18), 1.0f, 0);
    if (FAILED(hr)) return fail_stage("Clear", hr);
    begin_stage("14 BeginScene");
    hr = IDirect3DDevice8_BeginScene(g_device);
    if (FAILED(hr)) return fail_stage("BeginScene", hr);
    hr = set_rs(D3DRS_ZENABLE, FALSE, "15 ZENABLE false for raster panels");
    if (FAILED(hr)) goto draw_failed;

    /* Opposite cull modes must leave opposite winding colours visible. */
    hr = set_rs(D3DRS_CULLMODE, D3DCULL_CCW, "16 CULL CCW");
    if (FAILED(hr)) goto draw_failed;
    DRAW("17 cull-left forward green", draw_triangle(55, 70, 135, 225,
            215, 70, 0.5f, D3DCOLOR_XRGB(32, 255, 80), FALSE));
    DRAW("18 cull-left reverse red", draw_triangle(55, 70, 135, 225,
            215, 70, 0.5f, D3DCOLOR_XRGB(255, 48, 48), TRUE));
    hr = set_rs(D3DRS_CULLMODE, D3DCULL_CW, "19 CULL CW");
    if (FAILED(hr)) goto draw_failed;
    DRAW("20 cull-right forward green", draw_triangle(70, 95, 135, 210,
            200, 95, 0.4f, D3DCOLOR_XRGB(32, 255, 80), FALSE));
    DRAW("21 cull-right reverse red", draw_triangle(70, 95, 135, 210,
            200, 95, 0.4f, D3DCOLOR_XRGB(255, 48, 48), TRUE));
    hr = set_rs(D3DRS_CULLMODE, D3DCULL_NONE, "22 CULL NONE");
    if (FAILED(hr)) goto draw_failed;

    begin_stage("23 SetViewport clipped panel");
    hr = IDirect3DDevice8_SetViewport(g_device, &clipped);
    if (FAILED(hr)) goto draw_failed;
    begin_stage("23a switch to untransformed XYZ for viewport");
    hr = IDirect3DDevice8_SetVertexShader(g_device, CLIP_FVF);
    if (FAILED(hr)) goto draw_failed;
    DRAW("24 draw NDC quad through viewport",
            draw_clip_quad(D3DCOLOR_XRGB(32, 210, 255)));
    begin_stage("24a restore XYZRHW vertex format");
    hr = IDirect3DDevice8_SetVertexShader(g_device, TEST_FVF);
    if (FAILED(hr)) goto draw_failed;
    begin_stage("25 restore full viewport");
    hr = IDirect3DDevice8_SetViewport(g_device, &full);
    if (FAILED(hr)) goto draw_failed;

    /* Three writes over black isolate R, G, and B channel masks. */
    DRAW("26 colour-mask panel background", draw_quad(580, 45, 820, 250,
            0.5f, D3DCOLOR_XRGB(0, 0, 0)));
    hr = set_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED,
            "27 COLORWRITE red only");
    if (FAILED(hr)) goto draw_failed;
    DRAW("28 red write stripe", draw_quad(595, 65, 655, 230, 0.4f, 0xffffffff));
    hr = set_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_GREEN,
            "29 COLORWRITE green only");
    if (FAILED(hr)) goto draw_failed;
    DRAW("30 green write stripe", draw_quad(670, 65, 730, 230, 0.4f, 0xffffffff));
    hr = set_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_BLUE,
            "31 COLORWRITE blue only");
    if (FAILED(hr)) goto draw_failed;
    DRAW("32 blue write stripe", draw_quad(745, 65, 805, 230, 0.4f, 0xffffffff));
    hr = set_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED
            | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE
            | D3DCOLORWRITEENABLE_ALPHA, "33 restore RGBA writes");
    if (FAILED(hr)) goto draw_failed;

    /* With LESS, the coplanar green probe fails without ZBIAS and passes with it. */
    hr = set_rs(D3DRS_ZENABLE, TRUE, "34 ZENABLE true");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_ZWRITEENABLE, TRUE, "35 ZWRITE true");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_ZFUNC, D3DCMP_LESS, "36 ZFUNC LESS");
    if (FAILED(hr)) goto draw_failed;
    DRAW("37 depth-bias bases", draw_quad(45, 340, 135, 535, 0.5f,
            D3DCOLOR_XRGB(255, 48, 48)));
    DRAW("38 depth-bias second base", draw_quad(160, 340, 250, 535, 0.5f,
            D3DCOLOR_XRGB(255, 48, 48)));
    DRAW("39 coplanar no-bias probe", draw_quad(55, 350, 125, 525, 0.5f,
            D3DCOLOR_XRGB(32, 255, 80)));
    hr = set_rs(D3DRS_ZBIAS, 1, "40 ZBIAS one");
    if (FAILED(hr)) goto draw_failed;
    DRAW("41 coplanar biased probe", draw_quad(170, 350, 240, 525, 0.5f,
            D3DCOLOR_XRGB(32, 255, 80)));
    hr = set_rs(D3DRS_ZBIAS, 0, "42 ZBIAS zero");
    if (FAILED(hr)) goto draw_failed;

    /* Pass 1 writes 0xA through a 0x0F mask; passes 2/3 colour outside/inside. */
    hr = set_rs(D3DRS_ZENABLE, FALSE, "43 stencil Z disabled");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILENABLE, TRUE, "44 STENCILENABLE true");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, "45 STENCILFUNC ALWAYS");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILREF, 0x0a, "46 STENCILREF 0xA");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILMASK, 0x0f, "47 STENCILMASK 0xF");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILWRITEMASK, 0x0f, "48 STENCILWRITEMASK 0xF");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP, "49 STENCILFAIL KEEP");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP, "50 STENCILZFAIL KEEP");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE, "51 STENCILPASS REPLACE");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_COLORWRITEENABLE, 0, "52 disable colour for stencil write");
    if (FAILED(hr)) goto draw_failed;
    DRAW("53 stencil diamond write", draw_diamond(410, 435, 90, 100, 0.5f, 0xffffffff));
    hr = set_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED
            | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE
            | D3DCOLORWRITEENABLE_ALPHA, "54 restore colour after stencil write");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILWRITEMASK, 0, "55 disable stencil writes");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP, "56 STENCILPASS KEEP");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_STENCILFUNC, D3DCMP_NOTEQUAL, "57 STENCILFUNC NOTEQUAL");
    if (FAILED(hr)) goto draw_failed;
    DRAW("58 stencil outside red", draw_quad(300, 320, 520, 550, 0.6f,
            D3DCOLOR_XRGB(160, 24, 48)));
    hr = set_rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL, "59 STENCILFUNC EQUAL");
    if (FAILED(hr)) goto draw_failed;
    DRAW("60 stencil inside green", draw_quad(300, 320, 520, 550, 0.6f,
            D3DCOLOR_XRGB(32, 240, 96)));
    hr = set_rs(D3DRS_STENCILENABLE, FALSE, "61 STENCILENABLE false");
    if (FAILED(hr)) goto draw_failed;

    /* Yellow means the near blue did not write Z; blue means it did. */
    hr = set_rs(D3DRS_ZENABLE, TRUE, "62 depth-write Z enabled");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_ZFUNC, D3DCMP_LESS, "63 depth-write ZFUNC LESS");
    if (FAILED(hr)) goto draw_failed;
    hr = set_rs(D3DRS_ZWRITEENABLE, FALSE, "64 ZWRITE false");
    if (FAILED(hr)) goto draw_failed;
    DRAW("65 no-write near blue", draw_quad(580, 340, 685, 535, 0.2f,
            D3DCOLOR_XRGB(32, 96, 255)));
    hr = set_rs(D3DRS_ZWRITEENABLE, TRUE, "66 ZWRITE true");
    if (FAILED(hr)) goto draw_failed;
    DRAW("67 no-write far yellow", draw_quad(590, 350, 675, 525, 0.7f,
            D3DCOLOR_XRGB(255, 224, 32)));
    DRAW("68 write near blue", draw_quad(710, 340, 815, 535, 0.2f,
            D3DCOLOR_XRGB(32, 96, 255)));
    DRAW("69 write far yellow rejected", draw_quad(720, 350, 805, 525, 0.7f,
            D3DCOLOR_XRGB(255, 224, 32)));

    begin_stage("70 EndScene");
    hr = IDirect3DDevice8_EndScene(g_device);
    if (FAILED(hr)) return fail_stage("EndScene", hr);
    begin_stage("71 Present");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return fail_stage("Present", hr);
    SetWindowTextA(g_window, "D3D8 raster/stencil: Present S_OK - "
            "cull | viewport | RGB mask / ZBIAS | stencil | ZWRITE (0x00000000)");
    return hr;

draw_failed:
    fail_stage(g_stage, hr);
    IDirect3DDevice8_EndScene(g_device);
    return hr;
#undef DRAW
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    (void)wparam; (void)lparam;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    RECT rect = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    MSG message;
    HRESULT hr;
    (void)previous; (void)command_line;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = g_class_name;
    if (!RegisterClassA(&wc)) return 1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_class_name, "D3D8 raster/stencil: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 2;
    ShowWindow(g_window, show_command);
    UpdateWindow(g_window);
    hr = create_device(g_window);
    if (SUCCEEDED(hr)) hr = configure_fixed_pipeline();
    if (SUCCEEDED(hr)) hr = render();
    if (FAILED(hr))
        MessageBoxA(g_window, "The D3D8 raster/stencil test failed. "
                "Check the window title for the last call.",
                "D3D8 raster/stencil test", MB_OK | MB_ICONERROR);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    release_all();
    return FAILED(hr) ? 3 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleA(NULL), NULL,
            GetCommandLineA(), SW_SHOWDEFAULT));
}
