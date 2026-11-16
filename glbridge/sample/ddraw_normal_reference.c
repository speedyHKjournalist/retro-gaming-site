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

struct Vertex{float p[3],n[3];};
struct Output{float p[4];DWORD diffuse,specular;};
static const float worlds[][16]={
 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},
 {1,0,1,0,0,1,0,0,1,0,1,0,0,0,.5f,1},
 {0,0,1,0,0,1,0,0,-1,0,0,0,10,10,10,1},
 {1,0,0,0,0,1,0,0,0,0,1,-1,10,10,10,0},
 {2,0,0,0,0,3,0,0,0,0,.5f,0,0,0,0,1},
 {1,.25f,0,0,0,1,.5f,0,.5f,0,1,0,0,0,0,1},
 {1,.25f,0,0,0,1,.5f,0,.5f,0,1,0,0,0,0,1},
 {1,0,1,0,0,1,0,0,1,0,1,0,0,0,.5f,1},
 {1,0,1,0,0,1,0,0,1,0,1,0,0,0,.5f,1},
 {1,0,1,0,0,1,0,0,1,0,1,0,0,0,.5f,1},
 {1,0,0,0,0,1,0,0,0,0,1,-1,10,10,10,0},
 {1,0,0,0,0,1,0,0,0,0,1,-1,10,10,10,0},
 {1,.25f,0,0,0,1,.5f,0,.5f,0,1,0,0,0,0,1}
};
static void vertices_for(unsigned kind,struct Vertex *v)
{
 unsigned original=kind;kind=original>=10&&original<=11?3:original>=7&&original<=9?1:original==12?5:original;
 for(unsigned i=0;i<4;++i){float x=(i&2)?1:-1,y=(i&1)?1:-1;v[i]=(struct Vertex){{x,y,0},{0,0,-1}};
  if(kind==2){v[i].p[0]=-10;v[i].p[1]=y-10;v[i].p[2]=10-x;v[i].n[0]=-1;v[i].n[2]=0;}
  if(kind==3){v[i].p[0]=x-10;v[i].p[1]=y-10;v[i].p[2]=-10;}
  if(kind==4){v[i].p[0]=x*.5f;v[i].p[1]=y/3;v[i].n[0]=.6f;v[i].n[1]=.8f;}
  if(kind>=5){v[i].p[2]=kind==6||original==12?.25f:.5f;v[i].n[0]=.6f;v[i].n[1]=.8f;}
  if(original==7||original==10){v[i].n[0]=1;v[i].n[1]=v[i].n[2]=0;}
  if(original==8||original==11){v[i].n[1]=1;v[i].n[0]=v[i].n[2]=0;}
  if(original==9){v[i].n[0]=.6f;v[i].n[1]=.8f;v[i].n[2]=-1;}
 }
}
static void cpu_rows(unsigned api,unsigned kind,unsigned normalize,const struct Output *out)
{
 char t[180];for(unsigned i=0;i<4;++i){wsprintfA(t,"CPU api=%u matrix=%u normalize=%u vertex=%u diffuse=%08lx specular=%08lx",api,kind,normalize,i,out[i].diffuse,out[i].specular);line(t);}
}

static BOOL reset_history7(IDirect3DDevice7 **dev,IDirect3D7 *d3d,IDirectDrawSurface7 *rt,unsigned normalize)
{
 IDirect3DDevice7_Release(*dev);*dev=NULL;TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,dev));D3DVIEWPORT7 vp={0,0,128,128,0,1};TRY(IDirect3DDevice7_SetViewport(*dev,&vp));D3DMATRIX m;CopyMemory(&m,worlds[0],sizeof(m));TRY(IDirect3DDevice7_SetTransform(*dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice7_SetTransform(*dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice7_SetTransform(*dev,D3DTRANSFORMSTATE_PROJECTION,&m));D3DMATERIAL7 mat;ZeroMemory(&mat,sizeof(mat));mat.diffuse.b=mat.diffuse.a=1;mat.power=30;TRY(IDirect3DDevice7_SetMaterial(*dev,&mat));D3DLIGHT7 light;ZeroMemory(&light,sizeof(light));light.dltType=D3DLIGHT_DIRECTIONAL;light.dcvDiffuse.r=light.dcvDiffuse.g=light.dcvDiffuse.b=1;light.dvDirection.z=1;TRY(IDirect3DDevice7_SetLight(*dev,0,&light));TRY(IDirect3DDevice7_LightEnable(*dev,0,TRUE));TRY(IDirect3DDevice7_SetRenderState(*dev,D3DRENDERSTATE_SPECULARENABLE,FALSE));TRY(IDirect3DDevice7_SetRenderState(*dev,D3DRENDERSTATE_COLORVERTEX,FALSE));TRY(IDirect3DDevice7_SetRenderState(*dev,D3DRENDERSTATE_CLIPPING,FALSE));TRY(IDirect3DDevice7_SetRenderState(*dev,D3DRENDERSTATE_NORMALIZENORMALS,normalize));return TRUE;
}
static BOOL reference7(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreateEx");IDirectDraw7 *dd;IDirect3D7 *d3d;IDirectDrawSurface7 *rt;IDirect3DDevice7 *dev;IDirect3DVertexBuffer7 *src,*dst;void *data;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL7 material;
    TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d));surface_desc(&desc);TRY(IDirectDraw7_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));D3DVIEWPORT7 vp={0,0,128,128,0,1};TRY(IDirect3DDevice7_SetViewport(dev,&vp));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3DDevice7_LightEnable(dev,0,TRUE));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_CLIPPING,FALSE));

    D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL,4};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&src,0));vd.dwFVF=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR;TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&dst,0));ZeroMemory(&material,sizeof(material));material.diffuse.b=material.diffuse.a=1;material.power=30;TRY(IDirect3DDevice7_SetMaterial(dev,&material));D3DLIGHT7 l;ZeroMemory(&l,sizeof(l));l.dltType=D3DLIGHT_DIRECTIONAL;l.dcvDiffuse.r=l.dcvDiffuse.g=l.dcvDiffuse.b=1;l.dvDirection.z=1;TRY(IDirect3DDevice7_SetLight(dev,0,&l));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,FALSE));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_COLORVERTEX,FALSE));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE));
    DWORD initial;TRY(IDirect3DDevice7_GetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,&initial));char initial_text[100];wsprintfA(initial_text,"INITIAL api=7 normalize=%08lx",initial);line(initial_text);
    for(unsigned norm=0;norm<2;++norm){TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,norm));DWORD state;TRY(IDirect3DDevice7_GetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,&state));char t[180];wsprintfA(t,"STATE api=7 normalize=%u value=%08lx",norm,state);line(t);for(unsigned kind=0;kind<13;++kind){struct Vertex vertices[4];vertices_for(kind,vertices);CopyMemory(&m,worlds[0],sizeof(m));if(kind==6){m._11=3;m._22=.5f;m._33=2;}if(kind==12){m._13=.3f;m._21=.25f;m._32=.5f;}TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));CopyMemory(&m,worlds[kind],sizeof(m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DVertexBuffer7_Lock(src,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer7_Unlock(src));TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));FillMemory(data,4*sizeof(struct Output),0x6d);TRY(IDirect3DVertexBuffer7_Unlock(dst));TRY(IDirect3DDevice7_Clear(dev,0,NULL,D3DCLEAR_TARGET,0xffab4e69,0,0));TRY(IDirect3DDevice7_BeginScene(dev));TRY(IDirect3DVertexBuffer7_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,4,src,0,dev,0));TRY(IDirect3DDevice7_DrawPrimitive(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ|D3DFVF_NORMAL,vertices,4,0));TRY(IDirect3DDevice7_EndScene(dev));TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));cpu_rows(7,kind,norm,data);TRY(IDirect3DVertexBuffer7_Unlock(dst));DDSURFACEDESC2 locked;ZeroMemory(&locked,sizeof(locked));locked.dwSize=sizeof(locked);TRY(IDirectDrawSurface7_Lock(rt,NULL,&locked,DDLOCK_READONLY|DDLOCK_WAIT,NULL));DWORD pixel;CopyMemory(&pixel,(BYTE*)locked.lpSurface+64*locked.lPitch+64*4,4);TRY(IDirectDrawSurface7_Unlock(rt,NULL));wsprintfA(t,"PIXEL api=7 matrix=%u normalize=%u color=%08lx",kind,norm,pixel);line(t);}}

    for(unsigned normalize=0;normalize<2;++normalize)for(unsigned sequence=0;sequence<3;++sequence){if(!reset_history7(&dev,d3d,rt,normalize))return FALSE;for(unsigned step=0;step<3;++step){unsigned kind=step==0?5:step==2?1:sequence==0?0:sequence==1?2:4;struct Vertex vertices[4];vertices_for(kind,vertices);for(unsigned i=0;i<4;++i){vertices[i].n[0]=1;vertices[i].n[1]=vertices[i].n[2]=0;}CopyMemory(&m,worlds[kind],sizeof(m));TRY(IDirect3DDevice7_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DVertexBuffer7_Lock(src,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer7_Unlock(src));TRY(IDirect3DVertexBuffer7_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,4,src,0,dev,0));TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));char t[180];const struct Output *out=data;wsprintfA(t,"HISTORY normalize=%u sequence=%u step=%u diffuse=%08lx",normalize,sequence,step,out[0].diffuse);line(t);TRY(IDirect3DVertexBuffer7_Unlock(dst));}}
    IDirect3DVertexBuffer7_Release(dst);IDirect3DVertexBuffer7_Release(src);IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(rt);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);line("DONE api=7");return TRUE;
}
static BOOL reference3(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreate");IDirectDraw *root;IDirectDraw4 *dd;IDirect3D3 *d3d;IDirectDrawSurface4 *rt;IDirect3DDevice3 *dev;IDirect3DViewport3 *vp;IDirect3DMaterial3 *mat;IDirect3DLight *light;IDirect3DVertexBuffer *src,*dst;void *data;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL material;D3DMATERIALHANDLE handle;
    TRY(create(NULL,&root,NULL));TRY(IDirectDraw_QueryInterface(root,&IID_IDirectDraw4,(void**)&dd));TRY(IDirectDraw_QueryInterface(root,&IID_IDirect3D3,(void**)&d3d));TRY(IDirectDraw4_SetCooperativeLevel(dd,window,DDSCL_NORMAL));surface_desc(&desc);TRY(IDirectDraw4_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev,NULL));TRY(IDirect3D3_CreateViewport(d3d,&vp,NULL));TRY(IDirect3DDevice3_AddViewport(dev,vp));D3DVIEWPORT2 viewport;ZeroMemory(&viewport,sizeof(viewport));viewport.dwSize=sizeof(viewport);viewport.dwWidth=viewport.dwHeight=128;viewport.dvClipX=-1;viewport.dvClipY=1;viewport.dvClipWidth=viewport.dvClipHeight=2;viewport.dvMaxZ=1;TRY(IDirect3DViewport3_SetViewport2(vp,&viewport));TRY(IDirect3DDevice3_SetCurrentViewport(dev,vp));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3D3_CreateLight(d3d,&light,NULL));TRY(IDirect3DViewport3_AddLight(vp,light));TRY(IDirect3D3_CreateMaterial(d3d,&mat,NULL));TRY(IDirect3DMaterial3_GetHandle(mat,dev,&handle));TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,handle));

    D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL,4};TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&src,0,NULL));vd.dwFVF=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR;TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&dst,0,NULL));ZeroMemory(&material,sizeof(material));material.dwSize=sizeof(material);material.diffuse.b=material.diffuse.a=1;material.power=30;TRY(IDirect3DMaterial3_SetMaterial(mat,&material));D3DLIGHT2 l;ZeroMemory(&l,sizeof(l));l.dwSize=sizeof(l);l.dltType=D3DLIGHT_DIRECTIONAL;l.dcvColor.r=l.dcvColor.g=l.dcvColor.b=1;l.dvDirection.z=1;l.dwFlags=D3DLIGHT_ACTIVE;TRY(IDirect3DLight_SetLight(light,(D3DLIGHT*)&l));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,FALSE));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE));D3DRECT rect={{0},{0},{128},{128}};
    DWORD initial;TRY(IDirect3DDevice3_GetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,&initial));char initial_text[100];wsprintfA(initial_text,"INITIAL api=3 normalize=%08lx",initial);line(initial_text);
    for(unsigned norm=0;norm<2;++norm){TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,norm));DWORD state;TRY(IDirect3DDevice3_GetRenderState(dev,D3DRENDERSTATE_NORMALIZENORMALS,&state));char t[180];wsprintfA(t,"STATE api=3 normalize=%u value=%08lx",norm,state);line(t);for(unsigned kind=0;kind<13;++kind){struct Vertex vertices[4];vertices_for(kind,vertices);CopyMemory(&m,worlds[0],sizeof(m));if(kind==6){m._11=3;m._22=.5f;m._33=2;}if(kind==12){m._13=.3f;m._21=.25f;m._32=.5f;}TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));CopyMemory(&m,worlds[kind],sizeof(m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DVertexBuffer_Lock(src,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer_Unlock(src));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));FillMemory(data,4*sizeof(struct Output),0x6d);TRY(IDirect3DVertexBuffer_Unlock(dst));TRY(IDirect3DViewport3_Clear2(vp,1,&rect,D3DCLEAR_TARGET,0xffab4e69,0,0));TRY(IDirect3DDevice3_BeginScene(dev));TRY(IDirect3DVertexBuffer_ProcessVertices(dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,4,src,0,dev,0));TRY(IDirect3DDevice3_DrawPrimitive(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ|D3DFVF_NORMAL,vertices,4,0));TRY(IDirect3DDevice3_EndScene(dev));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));cpu_rows(3,kind,norm,data);TRY(IDirect3DVertexBuffer_Unlock(dst));DDSURFACEDESC2 locked;ZeroMemory(&locked,sizeof(locked));locked.dwSize=sizeof(locked);TRY(IDirectDrawSurface4_Lock(rt,NULL,&locked,DDLOCK_READONLY|DDLOCK_WAIT,NULL));DWORD pixel;CopyMemory(&pixel,(BYTE*)locked.lpSurface+64*locked.lPitch+64*4,4);TRY(IDirectDrawSurface4_Unlock(rt,NULL));wsprintfA(t,"PIXEL api=3 matrix=%u normalize=%u color=%08lx",kind,norm,pixel);line(t);}}
    TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,0));IDirect3DVertexBuffer_Release(dst);IDirect3DVertexBuffer_Release(src);IDirect3DMaterial3_Release(mat);TRY(IDirect3DViewport3_DeleteLight(vp,light));IDirect3DLight_Release(light);TRY(IDirect3DDevice3_DeleteViewport(dev,vp));IDirect3DViewport3_Release(vp);IDirect3DDevice3_Release(dev);IDirectDrawSurface4_Release(rt);IDirect3D3_Release(d3d);IDirectDraw4_Release(dd);IDirectDraw_Release(root);line("DONE api=3");return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"NORMREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);if(!lib){status("load system ddraw",HRESULT_FROM_WIN32(GetLastError()));CloseHandle(logfile);ExitProcess(3);}HWND window=CreateWindowExA(0,"STATIC","Native normal-transform reference",WS_OVERLAPPEDWINDOW,0,0,128,128,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL seven=reference7(lib,window),three=reference3(lib,window);char t[100];wsprintfA(t,"COMPLETE api7=%u api3=%u",seven,three);line(t);line("PASS native normal-transform reference complete");DestroyWindow(window);FreeLibrary(lib);CloseHandle(logfile);ExitProcess(0);
}
