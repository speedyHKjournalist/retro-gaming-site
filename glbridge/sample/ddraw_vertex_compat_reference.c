/* Independent original system D3D3/7 RGB normal-transform reference. */
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
            return FALSE;    \
    } while (0)

static void surface_desc(DDSURFACEDESC2 *d)
{
    ZeroMemory(d,sizeof(*d));d->dwSize=sizeof(*d);d->dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d->dwWidth=d->dwHeight=128;d->ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY;d->ddpfPixelFormat.dwSize=sizeof(d->ddpfPixelFormat);d->ddpfPixelFormat.dwFlags=DDPF_RGB;d->ddpfPixelFormat.dwRGBBitCount=32;d->ddpfPixelFormat.dwRBitMask=0xff0000;d->ddpfPixelFormat.dwGBitMask=0xff00;d->ddpfPixelFormat.dwBBitMask=0xff;
}



static BOOL reference(HMODULE lib,HWND window){HRESULT(WINAPI*create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreateEx");IDirectDraw7*dd;IDirect3D7*d3d;IDirectDrawSurface7*rt;IDirect3DDevice7*dev;IDirect3DVertexBuffer7*src,*dst;
 TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d));DDSURFACEDESC2 desc;surface_desc(&desc);desc.dwWidth=desc.dwHeight=256;TRY(IDirectDraw7_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));D3DVIEWPORT7 vp={64,64,128,128,0,1};TRY(IDirect3DDevice7_SetViewport(dev,&vp));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_LIGHTING,FALSE));
 const float input[4][3]={{0,0,0},{1,1,1},{-1,-1,.5f},{.5f,-.5f,.25f}},iv[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},wv[16]={0,1,0,0,1,0,0,0,0,0,0,1,0,1,1,1};D3DMATRIX m;CopyMemory(&m,iv,64);for(unsigned state=1;state<=6;++state)TRY(IDirect3DDevice7_SetTransform(dev,state,&m));CopyMemory(&m,wv,64);TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_WORLD1,&m));
 D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ,4};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&src,0));vd.dwFVF=D3DFVF_XYZRHW;TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&dst,0));void*first=NULL,*second=NULL;HRESULT a=IDirect3DVertexBuffer7_Lock(src,0,&first,NULL),b=IDirect3DVertexBuffer7_Lock(src,0,&second,NULL);char t[220];wsprintfA(t,"LOCK first=%08lx second=%08lx same=%u",a,b,first&&first==second);line(t);if(SUCCEEDED(a))CopyMemory(first,input,sizeof(input));for(unsigned n=0;n<3;++n){HRESULT hr=IDirect3DVertexBuffer7_Unlock(src);wsprintfA(t,"UNLOCK index=%u hr=%08lx",n,hr);line(t);}

 const DWORD lock_flags[4][2]={{0,0},{DDLOCK_READONLY,0},{0,DDLOCK_READONLY},{DDLOCK_READONLY,DDLOCK_READONLY}};for(unsigned combination=0;combination<4;++combination){void*one=NULL,*two=NULL;HRESULT first=IDirect3DVertexBuffer7_Lock(src,lock_flags[combination][0],&one,NULL),second=IDirect3DVertexBuffer7_Lock(src,lock_flags[combination][1],&two,NULL);char nested[220];wsprintfA(nested,"NEST api=7 combination=%u first=%08lx second=%08lx same=%u",combination,first,second,one&&one==two);line(nested);for(unsigned unlock=0;unlock<3;++unlock){HRESULT result=IDirect3DVertexBuffer7_Unlock(src);wsprintfA(nested,"RELEASE api=7 combination=%u index=%u result=%08lx",combination,unlock,result);line(nested);}}
 for(unsigned blends=0;blends<4;++blends){TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_VERTEXBLEND,blends));void*data;TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));FillMemory(data,64,0x6d);TRY(IDirect3DVertexBuffer7_Unlock(dst));HRESULT hr=IDirect3DVertexBuffer7_ProcessVertices(dst,D3DVOP_TRANSFORM,0,4,src,0,dev,0);wsprintfA(t,"BLEND weights=%u hr=%08lx",blends,hr);line(t);TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));const DWORD*out=data;for(unsigned n=0;n<4;++n){wsprintfA(t,"VERTEX weights=%u vertex=%u %08lx %08lx %08lx %08lx",blends,n,out[n*4],out[n*4+1],out[n*4+2],out[n*4+3]);line(t);}TRY(IDirect3DVertexBuffer7_Unlock(dst));}
 IDirect3DVertexBuffer7_Release(dst);IDirect3DVertexBuffer7_Release(src);IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(rt);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);line("COMPLETE vertex=1");return TRUE;
}
void WINAPI WinMainCRTStartup(void){char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"VTXREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);HWND window=CreateWindowExA(0,"STATIC","Native vertex compatibility",WS_OVERLAPPEDWINDOW,0,0,256,256,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL ok=lib&&reference(lib,window);if(!ok)line("COMPLETE vertex=0");DestroyWindow(window);if(lib)FreeLibrary(lib);CloseHandle(logfile);ExitProcess(ok?0:1);}
