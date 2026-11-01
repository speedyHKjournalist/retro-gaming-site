/* Build twice, D9WG_FPU_API=8/9. Real app-local COM devices; no GPU/pixel
 * assertion. Expected x87 behavior follows Windows/Wine's test_fpu_setup. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#if D9WG_FPU_API == 9
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
#define CREATE_FACTORY_NAME "Direct3DCreate9"
#define SDK_VERSION D3D_SDK_VERSION
#define CREATE_DEVICE IDirect3D9_CreateDevice
#define RELEASE_FACTORY IDirect3D9_Release
#define RELEASE_DEVICE IDirect3DDevice9_Release
#define SET_STATE IDirect3DDevice9_SetRenderState
#define RESULT_FILE "FPU9.TXT"
#else
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
#define CREATE_FACTORY_NAME "Direct3DCreate8"
#define SDK_VERSION D3D_SDK_VERSION
#define CREATE_DEVICE IDirect3D8_CreateDevice
#define RELEASE_FACTORY IDirect3D8_Release
#define RELEASE_DEVICE IDirect3DDevice8_Release
#define SET_STATE IDirect3DDevice8_SetRenderState
#define RESULT_FILE "FPU8.TXT"
#endif
static HANDLE log_file;
static Factory *factory;
static HWND window;

static WORD control(void)
{
    WORD value;
    __asm__ volatile ("fnstcw %0" : "=m" (value) : : "memory");return value;
}
static void set_control(WORD value)
{
    __asm__ volatile ("fnclex\n\tfldcw %0" : : "m" (value) : "memory");
}
static void report(const char *text)
{
    DWORD written;
    WriteFile(log_file, text, (DWORD)lstrlenA(text), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);FlushFileBuffers(log_file);
}
static void check(BOOL condition, const char *label)
{
    if (!condition) {
        char text[192];wsprintfA(text, "FAIL API %u %s cw=%04x error=%lu", D9WG_FPU_API, label, control(), GetLastError());
        report(text);set_control(0x37f);ExitProcess(1);
    }
}
static HRESULT create(UINT adapter, DWORD flags, HWND target, Device **device)
{
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));pp.BackBufferWidth = 64;pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;pp.BackBufferCount = 1;pp.hDeviceWindow = target;
    pp.Windowed = TRUE;pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    return CREATE_DEVICE(factory, adapter, D3DDEVTYPE_HAL, target,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | flags, &pp, device);
}
static DWORD WINAPI other_thread(void *unused)
{
    Device *device = NULL;
    HWND target = CreateWindowExA(0, "STATIC", "FPU thread probe", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    (void)unused;check(target != NULL, "second thread window");set_control(0xb7f);
    check(SUCCEEDED(create(0, 0, target, &device)) && device && control() == 0x7f, "new thread default");
    RELEASE_DEVICE(device);DestroyWindow(target);return 0;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH], dll[MAX_PATH];UINT at;HMODULE module;WORD saved;Device *device = NULL;
    HANDLE thread;DWORD code;
    Factory *(WINAPI *make)(UINT);
    GetModuleFileNameA(NULL, path, sizeof(path));at = (UINT)lstrlenA(path);
    while (at && path[at - 1] != '\\') --at;
    path[at] = 0;lstrcpyA(dll, path);
    lstrcatA(path, RESULT_FILE);
    log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE) ExitProcess(2);
    report("READY real XP FPU COM probe");
#if D9WG_FPU_API == 9
    lstrcatA(dll, "D3D9.DLL");
#else
    lstrcatA(dll, "D3D8.DLL");
#endif
    module = LoadLibraryA(dll);check(module != NULL, "load local DLL");
    make = (void *)(uintptr_t)GetProcAddress(module, CREATE_FACTORY_NAME);check(make != NULL, "factory export");
    factory = make(SDK_VERSION);check(factory != NULL, "factory object");
    window = CreateWindowExA(0, "STATIC", "FPU probe", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "window");saved = control();
    set_control(0xf60);
    check(SUCCEEDED(create(0, 0, window, &device)) && device && control() == 0x7f, "default masks exceptions and selects 24-bit nearest");
    set_control(0x37f);check(SUCCEEDED(SET_STATE(device, D3DRS_AMBIENT, 0x123456)) && control() == 0x37f, "later API does not reset caller changes");
    RELEASE_DEVICE(device);check(control() == 0x37f, "release preserves current control");
    report("PASS default creation and later caller changes");
    set_control(0xf60);device = NULL;
    check(SUCCEEDED(create(0, D3DCREATE_FPU_PRESERVE, window, &device)) && device && control() == 0xf60, "FPU_PRESERVE creation");
    check(SUCCEEDED(SET_STATE(device, D3DRS_AMBIENT, 0x654321)) && control() == 0xf60, "FPU_PRESERVE later call");
    RELEASE_DEVICE(device);check(control() == 0xf60, "FPU_PRESERVE release");
    report("PASS FPU_PRESERVE creation and release");
    device = (Device *)(uintptr_t)1;
    check(FAILED(create(99, 0, window, &device)) && !device && control() == 0xf60, "invalid adapter leaves control unchanged");
    thread = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);check(thread != NULL, "create second thread");
    check(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &code) && !code && control() == 0xf60, "control is per thread");
    CloseHandle(thread);report("PASS failed creation and thread isolation");
    set_control(saved);DestroyWindow(window);RELEASE_FACTORY(factory);check(FreeLibrary(module), "unload DLL");
    report("PASS all real XP FPU checks");CloseHandle(log_file);ExitProcess(0);
}
