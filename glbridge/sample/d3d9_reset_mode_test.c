/* Real Win32 display/style/message Reset probe, built as ANSI and Unicode. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>

static HANDLE logfile;
static BOOL resetting;
static UINT sizes_inside, custom_inside, positions_inside, frames, successful, refused;
static IDirect3DDevice9* device;
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
        check(proc == procedures[i] && result == 0x1234, "FAIL original procedure/forwarded message");
    }
}
static void reset_to(D3DPRESENT_PARAMETERS* pp, BOOL expected)
{
    D3DPRESENT_PARAMETERS p = *pp;
    resetting = TRUE;
    HRESULT hr = IDirect3DDevice9_Reset(device, &p);
    resetting = FALSE;
    check(hr == (expected ? D3D_OK : D3DERR_INVALIDCALL), "FAIL mode Reset HRESULT");
    check(IDirect3DDevice9_TestCooperativeLevel(device) == (expected ? D3D_OK : D3DERR_DEVICENOTRESET), "FAIL mode Reset cooperative status");
    check(!sizes_inside, "FAIL Reset delivered WM_SIZE to application");
    check_procedures();
    if (expected)
        successful++;
    else {
        refused++;
        check(IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL) == D3DERR_DEVICELOST, "FAIL mode failure Present");
    }
}
static void frame(UINT phase, UINT width, UINT height, DWORD color)
{
    IDirect3DSurface9* back = NULL;
    D3DSURFACE_DESC desc;
    check(IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK && back, "FAIL Reset backbuffer");
    check(IDirect3DSurface9_GetDesc(back, &desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.Format == D3DFMT_X8R8G8B8, "FAIL Reset backbuffer dimensions");
    IDirect3DSurface9_Release(back);
    check(IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, color, 1, 0) == D3D_OK && IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL mode frame");
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
    wsprintfA(name, "MODE%u.TXT", D9WG_MODE_UNICODE);
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
    lstrcatA(file, "D3D9.DLL");
    HMODULE module = LoadLibraryA(file);
    check(module != NULL, "FAIL mode DLL");
    IDirect3D9*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate9");
    check(make != NULL, "FAIL mode factory export");
    IDirect3D9* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL mode factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = windows[0];
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, windows[0], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL mode device");
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
    check(successful == 5 && refused == 1 && frames == 6 && !sizes_inside && positions_inside && custom_inside, "FAIL mode counts/messages");
    check(IDirect3DDevice9_Release(device) == 0, "FAIL mode device refs");
    IDirect3D9_Release(factory);
    FreeLibrary(module);
    for (UINT i = 0; i < 2; i++)
        DestroyWindow(windows[i]);
    wsprintfA(name, "MODE STATUS unicode=%u accepted=5 refused=1 frames=6 sizeInside=0 customInside=%u posInside=%u restored=1", D9WG_MODE_UNICODE, custom_inside, positions_inside);
    report(name);
    report("PASS reset mode");
    CloseHandle(logfile);
    ExitProcess(0);
}
