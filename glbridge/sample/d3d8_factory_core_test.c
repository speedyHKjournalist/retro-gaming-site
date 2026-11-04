/* Real D3D8 factory wrappers and shared private D3D9 lifecycle. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
static HANDLE logfile;static char directory[MAX_PATH];static HWND window;static UINT root_rounds,identity_checks;
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void factory_core_check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s actual=%08lX",s,(DWORD)hr);report(line);ExitProcess(1);}}
void factory_core9_open(const char*directory,HWND window);void factory_core9_draw(DWORD color);void factory_core9_close(void);
typedef struct{HMODULE module;IDirect3D8*factory;IDirect3DDevice8*device;IDirect3DSurface8*cpu;}Context;
static Context create(UINT sdk)
{
    Context c;ZeroMemory(&c,sizeof(c));char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,"D3D8.DLL");c.module=LoadLibraryA(path);factory_core_check(c.module!=NULL,"FAIL D8 module");IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(c.module,"Direct3DCreate8");factory_core_check(make!=NULL,"FAIL D8 export");factory_core_check(!GetProcAddress(c.module,"Direct3DCreate9")&&!GetProcAddress(c.module,"d9wg_d3d9_core_create8"),"FAIL private ABI leaked");factory_core_check(!make(0)&&!make(120|0x80000000u),"FAIL SDK boundary");
    for(UINT i=0;i<64;i++){
        IDirect3D8*r=make(i%2?120:220);factory_core_check(r!=NULL,"FAIL root cycle");factory_core_check(IDirect3D8_GetAdapterCount(r)==1&&IDirect3D8_GetAdapterMonitor(r,0)==MonitorFromWindow(NULL,MONITOR_DEFAULTTOPRIMARY)&&!IDirect3D8_GetAdapterMonitor(r,0xffffffffu),"FAIL delegated adapter queries");
        IUnknown*unknown=NULL;expect(IDirect3D8_QueryInterface(r,&IID_IUnknown,(void**)&unknown),"FAIL root IUnknown");factory_core_check((void*)unknown==r&&IUnknown_Release(unknown)==1,"FAIL root IUnknown identity");IDirect3D8*same=NULL;expect(IDirect3D8_QueryInterface(r,&IID_IDirect3D8,(void**)&same),"FAIL root QI");factory_core_check(same==r&&IDirect3D8_Release(same)==1&&!IDirect3D8_Release(r),"FAIL root public refs");root_rounds++;identity_checks+=2;
    }
    c.factory=make(sdk);factory_core_check(c.factory!=NULL,"FAIL context root");IUnknown*unknown=NULL;expect(IDirect3D8_QueryInterface(c.factory,&IID_IUnknown,(void**)&unknown),"FAIL context QI");factory_core_check((void*)unknown==c.factory&&IUnknown_Release(unknown)==1,"FAIL context identity");identity_checks++;
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.FullScreen_PresentationInterval=D3DPRESENT_INTERVAL_DEFAULT;expect(IDirect3D8_CreateDevice(c.factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.device),"FAIL D8 device");IDirect3D8*parent=NULL;expect(IDirect3DDevice8_GetDirect3D(c.device,&parent),"FAIL D8 parent");factory_core_check(parent==c.factory&&IDirect3D8_Release(parent)==2,"FAIL D8 parent identity");identity_checks++;
    factory_core_check(IDirect3D8_Release(c.factory)==1,"FAIL root survives by device");expect(IDirect3DDevice8_GetDirect3D(c.device,&parent),"FAIL D8 device-only root");factory_core_check(parent==c.factory&&IDirect3D8_GetAdapterCount(parent)==1&&IDirect3D8_Release(parent)==1,"FAIL backend root lost with device owner");identity_checks++;
    expect(IDirect3DDevice8_CreateImageSurface(c.device,8,8,D3DFMT_A8R8G8B8,&c.cpu),"FAIL D8 CPU");expect(IDirect3DDevice8_SetVertexShader(c.device,68),"FAIL D8 FVF");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_ZENABLE,FALSE),"FAIL D8 Z");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_LIGHTING,FALSE),"FAIL D8 lighting");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL D8 cull");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL D8 color op");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL D8 color arg");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL D8 alpha op");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL D8 alpha arg");return c;
}
static void frame(UINT step,UINT api,DWORD color){char line[160];wsprintfA(line,"FCORE FRAME step=%u api=%u color=%08lX readback=1",step,api,color);report(line);}
static void draw(Context*c,UINT step,DWORD color)
{
    typedef struct{float x,y,z,w;DWORD color;}Vertex;Vertex quad[4]={{-.5f,-.5f,.5f,1,color},{7.5f,-.5f,.5f,1,color},{-.5f,7.5f,.5f,1,color},{7.5f,7.5f,.5f,1,color}},v[6];const UINT order[6]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=quad[order[i]];
    expect(IDirect3DDevice8_BeginScene(c->device),"FAIL D8 scene");expect(IDirect3DDevice8_DrawPrimitiveUP(c->device,D3DPT_TRIANGLELIST,2,v,20),"FAIL D8 draw");expect(IDirect3DDevice8_EndScene(c->device),"FAIL D8 end");IDirect3DSurface8*target=NULL;expect(IDirect3DDevice8_GetRenderTarget(c->device,&target),"FAIL D8 target");expect(IDirect3DDevice8_CopyRects(c->device,target,NULL,0,c->cpu,NULL),"FAIL D8 readback");IDirect3DSurface8_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface8_LockRect(c->cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL D8 lock");expect(IDirect3DSurface8_UnlockRect(c->cpu),"FAIL D8 unlock");frame(step,8,color);
}
static void close_context(Context*c){factory_core_check(!IDirect3DSurface8_Release(c->cpu)&&!IDirect3DDevice8_Release(c->device),"FAIL D8 final device/root refs");factory_core_check(FreeLibrary(c->module),"FAIL D8 unload");}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"FCORE.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);factory_core_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");window=CreateWindowExA(0,"STATIC","D3D8 factory core",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);factory_core_check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);
    Context eight=create(120);draw(&eight,0,0xffb02010);factory_core9_open(directory,window);identity_checks+=2;factory_core9_draw(0xff1040c0);frame(1,9,0xff1040c0);close_context(&eight);factory_core9_draw(0xff20a040);frame(2,9,0xff20a040);eight=create(220);draw(&eight,3,0xffc0a020);close_context(&eight);factory_core9_draw(0xff304050);frame(4,9,0xff304050);factory_core9_close();DestroyWindow(window);
    factory_core_check(root_rounds==128&&identity_checks==264,"FAIL counters");report("FCORE STATUS rootRounds=128 identityChecks=264 sdk120=1 sdk220=1 d8Loads=2 d8Unloads=2 d9Loads=1 d9Unloads=1 devices=3 factoryRefs=0 frames=5 readbacks=5");report("PASS D3D8 factory core wrapping");CloseHandle(logfile);ExitProcess(0);
}
