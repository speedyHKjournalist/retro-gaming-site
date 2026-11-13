/* Independent original XP clipping, fractional stretch and HWND coordinates. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
static HANDLE logfile;
static void line(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void status(const char*s,HRESULT hr){char b[256];wsprintfA(b,"%s hr=%08lx",s,hr);line(b);}
#define TRY(x) do{HRESULT hr=(x);status(#x,hr);if(FAILED(hr))return FALSE;}while(0)
static BOOL surface(IDirectDraw7*dd,UINT w,UINT h,IDirectDrawSurface7**out)
{
 DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=w;d.dwHeight=h;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=DDPF_RGB;d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;TRY(IDirectDraw7_CreateSurface(dd,&d,out,NULL));return TRUE;
}
static BOOL initialize(IDirectDrawSurface7*s,BOOL source)
{
 DDSURFACEDESC2 lock;ZeroMemory(&lock,sizeof(lock));lock.dwSize=sizeof(lock);TRY(IDirectDrawSurface7_Lock(s,NULL,&lock,DDLOCK_WAIT,NULL));for(UINT y=0;y<lock.dwHeight;y++)ZeroMemory((BYTE*)lock.lpSurface+y*lock.lPitch,lock.dwWidth*4);if(source){const DWORD colors[4]={0xff0000,0xff00,0xff,0xffffff};CopyMemory((BYTE*)lock.lpSurface+lock.lPitch+4,colors,16);}TRY(IDirectDrawSurface7_Unlock(s,NULL));return TRUE;
}
static BOOL samples(IDirectDrawSurface7*s,UINT test)
{
 const UINT xs[4]={16,48,80,112},ys[3]={16,56,80};DDSURFACEDESC2 lock;ZeroMemory(&lock,sizeof(lock));lock.dwSize=sizeof(lock);TRY(IDirectDrawSurface7_Lock(s,NULL,&lock,DDLOCK_READONLY|DDLOCK_WAIT,NULL));for(UINT y=0;y<3;y++)for(UINT x=0;x<4;x++){DWORD color=*(DWORD*)((BYTE*)lock.lpSurface+ys[y]*lock.lPitch+xs[x]*4);char b[180];wsprintfA(b,"PIXEL case=%u x=%u y=%u rgb=%06lx",test,xs[x],ys[y],color&0xffffff);line(b);}TRY(IDirectDrawSurface7_Unlock(s,NULL));return TRUE;
}
static BOOL reference(HMODULE lib,HWND window)
{
 HRESULT(WINAPI*create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreateEx");IDirectDraw7*dd;IDirectDrawSurface7 *src,*dst;IDirectDrawClipper *clip,*hwnd_clip;TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));if(!surface(dd,6,3,&src)||!surface(dd,128,96,&dst)||!initialize(src,TRUE))return FALSE;TRY(IDirectDraw7_CreateClipper(dd,0,&clip,NULL));
 struct{RGNDATAHEADER h;RECT r[2];}region={{sizeof(RGNDATAHEADER),RDH_RECTANGLES,2,2*sizeof(RECT),{0,0,128,96}},{{0,0,128,32},{64,32,128,96}}};TRY(IDirectDrawClipper_SetClipList(clip,(RGNDATA*)&region,0));TRY(IDirectDrawSurface7_SetClipper(dst,clip));RECT sr={1,1,5,2};for(UINT test=0;test<2;test++){if(!initialize(dst,FALSE))return FALSE;DDBLTFX fx;ZeroMemory(&fx,sizeof(fx));fx.dwSize=sizeof(fx);fx.dwDDFX=DDBLTFX_MIRRORLEFTRIGHT;HRESULT hr=IDirectDrawSurface7_Blt(dst,NULL,src,&sr,DDBLT_WAIT|(test?DDBLT_DDFX:0),test?&fx:NULL);char b[180];wsprintfA(b,"STRETCH case=%u hr=%08lx",test,hr);line(b);if(SUCCEEDED(hr)&&!samples(dst,test))return FALSE;}
 TRY(IDirectDrawSurface7_SetClipper(dst,NULL));if(!initialize(dst,FALSE))return FALSE;TRY(IDirectDraw7_CreateClipper(dd,0,&hwnd_clip,NULL));SetWindowPos(window,HWND_TOPMOST,32,48,32,16,SWP_SHOWWINDOW);UpdateWindow(window);for(UINT i=0;i<20;i++){MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(10);}RECT wr;GetClientRect(window,&wr);MapWindowPoints(window,NULL,(POINT*)&wr,2);char b[256];wsprintfA(b,"WINDOW %ld %ld %ld %ld",wr.left,wr.top,wr.right,wr.bottom);line(b);TRY(IDirectDrawClipper_SetHWnd(hwnd_clip,0,window));TRY(IDirectDrawSurface7_SetClipper(dst,hwnd_clip));DWORD bytes=0;TRY(IDirectDrawClipper_GetClipList(hwnd_clip,NULL,NULL,&bytes));BYTE buffer[512];if(bytes>sizeof(buffer))return FALSE;TRY(IDirectDrawClipper_GetClipList(hwnd_clip,NULL,(RGNDATA*)buffer,&bytes));RGNDATA*data=(RGNDATA*)buffer;for(DWORD i=0;i<data->rdh.nCount;i++){RECT*r=(RECT*)data->Buffer+i;wsprintfA(b,"REGION %lu %ld %ld %ld %ld",i,r->left,r->top,r->right,r->bottom);line(b);}DDBLTFX fx;ZeroMemory(&fx,sizeof(fx));fx.dwSize=sizeof(fx);fx.dwFillColor=0xff0000;HRESULT hr=IDirectDrawSurface7_Blt(dst,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_WAIT,&fx);wsprintfA(b,"WINDOW_FILL hr=%08lx",hr);line(b);if(SUCCEEDED(hr)&&!samples(dst,2))return FALSE;
 TRY(IDirectDrawSurface7_SetClipper(dst,NULL));IDirectDrawClipper_Release(hwnd_clip);IDirectDrawClipper_Release(clip);IDirectDrawSurface7_Release(dst);IDirectDrawSurface7_Release(src);IDirectDraw7_Release(dd);return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
 char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int n=lstrlenA(p);while(n&&p[n-1]!='\\')n--;p[n]=0;lstrcatA(p,"CLPREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);HWND window=CreateWindowExA(0,"STATIC","",WS_POPUP,0,0,128,96,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL ok=lib&&reference(lib,window);line(ok?"COMPLETE clipping=1":"COMPLETE clipping=0");DestroyWindow(window);if(lib)FreeLibrary(lib);CloseHandle(logfile);ExitProcess(ok?0:1);
}
