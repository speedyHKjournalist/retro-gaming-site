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
static BOOL same_bytes(const void*a,const void*b,UINT n){const volatile BYTE*x=a,*y=b;for(UINT i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static ULONG refs(IUnknown*p){IUnknown_AddRef(p);return IUnknown_Release(p);}
typedef struct Context {HMODULE module;IDirect3D9*factory;IDirect3DDevice9*device;IDirect3DSurface9*cpu;BOOL embedded;}Context;
static UINT sdk;
static Context create(BOOL embedded)
{
    Context c;ZeroMemory(&c,sizeof(c));char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,embedded?"CORE8.DLL":"D3D9.DLL");c.module=LoadLibraryA(path);check(c.module!=NULL,"FAIL module load");c.embedded=embedded;
    IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(c.module,embedded?"Core8CreateFactory":"Direct3DCreate9");check(make!=NULL,"FAIL factory export");
    if(embedded){check(!GetProcAddress(c.module,"Direct3DCreate9")&&!GetProcAddress(c.module,"DebugSetMute")&&!GetProcAddress(c.module,"DllMain"),"FAIL private exports");}
    check(make(0)==NULL,"FAIL SDK token");c.factory=make(embedded?sdk:D3D_SDK_VERSION);check(c.factory!=NULL,"FAIL factory");
    IUnknown*unknown=NULL;expect(IDirect3D9_QueryInterface(c.factory,&IID_IUnknown,(void**)&unknown),"FAIL factory QI");check((void*)unknown==c.factory&&IUnknown_Release(unknown)==1,"FAIL factory identity");
    if(embedded){
        HRESULT(WINAPI*construct)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(c.module,"Core8CreateDevice");check(construct!=NULL,"FAIL constructor export");
        /* PP8 DWORDs correspond to SDK8 compiled consumer offsets,not PP9. */
        struct{DWORD before[4],pp[13],after[4];}input,saved;ZeroMemory(&input,sizeof(input));for(UINT i=0;i<4;i++){input.before[i]=0x1234abcd;input.after[i]=0xdcba4321;}input.pp[2]=21;input.pp[5]=sdk==120?4:1;input.pp[7]=1;saved=input;
        IDirect3DDevice9*bad=(void*)(uintptr_t)1;check(construct(NULL,0,1,window,0x20,input.pp,&bad)==D3DERR_INVALIDCALL&&!bad,"FAIL null factory");bad=(void*)(uintptr_t)1;check(construct(c.factory,0,1,window,0x20,NULL,&bad)==D3DERR_INVALIDCALL&&!bad,"FAIL null PP8");check(construct(c.factory,0,1,window,0x20,input.pp,NULL)==D3DERR_INVALIDCALL,"FAIL null output");
        for(UINT trial=0;trial<2;trial++){
            DWORD pp[13];CopyMemory(pp,input.pp,sizeof(pp));pp[2]=trial?21:24;pp[5]=1;pp[4]=trial?4:0;pp[10]=trial?1:0;IDirect3DDevice9*temporary=NULL;expect(construct(c.factory,0,1,window,0,pp,&temporary),"FAIL legacy temporary constructor");IDirect3DSwapChain9*chain=NULL;expect(IDirect3DDevice9_GetSwapChain(temporary,0,&chain),"FAIL temporary chain");D3DPRESENT_PARAMETERS effective;expect(IDirect3DSwapChain9_GetPresentParameters(chain,&effective),"FAIL temporary parameters");check(effective.BackBufferFormat==(D3DFORMAT)pp[2]&&effective.MultiSampleType==(D3DMULTISAMPLE_TYPE)pp[4]&&effective.MultiSampleQuality==0&&effective.Flags==pp[10],"FAIL legacy format/MSAA/lockable");check(!IDirect3DSwapChain9_Release(chain)&&!IDirect3DDevice9_Release(temporary),"FAIL temporary retirement");
        }
        expect(construct(c.factory,0,1,window,sdk==120?0:0x20,input.pp,&c.device),"FAIL actual backend8 device");check(same_bytes(&input,&saved,sizeof(input)),"FAIL private PP8 input changed");
        IDirect3DSwapChain9*chain=NULL;expect(IDirect3DDevice9_GetSwapChain(c.device,0,&chain),"FAIL main chain");D3DPRESENT_PARAMETERS effective;expect(IDirect3DSwapChain9_GetPresentParameters(chain,&effective),"FAIL main parameters");check(effective.BackBufferWidth==8&&effective.BackBufferHeight==8&&effective.BackBufferCount==1&&effective.BackBufferFormat==21&&effective.hDeviceWindow==window&&(UINT)effective.SwapEffect==(sdk==120?4u:1u)&&!effective.MultiSampleQuality,"FAIL effective PP8");check(!IDirect3DSwapChain9_Release(chain),"FAIL main chain refs");char line[200];wsprintfA(line,"BACKEND8 PARAMETERS sdk=%u immutable52=1 effective=8x8 count=1 copyVsync=%u behavior=%u legacy16bit=1 msaaLockable=1",sdk,sdk==120,sdk==120?0:32);report(line);
    }else{
        D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        expect(IDirect3D9_CreateDevice(c.factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.device),"FAIL ordinary9 device");
    }
    IDirect3D9*parent=NULL;expect(IDirect3DDevice9_GetDirect3D(c.device,&parent),"FAIL backend parent");check(parent==c.factory&&IDirect3D9_Release(parent)==2,"FAIL parent identity/ref");IUnknown*identity=NULL;expect(IDirect3DDevice9_QueryInterface(c.device,&IID_IUnknown,(void**)&identity),"FAIL device QI");check((void*)identity==c.device&&IUnknown_Release(identity)==1,"FAIL device identity");
    DWORD minimum=0xa5a5a5a5;expect(IDirect3DDevice9_GetRenderState(c.device,D3DRS_POINTSIZE_MIN,&minimum),"FAIL initial default");check(minimum==(embedded?0u:0x3f800000u),"FAIL initial API8/API9 point minimum");
    if(embedded){check(IDirect3D9_Release(c.factory)==1,"FAIL early parent release");c.factory=NULL;
        HRESULT(WINAPI*reset8)(IDirect3DDevice9*,const void*)=(void*)(uintptr_t)GetProcAddress(c.module,"Core8Reset");check(reset8!=NULL,"FAIL private Reset export");
        struct{DWORD pre[4],pp[13],post[4];}input,saved;ZeroMemory(&input,sizeof(input));for(UINT i=0;i<4;i++){input.pre[i]=0x1234abcd;input.post[i]=0xdcba4321;}input.pp[5]=sdk==120?4:1;input.pp[7]=1;saved=input;
        check(reset8(c.device,NULL)==D3DERR_INVALIDCALL&&IDirect3DDevice9_TestCooperativeLevel(c.device)==D3DERR_DEVICENOTRESET,"FAIL null Reset status");
        IDirect3DSurface9*held=NULL;expect(IDirect3DDevice9_GetBackBuffer(c.device,0,0,D3DBACKBUFFER_TYPE_MONO,&held),"FAIL held backbuffer");check(reset8(c.device,input.pp)==D3DERR_DEVICELOST&&IDirect3DDevice9_TestCooperativeLevel(c.device)==D3DERR_DEVICENOTRESET,"FAIL held-backbuffer legacy status");check(!IDirect3DSurface9_Release(held),"FAIL held backbuffer release");
        input.pp[12]=1;check(reset8(c.device,input.pp)==D3DERR_INVALIDCALL&&IDirect3DDevice9_TestCooperativeLevel(c.device)==D3DERR_DEVICENOTRESET,"FAIL windowed8 Reset interval");input.pp[12]=0;
        expect(IDirect3DDevice9_SetRenderState(c.device,D3DRS_POINTSIZE_MIN,0x3f800000),"FAIL change old default");expect(reset8(c.device,input.pp),"FAIL actual core Reset8");check(IDirect3DDevice9_TestCooperativeLevel(c.device)==D3D_OK&&same_bytes(&input,&saved,sizeof(input)),"FAIL Reset recovery/input");
        DWORD restored=1;expect(IDirect3DDevice9_GetRenderState(c.device,D3DRS_POINTSIZE_MIN,&restored),"FAIL Reset default query");D3DMATERIAL9 material;expect(IDirect3DDevice9_GetMaterial(c.device,&material),"FAIL Reset material");check(!restored&&material.Diffuse.r==1&&material.Diffuse.g==1&&material.Diffuse.b==1&&!material.Diffuse.a,"FAIL Reset8 initial defaults");
        IDirect3DSwapChain9*chain=NULL;expect(IDirect3DDevice9_GetSwapChain(c.device,0,&chain),"FAIL Reset parameters chain");D3DPRESENT_PARAMETERS effective;expect(IDirect3DSwapChain9_GetPresentParameters(chain,&effective),"FAIL Reset effective parameters");check(effective.BackBufferWidth==8&&effective.BackBufferHeight==8&&effective.BackBufferCount==1&&effective.BackBufferFormat==21&&effective.hDeviceWindow==window&&!effective.MultiSampleQuality&&(UINT)effective.SwapEffect==(sdk==120?4u:1u),"FAIL Reset8 effective geometry");check(!IDirect3DSwapChain9_Release(chain),"FAIL Reset chain release");
        char line[220];wsprintfA(line,"BACKEND8 RESET sdk=%u nullPending=1 blockerLost=1 invalidPending=1 immutable52=1 currentFormat=21 count=1 pointMin=0 material8=1",sdk);report(line);
    }else{
        expect(IDirect3DDevice9_SetRenderState(c.device,D3DRS_POINTSIZE_MIN,0),"FAIL change9 default");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferFormat=21;pp.SwapEffect=1;pp.Windowed=1;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;expect(IDirect3DDevice9_Reset(c.device,&pp),"FAIL ordinary9 Reset");DWORD restored=0;expect(IDirect3DDevice9_GetRenderState(c.device,D3DRS_POINTSIZE_MIN,&restored),"FAIL Reset9 default query");check(pp.BackBufferWidth==8&&pp.BackBufferHeight==8&&pp.BackBufferCount==1&&restored==0x3f800000u,"FAIL ordinary9 Reset publication/default");report("BACKEND8 RESET9 dimensions=8x8 count=1 pointMin=3F800000");
    }
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
    char line[160];wsprintfA(line,"BACKEND8 FRAME step=%u embedded=%u color=%08lX readback=1",step,c->embedded,color);report(line);
}
static void close_context(Context*c)
{
    check(!IDirect3DSurface9_Release(c->cpu)&&!IDirect3DDevice9_Release(c->device)&&(!c->factory||!IDirect3D9_Release(c->factory)),"FAIL backend final refs");check(FreeLibrary(c->module),"FAIL backend module unload");ZeroMemory(c,sizeof(*c));
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"BRESET8.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");
    window=CreateWindowExA(0,"STATIC","Private D3D9 core",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);
    Context dll=create(FALSE);draw(&dll,0,0xffb02010);sdk=120;Context embedded=create(TRUE);draw(&embedded,1,0xff1040c0);close_context(&embedded);draw(&dll,2,0xff20a040);sdk=220;embedded=create(TRUE);draw(&embedded,3,0xffc0a020);close_context(&embedded);draw(&dll,4,0xff304050);close_context(&dll);
    DestroyWindow(window);report("BACKEND8 STATUS privateLoads=2 privateUnloads=2 dllLoads=1 dllUnloads=1 factories=3 devices=7 identityChecks=6 finalRefs=0 frames=5 readbacks=5");report("PASS D3D8 backend Reset coexistence");CloseHandle(logfile);ExitProcess(0);
}
