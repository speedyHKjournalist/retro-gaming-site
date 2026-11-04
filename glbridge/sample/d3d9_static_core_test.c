/* Static D3D9 core and ordinary D3D9 DLL coexist through real loader lifetimes. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;static char directory[MAX_PATH];static HWND window;
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s actual=%08lX",s,(DWORD)hr);report(line);ExitProcess(1);}}
static ULONG refs(IUnknown*p){IUnknown_AddRef(p);return IUnknown_Release(p);}
typedef struct Context {HMODULE module;IDirect3D9*factory;IDirect3DDevice9*device;IDirect3DSurface9*cpu;BOOL embedded;}Context;
static Context create(BOOL embedded)
{
    Context c;ZeroMemory(&c,sizeof(c));char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,embedded?"CORE9.DLL":"D3D9.DLL");c.module=LoadLibraryA(path);check(c.module!=NULL,"FAIL module load");c.embedded=embedded;
    IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(c.module,embedded?"D9CoreCreate9":"Direct3DCreate9");check(make!=NULL,"FAIL factory export");
    if(embedded){check(!GetProcAddress(c.module,"Direct3DCreate9")&&!GetProcAddress(c.module,"DebugSetMute")&&!GetProcAddress(c.module,"DllMain"),"FAIL private core leaked loader exports");}
    check(make(0)==NULL,"FAIL private SDK token validation");c.factory=make(D3D_SDK_VERSION);check(c.factory!=NULL,"FAIL factory");
    IUnknown*unknown=NULL;expect(IDirect3D9_QueryInterface(c.factory,&IID_IUnknown,(void**)&unknown),"FAIL factory QI");check((void*)unknown==c.factory&&IUnknown_Release(unknown)==1,"FAIL factory identity");
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    expect(IDirect3D9_CreateDevice(c.factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.device),"FAIL real backend device");IDirect3D9*parent=NULL;expect(IDirect3DDevice9_GetDirect3D(c.device,&parent),"FAIL backend parent");check(parent==c.factory&&IDirect3D9_Release(parent)==2,"FAIL backend parent identity/ref");
    expect(IDirect3DDevice9_CreateOffscreenPlainSurface(c.device,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&c.cpu,NULL),"FAIL CPU readback");
    expect(IDirect3DDevice9_SetFVF(c.device,68),"FAIL FVF");expect(IDirect3DDevice9_SetRenderState(c.device,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(c.device,D3DRS_LIGHTING,FALSE),"FAIL lighting");expect(IDirect3DDevice9_SetRenderState(c.device,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");
    expect(IDirect3DDevice9_SetTextureStageState(c.device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL color op");expect(IDirect3DDevice9_SetTextureStageState(c.device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL color arg");expect(IDirect3DDevice9_SetTextureStageState(c.device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL alpha op");expect(IDirect3DDevice9_SetTextureStageState(c.device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL alpha arg");
    return c;
}
typedef struct Vertex{float x,y,z,w;DWORD color;}Vertex;
static void draw(Context*c,UINT step,DWORD color)
{
    Vertex quad[4]={{-.5f,-.5f,.5f,1,color},{7.5f,-.5f,.5f,1,color},{-.5f,7.5f,.5f,1,color},{7.5f,7.5f,.5f,1,color}},v[6];const UINT order[6]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=quad[order[i]];
    check(refs((IUnknown*)c->device)==2,"FAIL public CPU pin");expect(IDirect3DDevice9_BeginScene(c->device),"FAIL scene");expect(IDirect3DDevice9_DrawPrimitiveUP(c->device,D3DPT_TRIANGLELIST,2,v,sizeof(Vertex)),"FAIL static/DLL draw");expect(IDirect3DDevice9_EndScene(c->device),"FAIL scene end");
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(c->device,0,&target),"FAIL target");expect(IDirect3DDevice9_GetRenderTargetData(c->device,target,c->cpu),"FAIL transport readback");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(c->cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL readback access");expect(IDirect3DSurface9_UnlockRect(c->cpu),"FAIL CPU unlock");
    char line[160];wsprintfA(line,"CORE FRAME step=%u embedded=%u color=%08lX readback=1",step,c->embedded,color);report(line);
}
static void close_context(Context*c)
{
    check(!IDirect3DSurface9_Release(c->cpu)&&!IDirect3DDevice9_Release(c->device)&&!IDirect3D9_Release(c->factory),"FAIL backend final refs");check(FreeLibrary(c->module),"FAIL backend module unload");ZeroMemory(c,sizeof(*c));
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"CORE9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");
    window=CreateWindowExA(0,"STATIC","Private D3D9 core",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);
    Context embedded=create(TRUE);draw(&embedded,0,0xffb02010);Context dll=create(FALSE);draw(&dll,1,0xff1040c0);
    close_context(&embedded);draw(&dll,2,0xff20a040);embedded=create(TRUE);draw(&embedded,3,0xffc0a020);close_context(&embedded);draw(&dll,4,0xff304050);close_context(&dll);
    DestroyWindow(window);report("CORE STATUS privateLoads=2 privateUnloads=2 dllLoads=1 dllUnloads=1 factories=3 devices=3 identityChecks=6 finalRefs=0 frames=5 readbacks=5");report("PASS D3D9 static core coexistence");CloseHandle(logfile);ExitProcess(0);
}
