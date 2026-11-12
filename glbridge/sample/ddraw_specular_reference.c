/* Independent system D3D7 / legacy D3D3 RGB ProcessVertices reference. */
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

struct input { float p[3], n[3]; DWORD diffuse, specular; };
struct output { float p[4]; DWORD diffuse, specular; };
static void surface_desc(DDSURFACEDESC2 *d)
{
    ZeroMemory(d,sizeof(*d));d->dwSize=sizeof(*d);d->dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d->dwWidth=d->dwHeight=128;d->ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY;d->ddpfPixelFormat.dwSize=sizeof(d->ddpfPixelFormat);d->ddpfPixelFormat.dwFlags=DDPF_RGB;d->ddpfPixelFormat.dwRGBBitCount=32;d->ddpfPixelFormat.dwRBitMask=0xff0000;d->ddpfPixelFormat.dwGBitMask=0xff00;d->ddpfPixelFormat.dwBBitMask=0xff;
}
static D3DLIGHT7 light7(unsigned kind)
{
    D3DLIGHT7 l;ZeroMemory(&l,sizeof(l));l.dltType=kind;l.dcvSpecular.r=l.dcvSpecular.g=l.dcvSpecular.b=1;l.dvDirection.z=1;l.dvRange=100;l.dvAttenuation2=1;l.dvFalloff=1;l.dvTheta=.261799388f;l.dvPhi=1.04719755f;return l;
}
static BOOL reference_grid(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreate");IDirectDraw *root;IDirectDraw4 *dd;IDirect3D3 *d3d;IDirectDrawSurface4 *rt;IDirect3DDevice3 *dev;IDirect3DViewport3 *vp;IDirect3DMaterial3 *mat;IDirect3DLight *light;IDirect3DVertexBuffer *src,*dst;void *data;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL material;D3DMATERIALHANDLE handle;
    TRY(create(NULL,&root,NULL));TRY(IDirectDraw_QueryInterface(root,&IID_IDirectDraw4,(void**)&dd));TRY(IDirectDraw_QueryInterface(root,&IID_IDirect3D3,(void**)&d3d));TRY(IDirectDraw4_SetCooperativeLevel(dd,window,DDSCL_NORMAL));surface_desc(&desc);TRY(IDirectDraw4_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev,NULL));TRY(IDirect3D3_CreateViewport(d3d,&vp,NULL));TRY(IDirect3DDevice3_AddViewport(dev,vp));D3DVIEWPORT2 viewport;ZeroMemory(&viewport,sizeof(viewport));viewport.dwSize=sizeof(viewport);viewport.dwWidth=viewport.dwHeight=128;viewport.dvClipX=-1;viewport.dvClipY=1;viewport.dvClipWidth=viewport.dvClipHeight=2;viewport.dvMaxZ=1;TRY(IDirect3DViewport3_SetViewport2(vp,&viewport));TRY(IDirect3DDevice3_SetCurrentViewport(dev,vp));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3D3_CreateLight(d3d,&light,NULL));TRY(IDirect3DViewport3_AddLight(vp,light));TRY(IDirect3D3_CreateMaterial(d3d,&mat,NULL));TRY(IDirect3DMaterial3_GetHandle(mat,dev,&handle));TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,handle));
    D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL,9};TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&src,0,NULL));TRY(IDirect3DVertexBuffer_Lock(src,0,&data,NULL));FillMemory(data,9*24,0);TRY(IDirect3DVertexBuffer_Unlock(src));vd.dwFVF=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR;TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&dst,0,NULL));
    ZeroMemory(&material,sizeof(material));material.dwSize=sizeof(material);material.diffuse.r=material.diffuse.g=material.diffuse.b=material.diffuse.a=1;material.specular.r=material.specular.g=material.specular.b=1;material.specular.a=.5f;


 const DWORD lock_flags[4][2]={{0,0},{DDLOCK_READONLY,0},{0,DDLOCK_READONLY},{DDLOCK_READONLY,DDLOCK_READONLY}};for(unsigned combination=0;combination<4;++combination){void*one=NULL,*two=NULL;HRESULT first=IDirect3DVertexBuffer_Lock(src,lock_flags[combination][0],&one,NULL),second=IDirect3DVertexBuffer_Lock(src,lock_flags[combination][1],&two,NULL);char nested[220];wsprintfA(nested,"NEST api=3 combination=%u first=%08lx second=%08lx same=%u",combination,first,second,one&&one==two);line(nested);for(unsigned unlock=0;unlock<3;++unlock){HRESULT result=IDirect3DVertexBuffer_Unlock(src);wsprintfA(nested,"RELEASE api=3 combination=%u index=%u result=%08lx",combination,unlock,result);line(nested);}}
 static const float depths[]={.25f,1,2,4},ranges[]={1,2,10,100,1000},powers_grid[]={1,5,30,32};
 for(unsigned z=0;z<4;++z){TRY(IDirect3DVertexBuffer_Lock(src,0,&data,NULL));for(unsigned i=0;i<9;++i){struct input v={{((int)(i%3)-1)*.5f,((int)(i/3)-1)*.5f,depths[z]},{0,0,-1},0,0};CopyMemory((BYTE*)data+i*24,&v,24);}TRY(IDirect3DVertexBuffer_Unlock(src));
 for(unsigned kind=1;kind<=3;++kind)for(unsigned range=0;range<5;++range)for(unsigned attenuation=0;attenuation<4;++attenuation){D3DLIGHT7 l=light7(kind);D3DLIGHT2 old;ZeroMemory(&old,sizeof(old));old.dwSize=sizeof(old);old.dltType=kind;old.dcvColor.r=old.dcvColor.g=old.dcvColor.b=1;old.dvDirection=l.dvDirection;old.dvRange=ranges[range];old.dvAttenuation0=attenuation==0||attenuation==3?1:0;old.dvAttenuation1=attenuation==1||attenuation==3?1:0;old.dvAttenuation2=attenuation==2||attenuation==3?1:0;old.dvFalloff=l.dvFalloff;old.dvTheta=l.dvTheta;old.dvPhi=l.dvPhi;old.dwFlags=D3DLIGHT_ACTIVE;TRY(IDirect3DLight_SetLight(light,(D3DLIGHT*)&old));for(unsigned power=0;power<4;++power){material.power=powers_grid[power];TRY(IDirect3DMaterial3_SetMaterial(mat,&material));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));FillMemory(data,9*sizeof(struct output),0x6d);TRY(IDirect3DVertexBuffer_Unlock(dst));TRY(IDirect3DVertexBuffer_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,9,src,0,dev,0));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));const struct output*out=data;for(unsigned i=0;i<9;++i){char t[200];wsprintfA(t,"GRID z=%u kind=%u range=%u attenuation=%u power=%u vertex=%u diffuse=%08lx specular=%08lx",z,kind,range,attenuation,power,i,out[i].diffuse,out[i].specular);line(t);}TRY(IDirect3DVertexBuffer_Unlock(dst));}}
 }
 IDirect3DVertexBuffer_Release(dst);IDirect3DVertexBuffer_Release(src);IDirect3DMaterial3_Release(mat);TRY(IDirect3DViewport3_DeleteLight(vp,light));IDirect3DLight_Release(light);TRY(IDirect3DDevice3_DeleteViewport(dev,vp));IDirect3DViewport3_Release(vp);IDirect3DDevice3_Release(dev);IDirectDrawSurface4_Release(rt);IDirect3D3_Release(d3d);IDirectDraw4_Release(dd);IDirectDraw_Release(root);line("COMPLETE specular=1");return TRUE;
}
void WINAPI WinMainCRTStartup(void){char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"SPECREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);HWND window=CreateWindowExA(0,"STATIC","Native specular grid",WS_OVERLAPPEDWINDOW,0,0,128,128,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL ok=lib&&reference_grid(lib,window);if(!ok)line("COMPLETE specular=0");DestroyWindow(window);if(lib)FreeLibrary(lib);CloseHandle(logfile);ExitProcess(ok?0:1);}
