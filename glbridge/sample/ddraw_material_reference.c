/* Independent legacy D3D3 RGB material/background reference. */
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
static BOOL reference3(HMODULE lib,HWND window)
{
    HRESULT(WINAPI *create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreate");IDirectDraw *root;IDirectDraw4 *dd;IDirect3D3 *d3d;IDirectDrawSurface4 *rt;IDirect3DDevice3 *dev;IDirect3DViewport3 *vp;IDirect3DMaterial3 *mat;IDirect3DLight *light;DDSURFACEDESC2 desc;D3DMATRIX m;D3DMATERIAL material;D3DMATERIALHANDLE handle;
    TRY(create(NULL,&root,NULL));TRY(IDirectDraw_QueryInterface(root,&IID_IDirectDraw4,(void**)&dd));TRY(IDirectDraw_QueryInterface(root,&IID_IDirect3D3,(void**)&d3d));TRY(IDirectDraw4_SetCooperativeLevel(dd,window,DDSCL_NORMAL));surface_desc(&desc);TRY(IDirectDraw4_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev,NULL));TRY(IDirect3D3_CreateViewport(d3d,&vp,NULL));TRY(IDirect3DDevice3_AddViewport(dev,vp));D3DVIEWPORT2 viewport;ZeroMemory(&viewport,sizeof(viewport));viewport.dwSize=sizeof(viewport);viewport.dwWidth=viewport.dwHeight=128;viewport.dvClipX=-1;viewport.dvClipY=1;viewport.dvClipWidth=viewport.dvClipHeight=2;viewport.dvMaxZ=1;TRY(IDirect3DViewport3_SetViewport2(vp,&viewport));TRY(IDirect3DDevice3_SetCurrentViewport(dev,vp));
    ZeroMemory(&m,sizeof(m));m._11=m._22=m._33=m._44=1;TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_WORLD,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_VIEW,&m));TRY(IDirect3DDevice3_SetTransform(dev,D3DTRANSFORMSTATE_PROJECTION,&m));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_SPECULARENABLE,TRUE));TRY(IDirect3D3_CreateLight(d3d,&light,NULL));TRY(IDirect3DViewport3_AddLight(vp,light));status("LIGHT duplicate",IDirect3DViewport3_AddLight(vp,light));TRY(IDirect3D3_CreateMaterial(d3d,&mat,NULL));TRY(IDirect3DMaterial3_GetHandle(mat,dev,&handle));TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,handle));

    D3DMATERIALHANDLE got;BOOL valid;char t[160];
    TRY(IDirect3DViewport3_GetBackground(vp,&got,&valid));wsprintfA(t,"BG phase=initial handle=%lu valid=%u",got,valid);line(t);
    HRESULT hr=IDirect3DViewport3_SetBackground(vp,0);status("BG zero_initial",hr);
    ZeroMemory(&material,sizeof(material));material.dwSize=sizeof(material);material.diffuse.r=1;TRY(IDirect3DMaterial3_SetMaterial(mat,&material));TRY(IDirect3DViewport3_SetBackground(vp,handle));TRY(IDirect3DViewport3_GetBackground(vp,&got,&valid));wsprintfA(t,"BG phase=bound same=%u valid=%u",got==handle,valid);line(t);
    hr=IDirect3DViewport3_SetBackground(vp,0);status("BG zero_bound",hr);TRY(IDirect3DViewport3_GetBackground(vp,&got,&valid));wsprintfA(t,"BG phase=after_zero same=%u valid=%u",got==handle,valid);line(t);
    D3DRECT rect={{0},{0},{128},{128}};TRY(IDirect3DViewport3_Clear(vp,1,&rect,D3DCLEAR_TARGET));
    DDSURFACEDESC2 locked;ZeroMemory(&locked,sizeof(locked));locked.dwSize=sizeof(locked);TRY(IDirectDrawSurface4_Lock(rt,NULL,&locked,DDLOCK_READONLY|DDLOCK_WAIT,NULL));DWORD pixel;CopyMemory(&pixel,(BYTE*)locked.lpSurface+64*locked.lPitch+64*4,4);TRY(IDirectDrawSurface4_Unlock(rt,NULL));wsprintfA(t,"PIXEL phase=background color=%08lx",pixel);line(t);
    TRY(IDirect3DViewport3_DeleteLight(vp,light));IDirect3DLight_Release(light);
    TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE));TRY(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE));
    struct Vertex{float p[3],n[3];DWORD color;float uv[2];}vertices[]={{{-1,-1,.5f},{0,0,-1},0xffffffff,{.25f,.75f}},{{-1,1,.5f},{0,0,-1},0xffffffff,{.25f,.75f}},{{1,-1,.5f},{0,0,-1},0xffffffff,{.25f,.75f}},{{1,1,.5f},{0,0,-1},0xffffffff,{.25f,.75f}}};
    IDirect3DVertexBuffer *src,*dst;void *data;D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1,4};TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&src,0,NULL));vd.dwFVF=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1;TRY(IDirect3D3_CreateVertexBuffer(d3d,&vd,&dst,0,NULL));
    ZeroMemory(&material,sizeof(material));material.dwSize=sizeof(material);material.emissive.g=1;TRY(IDirect3DMaterial3_SetMaterial(mat,&material));
    for(unsigned bound=0;bound<2;++bound)for(unsigned red=0;red<2;++red){TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,bound?handle:0));for(unsigned i=0;i<4;++i)vertices[i].color=red?0xffff0000:0xffffffff;TRY(IDirect3DDevice3_BeginScene(dev));TRY(IDirect3DDevice3_DrawPrimitive(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1,vertices,4,0));TRY(IDirect3DDevice3_EndScene(dev));locked.dwSize=sizeof(locked);TRY(IDirectDrawSurface4_Lock(rt,NULL,&locked,DDLOCK_READONLY|DDLOCK_WAIT,NULL));CopyMemory(&pixel,(BYTE*)locked.lpSurface+64*locked.lPitch+64*4,4);TRY(IDirectDrawSurface4_Unlock(rt,NULL));wsprintfA(t,"PIXEL phase=material bound=%u red=%u color=%08lx",bound,red,pixel);line(t);
      TRY(IDirect3DVertexBuffer_Lock(src,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer_Unlock(src));for(unsigned lit=0;lit<2;++lit)for(unsigned keep=0;keep<2;++keep){TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));FillMemory(data,4*32,0x6d);TRY(IDirect3DVertexBuffer_Unlock(dst));TRY(IDirect3DVertexBuffer_ProcessVertices(dst,D3DVOP_TRANSFORM|(lit?D3DVOP_LIGHT:0),0,4,src,0,dev,keep?D3DPV_DONOTCOPYDATA:0));TRY(IDirect3DVertexBuffer_Lock(dst,0,&data,NULL));for(unsigned i=0;i<4;++i){DWORD color,specular,u,v;CopyMemory(&color,(BYTE*)data+i*32+16,4);CopyMemory(&specular,(BYTE*)data+i*32+20,4);CopyMemory(&u,(BYTE*)data+i*32+24,4);CopyMemory(&v,(BYTE*)data+i*32+28,4);wsprintfA(t,"CPU bound=%u red=%u lit=%u keep=%u vertex=%u diffuse=%08lx specular=%08lx u=%08lx v=%08lx",bound,red,lit,keep,i,color,specular,u,v);line(t);}TRY(IDirect3DVertexBuffer_Unlock(dst));}
    }
    IDirect3DVertexBuffer_Release(dst);IDirect3DVertexBuffer_Release(src);
    TRY(IDirect3DDevice3_SetLightState(dev,D3DLIGHTSTATE_MATERIAL,0));
    IDirect3DMaterial3_Release(mat);TRY(IDirect3DDevice3_DeleteViewport(dev,vp));IDirect3DViewport3_Release(vp);IDirect3DDevice3_Release(dev);IDirectDrawSurface4_Release(rt);IDirect3D3_Release(d3d);IDirectDraw4_Release(dd);IDirectDraw_Release(root);line("DONE api=3");return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));int at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"MATREF.TXT");logfile=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE lib=LoadLibraryA(p);if(!lib){status("load system ddraw",HRESULT_FROM_WIN32(GetLastError()));CloseHandle(logfile);ExitProcess(3);}HWND window=CreateWindowExA(0,"STATIC","Native material reference",WS_OVERLAPPEDWINDOW,0,0,128,128,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL three=reference3(lib,window);char t[100];wsprintfA(t,"COMPLETE api3=%u",three);line(t);line("PASS native material reference complete");DestroyWindow(window);FreeLibrary(lib);CloseHandle(logfile);ExitProcess(0);
}
