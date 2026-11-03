/* Full/partial WRITEONLY locks preserve GPU writes, through all surface APIs.
 * Injected readback replies exercise guest transport; GPU replay checks pixels. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
static HANDLE logfile;
static const GUID *ids[]={&IID_IDirectDrawSurface,&IID_IDirectDrawSurface2,&IID_IDirectDrawSurface3,&IID_IDirectDrawSurface4,&IID_IDirectDrawSurface7};
static const UINT versions[]={1,2,3,4,7};
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,HRESULT wanted,const char*s){if(hr!=wanted){char text[180];wsprintfA(text,"%s actual=%08lX expected=%08lX",s,(DWORD)hr,(DWORD)wanted);report(text);ExitProcess(1);}}
static BOOL equal(const void*a,const void*b,UINT n){const BYTE*x=a,*y=b;for(UINT i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static IDirectDrawSurface7 *surface(IDirectDraw7*root){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=d.dwHeight=4;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;d.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);d.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;d.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;IDirectDrawSurface7*s=NULL;expect(IDirectDraw7_CreateSurface(root,&d,&s,NULL),DD_OK,"FAIL surface");return s;}
static void fill(IDirectDrawSurface7*s,DWORD color){DDBLTFX fx;ZeroMemory(&fx,sizeof(fx));fx.dwSize=sizeof(fx);fx.dwFillColor=color;expect(IDirectDrawSurface7_Blt(s,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_WAIT,&fx),DD_OK,"FAIL GPU fill");}
static HRESULT lock(UINT api,IUnknown*s,RECT*r,DWORD flags,void**bits,LONG*pitch)
{
    if(api<4){DDSURFACEDESC d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=api==1?IDirectDrawSurface_Lock((IDirectDrawSurface*)s,r,&d,flags,NULL):api==2?IDirectDrawSurface2_Lock((IDirectDrawSurface2*)s,r,&d,flags,NULL):IDirectDrawSurface3_Lock((IDirectDrawSurface3*)s,r,&d,flags,NULL);if(SUCCEEDED(hr)){*bits=d.lpSurface;*pitch=d.lPitch;}return hr;}
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=api==4?IDirectDrawSurface4_Lock((IDirectDrawSurface4*)s,r,&d,flags,NULL):IDirectDrawSurface7_Lock((IDirectDrawSurface7*)s,r,&d,flags,NULL);if(SUCCEEDED(hr)){*bits=d.lpSurface;*pitch=d.lPitch;}return hr;
}
static void unlock(UINT api,IUnknown*s){HRESULT hr=api==1?IDirectDrawSurface_Unlock((IDirectDrawSurface*)s,NULL):api==2?IDirectDrawSurface2_Unlock((IDirectDrawSurface2*)s,NULL):api==3?IDirectDrawSurface3_Unlock((IDirectDrawSurface3*)s,NULL):api==4?IDirectDrawSurface4_Unlock((IDirectDrawSurface4*)s,NULL):IDirectDrawSurface7_Unlock((IDirectDrawSurface7*)s,NULL);expect(hr,DD_OK,"FAIL unlock");}
static void verify(IDirectDrawSurface7*source,IDirectDrawSurface7*probe,DWORD background,DWORD edited)
{
    expect(IDirectDrawSurface7_Blt(probe,NULL,source,NULL,DDBLT_WAIT,NULL),DD_OK,"FAIL verify copy");DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);expect(IDirectDrawSurface7_Lock(probe,NULL,&d,DDLOCK_READONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL verify lock");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)check(*(DWORD*)((BYTE*)d.lpSurface+y*d.lPitch+x*4)==(x==1&&y==1?edited:background),"FAIL preserved pixel");expect(IDirectDrawSurface7_Unlock(probe,NULL),DD_OK,"FAIL verify unlock");
}
static void cases(IDirectDraw7*root,UINT at)
{
    UINT api=versions[at];IDirectDrawSurface7*source=surface(root),*probe=surface(root);IUnknown*view=NULL;expect(IDirectDrawSurface7_QueryInterface(source,ids[at],(void**)&view),DD_OK,"FAIL surface view");
    for(UINT part=0;part<2;part++){DWORD background=0xff204060+api*256+part,edited=0xffe01020+api*16+part;fill(source,background);RECT rect={1,1,3,3};void*bits=NULL;LONG pitch=0;expect(lock(api,view,part?&rect:NULL,DDLOCK_WRITEONLY|DDLOCK_WAIT,&bits,&pitch),DD_OK,"FAIL WRITEONLY preservation");*(DWORD*)((BYTE*)bits+(part?0:pitch+4))=edited;unlock(api,view);verify(source,probe,background,edited);char text[180];wsprintfA(text,"LOCK FRAME api=%u partial=%u background=%08lX edited=%08lX preserved=15 readbacks=2",api,part,background,edited);report(text);}
    IUnknown_Release(view);check(IDirectDrawSurface7_Release(source)==0&&IDirectDrawSurface7_Release(probe)==0,"FAIL surface refs");
}
static void failures(IDirectDraw7*root)
{
    IDirectDrawSurface7*source=surface(root),*probe=surface(root);
    for(UINT mode=0;mode<3;mode++){DWORD background=0xff112233+mode,edited=0xffa02010+mode;fill(source,background);DDSURFACEDESC2 d;for(UINT i=0;i<sizeof(d);i++)((BYTE*)&d)[i]=0x5a;d.dwSize=sizeof(d);DDSURFACEDESC2 before=d;expect(IDirectDrawSurface7_Lock(source,NULL,&d,DDLOCK_WRITEONLY|DDLOCK_WAIT,NULL),DDERR_GENERIC,"FAIL bad reply refusal");check(equal(&d,&before,sizeof(d)),"FAIL bad reply changed output");void*bits=NULL;LONG pitch=0;expect(lock(7,(IUnknown*)source,NULL,DDLOCK_WRITEONLY|DDLOCK_WAIT,&bits,&pitch),DD_OK,"FAIL failed-readback retry");*(DWORD*)((BYTE*)bits+pitch+4)=edited;unlock(7,(IUnknown*)source);verify(source,probe,background,edited);char text[180];wsprintfA(text,"LOCK RETRY mode=%u background=%08lX edited=%08lX refused=1 immutable=1 readbacks=3",mode,background,edited);report(text);}
    check(IDirectDrawSurface7_Release(source)==0&&IDirectDrawSurface7_Release(probe)==0,"FAIL retry surface refs");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"LOCK.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Lock preservation",WS_POPUP,30,40,32,24,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"DDRAW.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");HRESULT(WINAPI*make)(GUID*,void**,REFIID,IUnknown*)=(void*)(uintptr_t)GetProcAddress(module,"DirectDrawCreateEx");check(make!=NULL,"FAIL export");IDirectDraw7*root=NULL;expect(make(NULL,(void**)&root,&IID_IDirectDraw7,NULL),DD_OK,"FAIL factory");expect(IDirectDraw7_SetCooperativeLevel(root,window,DDSCL_NORMAL),DD_OK,"FAIL cooperation");for(UINT i=0;i<5;i++)cases(root,i);failures(root);check(IDirectDraw7_Release(root)==0,"FAIL factory refs");FreeLibrary(module);DestroyWindow(window);report("LOCK STATUS versions=5 surfaces=12 frames=13 readbacks=29 faultRefusals=3 factoryRefs=0");report("PASS DirectDraw lock preservation");CloseHandle(logfile);ExitProcess(0);
}
