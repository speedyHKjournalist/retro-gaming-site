/* XP gamma query, recording, Reset and presentation probe; no native HAL claim. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_GAMMA_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) {
        report(message);
        ExitProcess(1);
    }
}

static WORD value(UINT mode, UINT channel, UINT i)
{
    if(!mode) return (WORD)(i*257u);
    if(mode==1) return (WORD)(channel==0?i*i*257u/255u:channel==1?i*128u:i*257u);
    return (WORD)(channel==0?i*257u:channel==1?(i*384u>65535u?65535u:i*384u):i*64u);
}
static D3DGAMMARAMP input,output;
static void set(UINT mode,DWORD flags)
{
    for(UINT i=0;i<256;i++) {input.red[i]=value(mode,0,i);input.green[i]=value(mode,1,i);input.blue[i]=value(mode,2,i);}
    DEV(SetGammaRamp)(device,
#if D9WG_GAMMA_API == 9
        0,
#endif
        flags,&input);
    ZeroMemory(&input,sizeof(input));
}
static void get(UINT mode)
{
    DEV(GetGammaRamp)(device,
#if D9WG_GAMMA_API == 9
        0,
#endif
        &output);
    for(UINT i=0;i<256;i++) check(output.red[i]==value(mode,0,i)&&output.green[i]==value(mode,1,i)&&output.blue[i]==value(mode,2,i),"FAIL gamma query");
}
static void frame(UINT scene,UINT mode,BOOL windowed)
{
    check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,0xff80a0c0u,1,0)==D3D_OK,"FAIL clear");
    check(DEV(Present)(device,NULL,NULL,NULL,NULL)==D3D_OK,"FAIL present");
    char line[100];wsprintfA(line,"GAMMA %u %u %u %u FF80A0C0",D9WG_GAMMA_API,scene,mode,(UINT)windowed);report(line);
}
static void gamma_cases(D3DPRESENT_PARAMETERS* pp)
{
    get(0);frame(0,0,TRUE);
    set(1,1);get(1);frame(1,1,TRUE);
    check(DEV(BeginStateBlock)(device)==D3D_OK,"FAIL recording begin");
    set(2,0);get(2);
#if D9WG_GAMMA_API == 8
    DWORD block=0;
#else
    IDirect3DStateBlock9* block=NULL;
#endif
    check(DEV(EndStateBlock)(device,&block)==D3D_OK,"FAIL recording end");
    set(1,1);
#if D9WG_GAMMA_API == 8
    check(DEV(ApplyStateBlock)(device,block)==D3D_OK,"FAIL apply");
    check(DEV(DeleteStateBlock)(device,block)==D3D_OK,"FAIL delete block");
#else
    check(IDirect3DStateBlock9_Apply(block)==D3D_OK,"FAIL apply");IDirect3DStateBlock9_Release(block);
#endif
    get(1);frame(2,1,TRUE);
    pp->Windowed=FALSE;
    check(DEV(Reset)(device,pp)==D3D_OK,"FAIL fullscreen reset");get(1);frame(3,1,FALSE);
    set(0,0);get(0);frame(4,0,FALSE);
    pp->Windowed=TRUE;
    check(DEV(Reset)(device,pp)==D3D_OK,"FAIL windowed reset");get(0);frame(5,0,TRUE);
    set(2,0);get(2);frame(6,2,TRUE);
    DEV(SetGammaRamp)(device,
#if D9WG_GAMMA_API == 9
        0,
#endif
        1,NULL);
    DEV(GetGammaRamp)(device,
#if D9WG_GAMMA_API == 9
        0,
#endif
        NULL);
    get(2);frame(7,2,TRUE);
    report("CHECK identity, exact RGB, live recording, flags and gamma Reset replay");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "GAMMA%u.TXT",D9WG_GAMMA_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = {0,0,64,64};
    check(AdjustWindowRectEx(&client,WS_POPUP,FALSE,0),"FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D gamma probe", WS_POPUP, 30, 40, client.right-client.left, client.bottom-client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;check(GetClientRect(window,&actual)&&actual.right==64&&actual.bottom==64,"FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_GAMMA_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_GAMMA_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_GAMMA_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    gamma_cases(&pp);
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS gamma API=%u", D9WG_GAMMA_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
