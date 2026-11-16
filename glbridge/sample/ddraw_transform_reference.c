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


static void matrix_row(unsigned state,unsigned phase,const D3DMATRIX *m)
{
 char t[300];DWORD words[16];CopyMemory(words,m,64);int n=wsprintfA(t,"MATRIX state=%u phase=%u",state,phase);for(unsigned i=0;i<16;++i)n+=wsprintfA(t+n," %08lx",words[i]);line(t);
}
static BOOL reference(HMODULE lib,HWND window)
{
 HRESULT(WINAPI *create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreateEx");IDirectDraw7*dd;IDirect3D7*d3d;IDirectDrawSurface7*rt;IDirect3DDevice7*dev;IDirect3DVertexBuffer7*src,*dst;
 TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d));DDSURFACEDESC2 desc;surface_desc(&desc);TRY(IDirectDraw7_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));
 static const D3DVIEWPORT7 viewports[]={
 {0,0,128,128,0,1},{0,0,128,128,0,0},{0,0,128,128,1,0},{10,5,100,110,-2,4},{0,0,128,128,-1,-2},{0,0,128,128,2,3},{0,0,129,128,0,1},{1,0,128,128,0,1},{0,0,0,128,0,1},{0,0,128,0,0,1},{0xffffffffu,0,2,2,0,1}};
 static const float vertices[4][3]={{0,0,0},{1,1,1},{-1,-1,.5f},{.5f,-.5f,.25f}};
 D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ,4};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&src,0));vd.dwFVF=D3DFVF_XYZRHW;TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&dst,0));void*data;TRY(IDirect3DVertexBuffer7_Lock(src,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer7_Unlock(src));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_LIGHTING,FALSE));
 for(unsigned v=0;v<sizeof(viewports)/sizeof(*viewports);++v){D3DVIEWPORT7 in=viewports[v],out;HRESULT hr=IDirect3DDevice7_SetViewport(dev,&in);TRY(IDirect3DDevice7_GetViewport(dev,&out));char t[300];DWORD words[6];CopyMemory(words,&out,24);wsprintfA(t,"VIEW case=%u hr=%08lx x=%lu y=%lu w=%lu h=%lu min=%08lx max=%08lx",v,hr,words[0],words[1],words[2],words[3],words[4],words[5]);line(t);
 if(SUCCEEDED(hr)){TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));FillMemory(data,64,0x6d);TRY(IDirect3DVertexBuffer7_Unlock(dst));TRY(IDirect3DVertexBuffer7_ProcessVertices(dst,D3DVOP_TRANSFORM,0,4,src,0,dev,0));TRY(IDirect3DVertexBuffer7_Lock(dst,0,&data,NULL));const DWORD*o=data;for(unsigned i=0;i<4;++i){wsprintfA(t,"VERTEX case=%u vertex=%u %08lx %08lx %08lx %08lx",v,i,o[i*4],o[i*4+1],o[i*4+2],o[i*4+3]);line(t);}TRY(IDirect3DVertexBuffer7_Unlock(dst));}}
 static const unsigned states[]={1,2,3,4,5,6,16,17,18,19,20,21,22,23};
 const float av[16]={1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1},bv[16]={2,0,0,0,0,3,0,0,0,0,4,0,5,6,7,1};D3DMATRIX a,b;CopyMemory(&a,av,64);CopyMemory(&b,bv,64);
 for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i){D3DMATRIX got;DWORD block;unsigned state=states[i];TRY(IDirect3DDevice7_SetTransform(dev,state,(D3DMATRIX*)&a));TRY(IDirect3DDevice7_MultiplyTransform(dev,state,(D3DMATRIX*)&b));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,0,&got);
 TRY(IDirect3DDevice7_SetTransform(dev,state,(D3DMATRIX*)&a));TRY(IDirect3DDevice7_BeginStateBlock(dev));TRY(IDirect3DDevice7_MultiplyTransform(dev,state,(D3DMATRIX*)&b));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,1,&got);TRY(IDirect3DDevice7_EndStateBlock(dev,&block));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,2,&got);TRY(IDirect3DDevice7_SetTransform(dev,state,(D3DMATRIX*)&a));TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,3,&got);TRY(IDirect3DDevice7_DeleteStateBlock(dev,block));
 TRY(IDirect3DDevice7_SetTransform(dev,state,(D3DMATRIX*)&a));TRY(IDirect3DDevice7_BeginStateBlock(dev));TRY(IDirect3DDevice7_SetTransform(dev,state,(D3DMATRIX*)&b));TRY(IDirect3DDevice7_MultiplyTransform(dev,state,(D3DMATRIX*)&b));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,4,&got);TRY(IDirect3DDevice7_EndStateBlock(dev,&block));TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));TRY(IDirect3DDevice7_GetTransform(dev,state,&got));matrix_row(state,5,&got);TRY(IDirect3DDevice7_DeleteStateBlock(dev,block));}
 IDirect3DVertexBuffer7_Release(dst);IDirect3DVertexBuffer7_Release(src);IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(rt);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
 char logfile_path[MAX_PATH];GetModuleFileNameA(NULL,logfile_path,sizeof(logfile_path));int pos=lstrlenA(logfile_path);while(pos&&logfile_path[pos-1]!='\\')--pos;logfile_path[pos]=0;lstrcatA(logfile_path,"TRNREF.TXT");logfile=CreateFileA(logfile_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(1);HWND window=CreateWindowExA(0,"STATIC","Native transform reference",WS_OVERLAPPEDWINDOW,0,0,160,160,NULL,NULL,GetModuleHandleA(NULL),NULL);HMODULE lib=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");BOOL ok=lib&&reference(lib,window);if(lib)FreeLibrary(lib);DestroyWindow(window);line(ok?"COMPLETE transform=1":"COMPLETE transform=0");CloseHandle(logfile);ExitProcess(ok?0:1);
}
