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
static const float powers[] = {0,30,0,5,0};
static void seed_vertices(void *data, BOOL colors)
{
    for(unsigned i=0;i<9;++i){struct input v={{((int)(i%3)-1)*.5f,((int)(i/3)-1)*.5f,1},{0,0,-1},0x40ffffff,0xc0ffffff};CopyMemory((BYTE*)data+i*(colors?32:24),&v,colors?32:24);}
}
static void output_rows(unsigned api,const char *phase,unsigned kind,unsigned power,unsigned colors,unsigned cv,unsigned src,const struct output *v)
{
    char t[200];for(unsigned i=0;i<9;++i){wsprintfA(t,"PV api=%u phase=%s kind=%u power=%u colors=%u cv=%u src=%u vertex=%u diffuse=%08lx specular=%08lx",api,phase,kind,power,colors,cv,src,i,v[i].diffuse,v[i].specular);line(t);}
}
static void surface_desc(DDSURFACEDESC2 *d)
{
    ZeroMemory(d,sizeof(*d));d->dwSize=sizeof(*d);d->dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d->dwWidth=d->dwHeight=128;d->ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY;d->ddpfPixelFormat.dwSize=sizeof(d->ddpfPixelFormat);d->ddpfPixelFormat.dwFlags=DDPF_RGB;d->ddpfPixelFormat.dwRGBBitCount=32;d->ddpfPixelFormat.dwRBitMask=0xff0000;d->ddpfPixelFormat.dwGBitMask=0xff00;d->ddpfPixelFormat.dwBBitMask=0xff;
}
static D3DLIGHT7 light7(unsigned kind)
{
    D3DLIGHT7 l;ZeroMemory(&l,sizeof(l));l.dltType=kind;l.dcvSpecular.r=l.dcvSpecular.g=l.dcvSpecular.b=1;l.dvDirection.z=1;l.dvRange=100;l.dvAttenuation2=1;l.dvFalloff=1;l.dvTheta=.261799388f;l.dvPhi=1.04719755f;return l;
}
static BOOL pv7(IDirect3DVertexBuffer7 *dst,IDirect3DVertexBuffer7 *src,IDirect3DDevice7 *dev,const char *phase,unsigned kind,unsigned power,unsigned colors,unsigned cv,unsigned source)
{
    void *data;TRY(IDirect3DVertexBuffer7_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,9,src,0,dev,0));TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));output_rows(7,phase,kind,power,colors,cv,source,data);TRY(IDirect3DVertexBuffer7_Unlock(dst));return TRUE;
}
static BOOL reference7(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreateEx");IDirectDraw7 *dd;IDirect3D7 *d3d;IDirectDrawSurface7 *rt;IDirect3DDevice7 *dev;IDirect3DVertexBuffer7 *src[2],*dst;void *data;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL7 material;
    TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d));surface_desc(&desc);TRY(IDirectDraw7_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));D3DVIEWPORT7 vp={0,0,128,128,0,1};TRY(IDirect3DDevice7_SetViewport(dev,&vp));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3DDevice7_LightEnable(dev,0,TRUE));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_CLIPPING,FALSE));
    for(unsigned colors=0;colors<2;++colors){D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL|(colors?(D3DFVF_DIFFUSE|D3DFVF_SPECULAR):0),9};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&src[colors],0));TRY(IDirect3DVertexBuffer7_Lock(src[colors],0,&data,NULL));seed_vertices(data,colors);TRY(IDirect3DVertexBuffer7_Unlock(src[colors]));}
    D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR,9};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&dst,0));ZeroMemory(&material,sizeof(material));material.specular.r=material.specular.g=material.specular.b=1;material.specular.a=.5f;
    for(unsigned kind=1;kind<=3;++kind){D3DLIGHT7 l=light7(kind);TRY(IDirect3DDevice7_SetLight(dev,0,&l));for(unsigned p=0;p<5;++p){material.power=powers[p];TRY(IDirect3DDevice7_SetMaterial(dev,&material));if(!pv7(dst,src[0],dev,"sequence",kind,p,0,1,2))return FALSE;}}
    material.power=30;TRY(IDirect3DDevice7_SetMaterial(dev,&material));material.power=0;TRY(IDirect3DDevice7_SetMaterial(dev,&material));if(!pv7(dst,src[0],dev,"set_without_process",3,0,0,1,2))return FALSE;
    for(unsigned colors=0;colors<2;++colors)for(unsigned cv=0;cv<2;++cv)for(unsigned source=0;source<3;++source){TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_COLORVERTEX,cv));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_SPECULARMATERIALSOURCE,source));if(!pv7(dst,src[colors],dev,"source",3,0,colors,cv,source))return FALSE;}
    TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_COLORVERTEX,FALSE));
    IDirect3DVertexBuffer7 *other;TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&other,0));if(!pv7(other,src[0],dev,"new_destination",3,0,0,0,2))return FALSE;IDirect3DVertexBuffer7_Release(other);
    material.power=17;DWORD block;TRY(IDirect3DDevice7_BeginStateBlock(dev));TRY(IDirect3DDevice7_SetMaterial(dev,&material));if(!pv7(dst,src[0],dev,"record_during",3,0,0,0,2))return FALSE;TRY(IDirect3DDevice7_EndStateBlock(dev,&block));if(!pv7(dst,src[0],dev,"record_end",3,0,0,0,2))return FALSE;TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));if(!pv7(dst,src[0],dev,"record_apply",3,17,0,0,2))return FALSE;material.power=0;TRY(IDirect3DDevice7_SetMaterial(dev,&material));if(!pv7(dst,src[0],dev,"record_zero",3,0,0,0,2))return FALSE;TRY(IDirect3DDevice7_DeleteStateBlock(dev,block));
    IDirect3DVertexBuffer7_Release(dst);for(unsigned i=0;i<2;++i)IDirect3DVertexBuffer7_Release(src[i]);IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(rt);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);line("DONE api=7");return TRUE;
}
static BOOL reference3(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreate");IDirectDraw *root;IDirectDraw4 *dd;IDirect3D3 *d3d;IDirectDrawSurface4 *rt;IDirect3DDevice3 *dev;IDirect3DViewport3 *vp;IDirect3DMaterial3 *mat;IDirect3DLight *light;IDirect3DVertexBuffer *src,*dst;void *data;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL material;D3DMATERIALHANDLE handle;
    TRY(create(NULL,&root,NULL));TRY(IDirectDraw_QueryInterface(root,&IID_IDirectDraw4,(void**)&dd));TRY(IDirectDraw_QueryInterface(root,&IID_IDirect3D3,(void**)&d3d));TRY(IDirectDraw4_SetCooperativeLevel(dd,window,DDSCL_NORMAL));surface_desc(&desc);TRY(IDirectDraw4_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev,NULL));TRY(IDirect3D3_CreateViewport(d3d,&vp,NULL));TRY(IDirect3DDevice3_AddViewport(dev,vp));D3DVIEWPORT2 viewport;ZeroMemory(&viewport,sizeof(viewport));viewport.dwSize=sizeof(viewport);viewport.dwWidth=viewport.dwHeight=128;viewport.dvClipX=-1;viewport.dvClipY=1;viewport.dvClipWidth=viewport.dvClipHeight=2;viewport.dvMaxZ=1;TRY(IDirect3DViewport3_SetViewport2(vp,&viewport));TRY(IDirect3DDevice3_SetCurrentViewport(dev,vp));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3D3_CreateLight(d3d,&light,NULL));TRY(IDirect3DViewport3_AddLight(vp,light));TRY(IDirect3D3_CreateMaterial(d3d,&mat,NULL));TRY(IDirect3DMaterial3_GetHandle(mat,dev,&handle));TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,handle));
    D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL,9};TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&src,0,NULL));TRY(IDirect3DVertexBuffer_Lock(src,0,&data,NULL));seed_vertices(data,FALSE);TRY(IDirect3DVertexBuffer_Unlock(src));vd.dwFVF=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR;TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&dst,0,NULL));
    ZeroMemory(&material,sizeof(material));material.dwSize=sizeof(material);material.specular.r=material.specular.g=material.specular.b=1;material.specular.a=.5f;
    for(unsigned kind=1;kind<=3;++kind){D3DLIGHT7 l=light7(kind);D3DLIGHT2 old;ZeroMemory(&old,sizeof(old));old.dwSize=sizeof(old);old.dltType=kind;old.dcvColor.r=old.dcvColor.g=old.dcvColor.b=1;old.dvDirection=l.dvDirection;old.dvRange=l.dvRange;old.dvAttenuation2=l.dvAttenuation2;old.dvFalloff=l.dvFalloff;old.dvTheta=l.dvTheta;old.dvPhi=l.dvPhi;old.dwFlags=D3DLIGHT_ACTIVE;TRY(IDirect3DLight_SetLight(light,(D3DLIGHT*)&old));for(unsigned p=0;p<5;++p){material.power=powers[p];TRY(IDirect3DMaterial3_SetMaterial(mat,&material));TRY(IDirect3DVertexBuffer_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,9,src,0,dev,0));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));output_rows(3,"sequence",kind,p,0,0,0,data);TRY(IDirect3DVertexBuffer_Unlock(dst));}}
    IDirect3DVertexBuffer_Release(dst);IDirect3DVertexBuffer_Release(src);IDirect3DMaterial3_Release(mat);TRY(IDirect3DViewport3_DeleteLight(vp,light));IDirect3DLight_Release(light);TRY(IDirect3DDevice3_DeleteViewport(dev,vp));IDirect3DViewport3_Release(vp);IDirect3DDevice3_Release(dev);IDirectDrawSurface4_Release(rt);IDirect3D3_Release(d3d);IDirectDraw4_Release(dd);IDirectDraw_Release(root);line("DONE api=3");return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"LIGHTREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);if(!lib){status("load system ddraw",HRESULT_FROM_WIN32(GetLastError()));CloseHandle(logfile);ExitProcess(3);}HWND window=CreateWindowExA(0,"STATIC","Native lighting reference",WS_OVERLAPPEDWINDOW,0,0,128,128,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL seven=reference7(lib,window),three=reference3(lib,window);char t[100];wsprintfA(t,"COMPLETE api7=%u api3=%u",seven,three);line(t);line("PASS native lighting reference complete");DestroyWindow(window);FreeLibrary(lib);CloseHandle(logfile);ExitProcess(0);
}
