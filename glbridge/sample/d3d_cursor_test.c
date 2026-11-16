/* XP application cursor API and presentation probe; no native HAL claim. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_CURSOR_API == 8
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

#if D9WG_CURSOR_API == 8
typedef IDirect3DSurface8 Surface;
typedef IDirect3DCubeTexture8 Cube;
#define SURFACE(method) IDirect3DSurface8_##method
#define CUBE(method) IDirect3DCubeTexture8_##method
#else
typedef IDirect3DSurface9 Surface;
typedef IDirect3DCubeTexture9 Cube;
#define SURFACE(method) IDirect3DSurface9_##method
#define CUBE(method) IDirect3DCubeTexture9_##method
#endif
static HWND probe_window;
static Surface* image(UINT width,UINT height,D3DFORMAT format)
{
    Surface* out=NULL;
#if D9WG_CURSOR_API == 8
    check(DEV(CreateImageSurface)(device,width,height,format,&out)==D3D_OK,"FAIL image8");
#else
    check(DEV(CreateOffscreenPlainSurface)(device,width,height,format,D3DPOOL_SYSTEMMEM,&out,NULL)==D3D_OK,"FAIL image9");
#endif
    return out;
}
static DWORD pixel(UINT x,UINT y)
{
    if(y<16) return x<16?0xffff0000u:0x8000ff00u;
    return x<16?0x000000ffu:0xffffffffu;
}
static void position(int x,int y,BOOL direct)
{
    POINT point={x,y};check(ClientToScreen(probe_window,&point),"FAIL cursor coordinate conversion");
    if(direct) check(SetCursorPos(point.x,point.y),"FAIL OS cursor move");
    else DEV(SetCursorPosition)(device,point.x,point.y,1);
}
static void frame(UINT scene)
{
    check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,0xff204060u,1,0)==D3D_OK,"FAIL clear");
    check(DEV(Present)(device,NULL,NULL,NULL,NULL)==D3D_OK,"FAIL present");
    char line[70];wsprintfA(line,"CURSOR %u %u",D9WG_CURSOR_API,scene);report(line);
}
static void cursor_cases(void)
{
#if D9WG_CURSOR_API == 8
    D3DCAPS8 caps;
#else
    D3DCAPS9 caps;
#endif
    check(DEV(GetDeviceCaps)(device,&caps)==D3D_OK&&(caps.CursorCaps&D3DCURSORCAPS_COLOR),"FAIL cursor capability");
    check(!DEV(ShowCursor)(device,TRUE)&&!DEV(ShowCursor)(device,TRUE),"FAIL enabled cursor before image");
    check(DEV(SetCursorProperties)(device,0,0,NULL)==D3DERR_INVALIDCALL,"FAIL NULL cursor accepted");
    frame(0);
    Surface* bitmap=image(32,32,D3DFMT_A8R8G8B8);
    D3DLOCKED_RECT lock;
    check(SURFACE(LockRect)(bitmap,&lock,NULL,0)==D3D_OK,"FAIL cursor lock");
    for(UINT y=0;y<32;y++)for(UINT x=0;x<32;x++)((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]=pixel(x,y);
    check(SURFACE(UnlockRect)(bitmap)==D3D_OK,"FAIL cursor unlock");
    check(DEV(SetCursorProperties)(device,3,5,bitmap)==D3D_OK,"FAIL cursor properties");
    check(SURFACE(Release)(bitmap)==0,"FAIL retained CPU cursor source");
    check(!DEV(ShowCursor)(device,TRUE)&&DEV(ShowCursor)(device,TRUE),"FAIL ShowCursor previous state");
    position(24,24,FALSE);frame(1);
    position(40,30,TRUE);frame(2);
    check(DEV(ShowCursor)(device,FALSE)&&!DEV(ShowCursor)(device,FALSE),"FAIL hide previous state");
    position(20,20,FALSE);frame(3);
    check(!DEV(ShowCursor)(device,TRUE),"FAIL show after hide");position(-3,4,FALSE);frame(4);
    Surface* back=NULL;
#if D9WG_CURSOR_API == 8
    check(DEV(GetRenderTarget)(device,&back)==D3D_OK,"FAIL back8");
#else
    check(DEV(GetRenderTarget)(device,0,&back)==D3D_OK,"FAIL back9");
#endif
    Cube* cube=NULL;
    check(DEV(CreateCubeTexture)(device,64,2,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&cube
#if D9WG_CURSOR_API == 9
        ,NULL
#endif
    )==D3D_OK,"FAIL cube source");
    Surface* face=NULL;
    check(CUBE(GetCubeMapSurface)(cube,D3DCUBEMAP_FACE_POSITIVE_Z,1,&face)==D3D_OK,"FAIL cube view");
#if D9WG_CURSOR_API == 8
    check(DEV(SetRenderTarget)(device,face,NULL)==D3D_OK,"FAIL cube target8");
#else
    check(DEV(SetRenderTarget)(device,0,face)==D3D_OK,"FAIL cube target9");
#endif
    check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,0xffff00ffu,1,0)==D3D_OK,"FAIL GPU cursor pixels");
    check(DEV(SetCursorProperties)(device,1,2,face)==D3D_OK,"FAIL GPU cursor source");
#if D9WG_CURSOR_API == 8
    check(DEV(SetRenderTarget)(device,back,NULL)==D3D_OK,"FAIL restore back8");
#else
    check(DEV(SetRenderTarget)(device,0,back)==D3D_OK,"FAIL restore back9");
#endif
    SURFACE(Release)(face);SURFACE(Release)(back);check(CUBE(Release)(cube)==0,"FAIL retained GPU cursor source");
    position(5,42,FALSE);frame(5);position(45,45,TRUE);frame(6);
    check(DEV(ShowCursor)(device,FALSE),"FAIL final hide");frame(7);
    Surface* invalid=image(3,2,D3DFMT_A8R8G8B8);
    check(DEV(SetCursorProperties)(device,0,0,invalid)==D3DERR_INVALIDCALL,"FAIL non-power cursor accepted");SURFACE(Release)(invalid);
    invalid=image(32,32,D3DFMT_X8R8G8B8);
    check(DEV(SetCursorProperties)(device,0,0,invalid)==D3DERR_INVALIDCALL,"FAIL X8 cursor accepted");SURFACE(Release)(invalid);
    report("CHECK hidden-before-image, previous BOOL, source release, hardware OS motion, clipping and actual cube readback");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "CUR%u.TXT",D9WG_CURSOR_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = {0,0,64,64};
    check(AdjustWindowRectEx(&client,WS_POPUP,FALSE,0),"FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D cursor probe", WS_POPUP, 30, 40, client.right-client.left, client.bottom-client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;check(GetClientRect(window,&actual)&&actual.right==64&&actual.bottom==64,"FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_CURSOR_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_CURSOR_API);
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
#if D9WG_CURSOR_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    probe_window=window;cursor_cases();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS cursor API=%u", D9WG_CURSOR_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
