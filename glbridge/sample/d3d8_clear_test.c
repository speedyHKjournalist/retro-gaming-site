// Minimal Direct3D 8 -> WineD3D -> v86 OpenGL bridge smoke test.
//
// This program deliberately imports d3d8.dll only. It must not call OpenGL
// directly, otherwise it would not verify WineD3D's Present -> wglSwapBuffers
// path.
//
// Build for Windows XP as documented in ../winproxy/README.md. The command
// uses a 32-bit MinGW compiler and avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480

static const char g_window_class[] = "V86GLD3D8ClearTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-smoke] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[160];

    wsprintfA(line, "[d3d8-smoke] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void set_result_title(HWND hwnd, const char *stage, HRESULT hr)
{
    char title[160];

    wsprintfA(title, "D3D8 smoke: %s (0x%08lX)",
            stage, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
}

static void release_d3d8(void)
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

static HRESULT init_d3d8(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS present_parameters;
    HRESULT hr;

    trace_text("Direct3DCreate8 begin");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d)
    {
        trace_text("Direct3DCreate8 returned NULL");
        g_failed_stage = "Direct3DCreate8 returned NULL";
        return E_FAIL;
    }
    trace_text("Direct3DCreate8 returned an interface");

    ZeroMemory(&mode, sizeof(mode));
    hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, D3DADAPTER_DEFAULT, &mode);
    trace_hresult("GetAdapterDisplayMode", hr);
    if (FAILED(hr))
    {
        g_failed_stage = "GetAdapterDisplayMode";
        return hr;
    }

    hr = IDirect3D8_CheckDeviceType(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, mode.Format, mode.Format, TRUE);
    trace_hresult("CheckDeviceType(windowed HAL)", hr);
    if (FAILED(hr))
    {
        g_failed_stage = "CheckDeviceType";
        return hr;
    }

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
    present_parameters.FullScreen_RefreshRateInHz = 0;
    present_parameters.FullScreen_PresentationInterval =
            D3DPRESENT_INTERVAL_DEFAULT;

    hr = IDirect3D8_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present_parameters, &g_device);
    trace_hresult("CreateDevice(windowed, software VP, no MSAA)", hr);
    if (FAILED(hr))
        g_failed_stage = "CreateDevice";
    return hr;
}

static HRESULT render_blue_frame(HWND hwnd)
{
    HRESULT hr;

    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 64, 255), 1.0f, 0);
    trace_hresult("Clear(blue)", hr);
    if (FAILED(hr))
    {
        g_failed_stage = "Clear";
        return hr;
    }

    hr = IDirect3DDevice8_BeginScene(g_device);
    trace_hresult("BeginScene", hr);
    if (FAILED(hr))
    {
        g_failed_stage = "BeginScene";
        return hr;
    }

    hr = IDirect3DDevice8_EndScene(g_device);
    trace_hresult("EndScene", hr);
    if (FAILED(hr))
    {
        g_failed_stage = "EndScene";
        return hr;
    }

    trace_text("Present begin");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    trace_hresult("Present", hr);
    if (SUCCEEDED(hr))
        set_result_title(hwnd, "Present S_OK - expected solid blue", hr);
    else
        g_failed_stage = "Present";

    return hr;
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
    hwnd = CreateWindowA(g_window_class, "D3D8 smoke: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            NULL, NULL, instance, NULL);
    if (!hwnd)
    {
        trace_text("CreateWindow failed");
        return 2;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    hr = init_d3d8(hwnd);
    if (SUCCEEDED(hr))
        hr = render_blue_frame(hwnd);

    if (FAILED(hr))
    {
        set_result_title(hwnd, g_failed_stage, hr);
        MessageBoxA(hwnd,
                "The D3D8 smoke test failed. Check the window title, guest "
                "debug output, and v86gl logs for the HRESULT and last call.",
                "D3D8 smoke test", MB_OK | MB_ICONERROR);
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
