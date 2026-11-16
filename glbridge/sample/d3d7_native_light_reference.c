/* Original Microsoft D3D7 RGB public light-state reference, RAM tools only. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static void line(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, 0);
    WriteFile(logfile, "\r\n", 2, &n, 0);
    FlushFileBuffers(logfile);
}
static void status(const char* s, HRESULT hr)
{
    char t[160];
    wsprintfA(t, "%s hr=%08lx", s, hr);
    line(t);
}
#define TRY(x)            \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
        if (FAILED(hr))   \
            goto done;    \
    } while (0)
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH], t[200];
    GetModuleFileNameA(0, p, sizeof(p));
    int at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "CLIP.TXT");
    logfile = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    if (logfile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    line("READY native Microsoft DDraw7 light-state reference");
#ifdef D9WG_PROXY_CAPTURE
    GetModuleFileNameA(NULL, p, sizeof(p));
    at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "ddraw.dll");
#else
    GetSystemDirectoryA(p, sizeof(p));
    lstrcatA(p, "\\ddraw.dll");
#endif
    HMODULE lib = LoadLibraryA(p);
    if (!lib) {
        status("load native ddraw", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    HRESULT(WINAPI * create)(GUID*, void**, REFIID, IUnknown*)
        = (void*)GetProcAddress(lib, "DirectDrawCreateEx");
    if (!create) {
        status("native DirectDrawCreateEx export", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    IDirectDraw7* dd = 0;
    IDirect3D7* d3d = 0;
    IDirectDrawSurface7 *target = 0, *depth = 0;
    IDirect3DDevice7* dev = 0;
    HWND window = CreateWindowExA(
        0, "STATIC", "Clip reference", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, 0, 0, GetModuleHandleA(0), 0);
    TRY(create(0, (void**)&dd, &IID_IDirectDraw7, 0));
    TRY(IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
    TRY(IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void**)&d3d));
    DDSURFACEDESC2 desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = desc.dwHeight = 128;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    TRY(IDirectDraw7_CreateSurface(dd, &desc, &target, 0));
    DDSURFACEDESC2 zd;
    ZeroMemory(&zd, sizeof(zd));
    zd.dwSize = sizeof(zd);
    zd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zd.dwWidth = zd.dwHeight = 128;
    zd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
    zd.ddpfPixelFormat.dwSize = sizeof(zd.ddpfPixelFormat);
    zd.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    zd.ddpfPixelFormat.dwZBufferBitDepth = 16;
    zd.ddpfPixelFormat.dwZBitMask = 0xffff;
    TRY(IDirectDraw7_CreateSurface(dd, &zd, &depth, 0));
    TRY(IDirectDrawSurface7_AddAttachedSurface(target, depth));
    TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &dev));
    D3DDEVICEDESC7 caps;
    TRY(IDirect3DDevice7_GetCaps(dev, &caps));
    wsprintfA(t,"CAPS maxActive=%lu",caps.dwMaxActiveLights);line(t);
    const DWORD indices[]={0,7,8,37,1000};
    for(unsigned i=0;i<5;++i){
        DWORD index=indices[i];
        for(unsigned operation=0;operation<7;++operation){
            HRESULT changed=D3D_OK;
            if(operation==1||operation==6)changed=IDirect3DDevice7_LightEnable(dev,index,FALSE);
            if(operation==2)changed=IDirect3DDevice7_LightEnable(dev,index,TRUE);
            if(operation==3){D3DLIGHT7 light;ZeroMemory(&light,sizeof(light));light.dltType=D3DLIGHT_DIRECTIONAL;light.dcvDiffuse.r=.25f;light.dvDirection.z=-1;changed=IDirect3DDevice7_SetLight(dev,index,&light);}
            if(operation==4)changed=IDirect3DDevice7_LightEnable(dev,index,FALSE);
            if(operation==5)changed=IDirect3DDevice7_LightEnable(dev,index,TRUE);
            D3DLIGHT7 light;FillMemory(&light,sizeof(light),0xcc);BOOL enabled=(BOOL)0xcccccccc;
            HRESULT got=IDirect3DDevice7_GetLight(dev,index,&light),enable=IDirect3DDevice7_GetLightEnable(dev,index,&enabled);
            char text[500],word[16];wsprintfA(text,"LIGHT index=%lu op=%u changed=%08lx get=%08lx enable=%08lx enabled=%08lx words=",index,operation,changed,got,enable,enabled);
            for(unsigned j=0;j<26;++j){DWORD value;CopyMemory(&value,(BYTE*)&light+j*4,4);wsprintfA(word,j?",%08lx":"%08lx",value);lstrcatA(text,word);}line(text);
        }
        TRY(IDirect3DDevice7_LightEnable(dev,index,FALSE));
    }
    IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(depth);IDirectDrawSurface7_Release(target);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);DestroyWindow(window);FreeLibrary(lib);
    line("PASS native light reference finished");
done:
    CloseHandle(logfile);ExitProcess(0);
}
