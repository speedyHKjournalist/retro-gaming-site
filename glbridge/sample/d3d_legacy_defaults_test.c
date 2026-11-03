/* Versioned creation defaults and legacy MODULATE alpha. XP uses the proxy;
 * independent GPU replay checks the readback pixels. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
#include <d3d.h>
static HANDLE logfile;
static void report(const char *s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char *s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,HRESULT wanted,const char*s){if(hr!=wanted){char line[180];wsprintfA(line,"%s actual=%08lX expected=%08lX",s,(DWORD)hr,(DWORD)wanted);report(line);ExitProcess(1);}}
static IDirectDrawSurface7 *surface(IDirectDraw7 *root,BOOL texture,BOOL alpha)
{
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=texture?1:8;d.dwHeight=texture?1:8;d.ddsCaps.dwCaps=(texture?DDSCAPS_TEXTURE:DDSCAPS_3DDEVICE|DDSCAPS_OFFSCREENPLAIN)|DDSCAPS_SYSTEMMEMORY;
    d.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);d.ddpfPixelFormat.dwFlags=DDPF_RGB|(alpha?DDPF_ALPHAPIXELS:0);d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;d.ddpfPixelFormat.dwRGBAlphaBitMask=alpha?0xff000000:0;
    IDirectDrawSurface7 *p=NULL;expect(IDirectDraw7_CreateSurface(root,&d,&p,NULL),DD_OK,"FAIL surface creation");
    if(texture){DDSURFACEDESC2 lock;ZeroMemory(&lock,sizeof(lock));lock.dwSize=sizeof(lock);expect(IDirectDrawSurface7_Lock(p,NULL,&lock,DDLOCK_WRITEONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL upload lock");*(DWORD*)lock.lpSurface=alpha?0x80804020:0x00804020;expect(IDirectDrawSurface7_Unlock(p,NULL),DD_OK,"FAIL upload unlock");}return p;
}
static IDirect3DDevice7 *device(IDirectDraw7*root,IDirectDrawSurface7*target,UINT api)
{
    IUnknown *raw=NULL,*factory=NULL,*view=NULL;
    if(api==1){expect(IDirectDrawSurface7_QueryInterface(target,&IID_IDirectDrawSurface,(void**)&view),DD_OK,"FAIL v1 surface");expect(IUnknown_QueryInterface(view,&IID_IDirect3DHALDevice,(void**)&raw),DD_OK,"FAIL v1 device");}
    else if(api==2){expect(IDirectDraw7_QueryInterface(root,&IID_IDirect3D2,(void**)&factory),DD_OK,"FAIL v2 factory");expect(IDirectDrawSurface7_QueryInterface(target,&IID_IDirectDrawSurface,(void**)&view),DD_OK,"FAIL v2 surface");expect(IDirect3D2_CreateDevice((IDirect3D2*)factory,&IID_IDirect3DHALDevice,(IDirectDrawSurface*)view,(IDirect3DDevice2**)&raw),D3D_OK,"FAIL v2 device");}
    else if(api==3){expect(IDirectDraw7_QueryInterface(root,&IID_IDirect3D3,(void**)&factory),DD_OK,"FAIL v3 factory");expect(IDirectDrawSurface7_QueryInterface(target,&IID_IDirectDrawSurface4,(void**)&view),DD_OK,"FAIL v3 surface");expect(IDirect3D3_CreateDevice((IDirect3D3*)factory,&IID_IDirect3DHALDevice,(IDirectDrawSurface4*)view,(IDirect3DDevice3**)&raw,NULL),D3D_OK,"FAIL v3 device");}
    else{expect(IDirectDraw7_QueryInterface(root,&IID_IDirect3D7,(void**)&factory),DD_OK,"FAIL v7 factory");expect(IDirect3D7_CreateDevice((IDirect3D7*)factory,&IID_IDirect3DHALDevice,target,(IDirect3DDevice7**)&raw),D3D_OK,"FAIL v7 device");}
    IDirect3DDevice7 *d=NULL;expect(IUnknown_QueryInterface(raw,&IID_IDirect3DDevice7,(void**)&d),D3D_OK,"FAIL shared v7 device view");IUnknown_Release(raw);if(view)IUnknown_Release(view);if(factory)IUnknown_Release(factory);return d;
}
static void render_state(IDirect3DDevice7*d,D3DRENDERSTATETYPE state,DWORD wanted){DWORD value=0;expect(IDirect3DDevice7_GetRenderState(d,state,&value),D3D_OK,"FAIL default render state");check(value==wanted,"FAIL versioned default value");}
static void stage_state(IDirect3DDevice7*d,D3DTEXTURESTAGESTATETYPE state,DWORD wanted){DWORD value=0;expect(IDirect3DDevice7_GetTextureStageState(d,0,state,&value),D3D_OK,"FAIL stage state");check(value==wanted,"FAIL alpha cascade value");}
typedef struct Vertex {float x,y,z,rhw;DWORD color;float u,v;} Vertex;
static void draw(IDirect3DDevice7*d,IDirectDrawSurface7*target,UINT api,UINT frame,DWORD alpha)
{
    Vertex vertices[4]={{0,0,.5f,1,0x40ffffff,0,0},{8,0,.5f,1,0x40ffffff,1,0},{0,8,.5f,1,0x40ffffff,0,1},{8,8,.5f,1,0x40ffffff,1,1}};
    expect(IDirect3DDevice7_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff010203,1,0),D3D_OK,"FAIL clear");expect(IDirect3DDevice7_BeginScene(d),D3D_OK,"FAIL scene");expect(IDirect3DDevice7_DrawPrimitive(d,D3DPT_TRIANGLESTRIP,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1,vertices,4,0),D3D_OK,"FAIL textured quad");expect(IDirect3DDevice7_EndScene(d),D3D_OK,"FAIL end scene");
    DDSURFACEDESC2 lock;ZeroMemory(&lock,sizeof(lock));lock.dwSize=sizeof(lock);expect(IDirectDrawSurface7_Lock(target,NULL,&lock,DDLOCK_READONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL actual readback command");expect(IDirectDrawSurface7_Unlock(target,NULL),DD_OK,"FAIL readback unlock");
    char line[180];wsprintfA(line,"DEFAULT FRAME api=%u index=%u alpha=%u arg2=1 readback=1",api,frame,alpha);report(line);
}
static void cases(IDirectDraw7*root,UINT api)
{
    IDirectDrawSurface7*target=surface(root,FALSE,TRUE),*argb=surface(root,TRUE,TRUE),*rgb=surface(root,TRUE,FALSE);IDirect3DDevice7*d=device(root,target,api);IDirect3DDevice3*legacy=NULL;expect(IDirect3DDevice7_QueryInterface(d,&IID_IDirect3DDevice3,(void**)&legacy),D3D_OK,"FAIL shared legacy view");
    render_state(d,D3DRENDERSTATE_COLORKEYENABLE,api==1);render_state(d,D3DRENDERSTATE_SPECULARENABLE,api==2);render_state(d,D3DRENDERSTATE_NORMALIZENORMALS,api<7);stage_state(d,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);stage_state(d,D3DTSS_ALPHAARG2,D3DTA_CURRENT);
    const D3DRENDERSTATETYPE ignored[]={D3DRENDERSTATE_LIGHTING,D3DRENDERSTATE_NORMALIZENORMALS,D3DRENDERSTATE_LOCALVIEWER};for(UINT i=0;i<3;i++){DWORD value=0,before=0;expect(IDirect3DDevice7_GetRenderState(d,ignored[i],&before),D3D_OK,"FAIL modern ignored-state query");expect(IDirect3DDevice3_GetRenderState(legacy,ignored[i],&value),D3D_OK,"FAIL legacy ignored query");check(value==0xffffffff,"FAIL legacy ignored sentinel");expect(IDirect3DDevice3_SetRenderState(legacy,ignored[i],0),D3D_OK,"FAIL legacy ignored write");render_state(d,ignored[i],before);}
    DWORD rejected=0x12345678;expect(IDirect3DDevice7_SetRenderState(d,D3DRENDERSTATE_TEXTUREMAPBLEND,D3DTBLEND_MODULATE),DDERR_INVALIDPARAMS,"FAIL modern obsolete state rejection");expect(IDirect3DDevice7_GetRenderState(d,D3DRENDERSTATE_TEXTUREMAPBLEND,&rejected),DDERR_INVALIDPARAMS,"FAIL modern obsolete query");check(rejected==0x12345678,"FAIL rejected query changed output");
    char line[180];wsprintfA(line,"DEFAULT CREATE api=%u key=%u specular=%u normalize=%u alpha=2 arg2=1 legacyIgnored=3 modernRejected=2",api,api==1,api==2,api<7);report(line);
    expect(IDirect3DDevice7_SetRenderState(d,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE),D3D_OK,"FAIL cull");expect(IDirect3DDevice7_SetRenderState(d,D3DRENDERSTATE_ZENABLE,FALSE),D3D_OK,"FAIL depth");
    expect(IDirect3DDevice3_SetTexture(legacy,0,NULL),D3D_OK,"FAIL unbound legacy texture");
    IDirect3DTexture2*texture4=NULL;expect(IDirectDrawSurface7_QueryInterface(argb,&IID_IDirect3DTexture2,(void**)&texture4),DD_OK,"FAIL ARGB view");expect(IDirect3DDevice3_SetTexture(legacy,0,texture4),D3D_OK,"FAIL ARGB binding");IDirect3DTexture2_Release(texture4);stage_state(d,D3DTSS_ALPHAOP,2);draw(d,target,api,0,128);
    expect(IDirectDrawSurface7_QueryInterface(rgb,&IID_IDirect3DTexture2,(void**)&texture4),DD_OK,"FAIL RGB view");expect(IDirect3DDevice3_SetTexture(legacy,0,texture4),D3D_OK,"FAIL RGB binding");IDirect3DTexture2_Release(texture4);stage_state(d,D3DTSS_ALPHAOP,api<7?3:2);
    expect(IDirect3DDevice3_SetTextureStageState(legacy,0,D3DTSS_COLOROP,D3DTOP_MODULATE),D3D_OK,"FAIL unchanged stage value");draw(d,target,api,1,api<7?64:255);
    expect(IDirect3DDevice3_SetTextureStageState(legacy,0,D3DTSS_ALPHAOP,D3DTOP_MODULATE),D3D_OK,"FAIL changed alpha mode");expect(IDirect3DDevice3_SetTextureStageState(legacy,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),D3D_OK,"FAIL explicit current alpha");expect(IDirect3DDevice7_SetTexture(d,0,argb),D3D_OK,"FAIL explicit ARGB binding");
    if(api<7){expect(IDirect3DDevice3_SetRenderState(legacy,D3DRENDERSTATE_TEXTUREMAPBLEND,D3DTBLEND_MODULATE),D3D_OK,"FAIL unchanged legacy blend");}stage_state(d,D3DTSS_ALPHAOP,3);draw(d,target,api,2,64);
    if(api<7){expect(IDirect3DDevice3_SetRenderState(legacy,D3DRENDERSTATE_TEXTUREMAPBLEND,D3DTBLEND_ADD),D3D_OK,"FAIL changed legacy blend");expect(IDirect3DDevice3_SetRenderState(legacy,D3DRENDERSTATE_TEXTUREMAPBLEND,D3DTBLEND_MODULATE),D3D_OK,"FAIL reactivate legacy blend");}else expect(IDirect3DDevice7_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),D3D_OK,"FAIL modern alpha restore");stage_state(d,D3DTSS_ALPHAOP,2);stage_state(d,D3DTSS_ALPHAARG2,1);draw(d,target,api,3,128);
    expect(IDirect3DDevice7_SetTexture(d,0,NULL),D3D_OK,"FAIL unbind");stage_state(d,D3DTSS_ALPHAOP,2);IDirect3DDevice3_Release(legacy);check(IDirect3DDevice7_Release(d)==0,"FAIL device references");check(IDirectDrawSurface7_Release(target)==0&&IDirectDrawSurface7_Release(argb)==0&&IDirectDrawSurface7_Release(rgb)==0,"FAIL surface references");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"LDEF.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");
    HWND window=CreateWindowExA(0,"STATIC","Legacy defaults",WS_POPUP,30,40,32,24,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"DDRAW.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");HRESULT(WINAPI*make)(GUID*,void**,REFIID,IUnknown*)=(void*)(uintptr_t)GetProcAddress(module,"DirectDrawCreateEx");check(make!=NULL,"FAIL export");IDirectDraw7*root=NULL;expect(make(NULL,(void**)&root,&IID_IDirectDraw7,NULL),DD_OK,"FAIL factory");expect(IDirectDraw7_SetCooperativeLevel(root,window,DDSCL_NORMAL),DD_OK,"FAIL cooperation");
    cases(root,1);cases(root,2);cases(root,3);cases(root,7);check(IDirectDraw7_Release(root)==0,"FAIL factory references");FreeLibrary(module);DestroyWindow(window);report("DEFAULT STATUS versions=4 surfaces=12 frames=16 readbacks=16 factoryRefs=0");report("PASS legacy D3D defaults and alpha");CloseHandle(logfile);ExitProcess(0);
}
