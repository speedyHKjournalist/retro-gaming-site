/* Shared D3D8/9 Win32 mode, constructor and Reset probe; ANSI/Unicode. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <stdint.h>
#if D9WG_MODE_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSurface8 Surface;
#define FACTORY(method) IDirect3D8_##method
#define DEV(method) IDirect3DDevice8_##method
#define SURFACE(method) IDirect3DSurface8_##method
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSurface9 Surface;
#define FACTORY(method) IDirect3D9_##method
#define DEV(method) IDirect3DDevice9_##method
#define SURFACE(method) IDirect3DSurface9_##method
#endif

static HANDLE logfile;
static BOOL resetting;
static UINT sizes_inside, custom_inside, positions_inside, frames, successful, refused;
static Device* device;
static HWND active_device_window;
static LONG_PTR device_procedure;
static UINT ctor_accepted, ctor_refused;
static HWND windows[2];
static LONG styles[2], exstyles[2];
static LONG_PTR procedures[2];
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL good, const char* text)
{
    if (!good) {
        report(text);
        ExitProcess(1);
    }
}
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (resetting && message == WM_SIZE)
        sizes_inside++;
    if (resetting && message == WM_WINDOWPOSCHANGED) {
        positions_inside++;
#if D9WG_MODE_UNICODE
        SendMessageW(window, WM_USER + 19, 0x89, 0xab);
#else
        SendMessageA(window, WM_USER + 19, 0x89, 0xab);
#endif
    }
    if (message == WM_USER + 19) {
        if (wp != 0x89 || lp != 0xab)
            return 0;
        if (resetting)
            custom_inside++;
        return 0x1234;
    }
#if D9WG_MODE_UNICODE
    return DefWindowProcW(window, message, wp, lp);
#else
    return DefWindowProcA(window, message, wp, lp);
#endif
}
static void check_procedures(void)
{
    for (UINT i = 0; i < 2; i++) {
#if D9WG_MODE_UNICODE
        LONG_PTR proc = GetWindowLongPtrW(windows[i], GWLP_WNDPROC);
        LRESULT result = SendMessageW(windows[i], WM_USER + 19, 0x89, 0xab);
#else
        LONG_PTR proc = GetWindowLongPtrA(windows[i], GWLP_WNDPROC);
        LRESULT result = SendMessageA(windows[i], WM_USER + 19, 0x89, 0xab);
#endif
        check(proc == (device && windows[i] == active_device_window ? device_procedure : procedures[i]) && result == 0x1234, "FAIL original procedure/forwarded message");
    }
}
static void reset_to(D3DPRESENT_PARAMETERS* pp, BOOL expected)
{
    D3DPRESENT_PARAMETERS p = *pp;
    resetting = TRUE;
    HRESULT hr = DEV(Reset)(device, &p);
    resetting = FALSE;
    check(hr == (expected ? D3D_OK : D3DERR_INVALIDCALL), "FAIL mode Reset HRESULT");
    check(DEV(TestCooperativeLevel)(device) == (expected ? D3D_OK : D3DERR_DEVICENOTRESET), "FAIL mode Reset cooperative status");
    check(!sizes_inside, "FAIL Reset delivered WM_SIZE to application");
    if (expected)
        active_device_window = pp->hDeviceWindow;
    check_procedures();
    if (expected)
        successful++;
    else {
        refused++;
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3DERR_DEVICELOST, "FAIL mode failure Present");
    }
}
static void frame(UINT phase, UINT width, UINT height, DWORD color)
{
    Surface* back = NULL;
    D3DSURFACE_DESC desc;
    check(DEV(GetBackBuffer)(device,
#if D9WG_MODE_API == 9
              0,
#endif
              0, D3DBACKBUFFER_TYPE_MONO, &back)
                == D3D_OK
            && back,
        "FAIL Reset backbuffer");
    check(SURFACE(GetDesc)(back, &desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.Format == D3DFMT_X8R8G8B8, "FAIL Reset backbuffer dimensions");
    SURFACE(Release)(back);
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, color, 1, 0) == D3D_OK && DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL mode frame");
    char line[128];
    wsprintfA(line, "MODE FRAME %u width=%u height=%u color=%08lX", phase, width, height, color);
    report(line);
    frames++;
}
static void physical_mode(UINT width, UINT height)
{
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth == width && mode.dmPelsHeight == height, "FAIL physical display mode");
}
static void fullscreen_window(UINT index, UINT width, UINT height)
{
    RECT client;
    POINT origin = { 0, 0 };
    HWND window = windows[index], foreground = GetForegroundWindow();
    check(GetClientRect(window, &client) && ClientToScreen(window, &origin) && client.right == (LONG)width && client.bottom == (LONG)height && !origin.x && !origin.y, "FAIL fullscreen client geometry");
    check(IsWindowVisible(window) && !(GetWindowLongA(window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) && !(GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_CLIENTEDGE), "FAIL fullscreen styles/visibility");
    check(foreground == windows[0] || foreground == windows[1], "FAIL fullscreen foreground ownership");
    physical_mode(width, height);
}
static void size_client(HWND window, UINT width, UINT height)
{
    RECT rect = { 0, 0, (LONG)width, (LONG)height };
    check(AdjustWindowRectEx(&rect, GetWindowLongA(window, GWL_STYLE), FALSE, GetWindowLongA(window, GWL_EXSTYLE)), "FAIL client layout");
    check(SetWindowPos(window, NULL, 20, 30, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE), "FAIL client resize");
    check(GetClientRect(window, &rect) && rect.right == (LONG)width && rect.bottom == (LONG)height, "FAIL exact client size");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], file[MAX_PATH], name[224];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "M%u%u.TXT", D9WG_MODE_API, D9WG_MODE_UNICODE);
    lstrcpyA(file, directory);
    lstrcatA(file, name);
    logfile = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL mode log");
    DEVMODEA desktop;
    ZeroMemory(&desktop, sizeof(desktop));
    desktop.dmSize = sizeof(desktop);
    check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &desktop), "FAIL desktop mode");
#if D9WG_MODE_UNICODE
    WNDCLASSW cls;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"D9WGResetModeW";
    check(RegisterClassW(&cls), "FAIL class W");
    for (UINT i = 0; i < 2; i++)
        windows[i] = CreateWindowExW(WS_EX_CLIENTEDGE, cls.lpszClassName, L"Reset mode probe", WS_OVERLAPPEDWINDOW, 20, 30, 100, 100, NULL, NULL, cls.hInstance, NULL);
#else
    WNDCLASSA cls;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "D9WGResetModeA";
    check(RegisterClassA(&cls), "FAIL class A");
    for (UINT i = 0; i < 2; i++)
        windows[i] = CreateWindowExA(WS_EX_CLIENTEDGE, cls.lpszClassName, "Reset mode probe", WS_OVERLAPPEDWINDOW, 20, 30, 100, 100, NULL, NULL, cls.hInstance, NULL);
#endif
    for (UINT i = 0; i < 2; i++) {
        check(windows[i] != NULL, "FAIL mode window");
        styles[i] = GetWindowLongA(windows[i], GWL_STYLE);
        exstyles[i] = GetWindowLongA(windows[i], GWL_EXSTYLE);
#if D9WG_MODE_UNICODE
        procedures[i] = GetWindowLongPtrW(windows[i], GWLP_WNDPROC);
#else
        procedures[i] = GetWindowLongPtrA(windows[i], GWLP_WNDPROC);
#endif
        size_client(windows[i], 128, 128);
    }
    ShowWindow(windows[0], SW_SHOW);
    SetForegroundWindow(windows[0]);
    styles[0] = GetWindowLongA(windows[0], GWL_STYLE);
    exstyles[0] = GetWindowLongA(windows[0], GWL_EXSTYLE);
    lstrcpyA(file, directory);
    wsprintfA(name, "D3D%u.DLL", D9WG_MODE_API);
    lstrcatA(file, name);
    HMODULE module = LoadLibraryA(file);
    check(module != NULL, "FAIL mode DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_MODE_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL mode factory export");
    Factory* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL mode factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = windows[0];
#if D9WG_MODE_API == 8
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
#else
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
    check(FACTORY(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, windows[0], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL mode device");
    active_device_window = windows[0];
#if D9WG_MODE_UNICODE
    device_procedure = GetWindowLongPtrW(windows[0], GWLP_WNDPROC);
#else
    device_procedure = GetWindowLongPtrA(windows[0], GWLP_WNDPROC);
#endif
    frame(0, 128, 128, 0xff102030u);
    pp.Windowed = FALSE;
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    reset_to(&pp, TRUE);
    fullscreen_window(0, 640, 480);
    frame(1, 640, 480, 0xff204060u);
    D3DPRESENT_PARAMETERS bad = pp;
    bad.BackBufferWidth = bad.BackBufferHeight = 33;
    reset_to(&bad, FALSE);
    physical_mode(640, 480);
    reset_to(&pp, TRUE);
    fullscreen_window(0, 640, 480);
    frame(2, 640, 480, 0xff306090u);
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    reset_to(&pp, TRUE);
    fullscreen_window(0, 800, 600);
    frame(3, 800, 600, 0xff4080a0u);
    pp.hDeviceWindow = windows[1];
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    reset_to(&pp, TRUE);
    fullscreen_window(1, 640, 480);
    check(GetWindowLongA(windows[0], GWL_STYLE) == styles[0] && GetWindowLongA(windows[0], GWL_EXSTYLE) == exstyles[0], "FAIL old device window styles");
    frame(4, 640, 480, 0xff50a0c0u);
    pp.Windowed = TRUE;
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    reset_to(&pp, TRUE);
    physical_mode(desktop.dmPelsWidth, desktop.dmPelsHeight);
    check(GetWindowLongA(windows[1], GWL_STYLE) == styles[1] && GetWindowLongA(windows[1], GWL_EXSTYLE) == exstyles[1], "FAIL windowed styles");
    size_client(windows[1], 128, 128);
    ShowWindow(windows[1], SW_SHOW);
    SetForegroundWindow(windows[1]);
    frame(5, 128, 128, 0xff60c0e0u);
    check(DEV(Release)(device) == 0, "FAIL initial mode device refs");
    device = NULL;
    active_device_window = NULL;
    check_procedures();
    pp.Windowed = FALSE;
    pp.hDeviceWindow = windows[0];
    pp.BackBufferWidth = pp.BackBufferHeight = 33;
    Device* failed = (Device*)(uintptr_t)1;
    resetting = TRUE;
    HRESULT hr = FACTORY(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, windows[0], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &failed);
    resetting = FALSE;
    check(hr == D3DERR_INVALIDCALL && !failed, "FAIL invalid fullscreen constructor/cleanup");
    ctor_refused++;
    physical_mode(desktop.dmPelsWidth, desktop.dmPelsHeight);
    check_procedures();
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    resetting = TRUE;
    hr = FACTORY(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, windows[0], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device);
    resetting = FALSE;
    check(hr == D3D_OK && device, "FAIL fullscreen constructor");
    ctor_accepted++;
    active_device_window = windows[0];
    check_procedures();
    fullscreen_window(0, 640, 480);
    frame(6, 640, 480, 0xff70e0f0u);
    check(DEV(Release)(device) == 0, "FAIL fullscreen constructor device refs");
    device = NULL;
    active_device_window = NULL;
    physical_mode(desktop.dmPelsWidth, desktop.dmPelsHeight);
    check_procedures();
    check(successful == 5 && refused == 1 && frames == 7 && ctor_accepted == 1 && ctor_refused == 1 && !sizes_inside && positions_inside && custom_inside, "FAIL shared mode counts/messages");
    FACTORY(Release)(factory);
    FreeLibrary(module);
    for (UINT i = 0; i < 2; i++)
        DestroyWindow(windows[i]);
    wsprintfA(name, "MODE STATUS api=%u unicode=%u accepted=5 refused=1 constructors=1 ctorRefused=1 frames=7 sizeInside=0 customInside=%u posInside=%u restored=1", D9WG_MODE_API, D9WG_MODE_UNICODE, custom_inside, positions_inside);
    report(name);
    report("PASS reset mode");
    CloseHandle(logfile);
    ExitProcess(0);
}
