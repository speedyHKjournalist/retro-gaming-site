/* Adapter wrappers compared with immutable accepted stage4K5 DLLs. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
static HANDLE logfile;static char directory[MAX_PATH];static HWND window;static UINT root_rounds,identity_checks,caps_queries;
static UINT type_queries,ms_queries,depth_queries;
static UINT adapter_queries,mode_queries,software_queries,count_queries,enum_entries,enum_errors;
static HMODULE baseline_module;static IDirect3D8*baseline_root;
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void factory_core_check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s actual=%08lX",s,(DWORD)hr);report(line);ExitProcess(1);}}
void factory_core9_open(const char*directory,HWND window);void factory_core9_draw(DWORD color);void factory_core9_close(void);
static void caps_compare(const D3DCAPS8*actual)
{
    D3DCAPS8 expected;expect(IDirect3D8_GetDeviceCaps(baseline_root,0,D3DDEVTYPE_HAL,&expected),"FAIL baseline caps");const BYTE*a=(const BYTE*)actual,*b=(const BYTE*)&expected;
    for(UINT i=0;i<sizeof(expected);i++){if(a[i]!=b[i]){char line[180];wsprintfA(line,"FAIL caps byte=%u old=%u new=%u",i,b[i],a[i]);report(line);ExitProcess(1);}}caps_queries++;
}
static void baseline_open(void)
{
    char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,"BASE8.DLL");baseline_module=LoadLibraryA(path);factory_core_check(baseline_module!=NULL,"FAIL baseline module");IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(baseline_module,"Direct3DCreate8");factory_core_check(make!=NULL,"FAIL baseline export");baseline_root=make(220);factory_core_check(baseline_root!=NULL,"FAIL baseline root");
    D3DCAPS8 caps;expect(IDirect3D8_GetDeviceCaps(baseline_root,0,D3DDEVTYPE_HAL,&caps),"FAIL baseline words");DWORD words[53];CopyMemory(words,&caps,sizeof(words));char line[640],word[16];lstrcpyA(line,"CHECKCORE BASE words=53 values=");for(UINT i=0;i<53;i++){wsprintfA(word,"%08lX",words[i]);lstrcatA(line,word);if(i<52)lstrcatA(line,",");}report(line);
}
void factory_core_report(const char*s){report(s);}
static BOOL same_bytes(const void*a,const void*b,UINT bytes){for(UINT i=0;i<bytes;i++){if(((const BYTE*)a)[i]!=((const BYTE*)b)[i])return FALSE;}return TRUE;}
static void adapter_compare(IDirect3D8*r,UINT round)
{
    const DWORD flags[]={0,D3DENUM_NO_WHQL_LEVEL,0xffffffffu,0x80000000u};struct{DWORD before[4];D3DADAPTER_IDENTIFIER8 value;DWORD after[32];}actual;D3DADAPTER_IDENTIFIER8 expected;FillMemory(&actual,sizeof(actual),0xa5);
    expect(IDirect3D8_GetAdapterIdentifier(r,0,flags[round%4],&actual.value),"FAIL D8 identifier");expect(IDirect3D8_GetAdapterIdentifier(baseline_root,0,flags[round%4],&expected),"FAIL baseline identifier");factory_core_check(same_bytes(&actual.value,&expected,sizeof(expected)),"FAIL D8 identifier bytes");for(UINT i=0;i<4;i++){factory_core_check(actual.before[i]==0xa5a5a5a5u,"FAIL identifier underflow");}for(UINT i=0;i<32;i++){factory_core_check(actual.after[i]==0xa5a5a5a5u,"FAIL identifier overflow");}adapter_queries++;
    FillMemory(&actual.value,sizeof(actual.value),0xa5);FillMemory(&expected,sizeof(expected),0xa5);factory_core_check(IDirect3D8_GetAdapterIdentifier(r,1,0,&actual.value)==D3DERR_INVALIDCALL&&same_bytes(&actual.value,&expected,sizeof(expected))&&IDirect3D8_GetAdapterIdentifier(r,0,0,NULL)==D3DERR_INVALIDCALL,"FAIL identifier error output");
    D3DDISPLAYMODE mode,old;expect(IDirect3D8_GetAdapterDisplayMode(r,0,&mode),"FAIL D8 mode");expect(IDirect3D8_GetAdapterDisplayMode(baseline_root,0,&old),"FAIL baseline mode");factory_core_check(same_bytes(&mode,&old,sizeof(mode)),"FAIL D8 mode bytes");mode_queries++;
    for(UINT i=0;i<2;i++){void*callback=(void*)(uintptr_t)i;factory_core_check(IDirect3D8_RegisterSoftwareDevice(r,callback)==D3DERR_INVALIDCALL&&IDirect3D8_RegisterSoftwareDevice(baseline_root,callback)==D3DERR_INVALIDCALL,"FAIL software registration");software_queries++;}
}
static void enumeration_compare(IDirect3D8*r)
{
    UINT count=IDirect3D8_GetAdapterModeCount(r,0);factory_core_check(count==9&&count==IDirect3D8_GetAdapterModeCount(baseline_root,0),"FAIL enumeration count");count_queries++;
    for(UINT i=0;i<count;i++){
        struct{DWORD before[4];D3DDISPLAYMODE value;DWORD after[4];}actual;D3DDISPLAYMODE expected;FillMemory(&actual,sizeof(actual),0xa5);expect(IDirect3D8_EnumAdapterModes(r,0,i,&actual.value),"FAIL enum mode");expect(IDirect3D8_EnumAdapterModes(baseline_root,0,i,&expected),"FAIL baseline enum mode");factory_core_check(same_bytes(&actual.value,&expected,sizeof(expected)),"FAIL mode order/format/refresh bytes");for(UINT j=0;j<4;j++){factory_core_check(actual.before[j]==0xa5a5a5a5u&&actual.after[j]==0xa5a5a5a5u,"FAIL enum mode guard");}enum_entries++;
    }
    const UINT adapters[]={1,0xffffffffu};for(UINT i=0;i<2;i++){factory_core_check(!IDirect3D8_GetAdapterModeCount(r,adapters[i])&&!IDirect3D8_GetAdapterModeCount(baseline_root,adapters[i]),"FAIL invalid adapter mode count");count_queries++;}
    const UINT bad_adapter[]={0,0,1,0xffffffffu,0},bad_index[]={9,0xffffffffu,0,0,0};for(UINT i=0;i<5;i++){
        D3DDISPLAYMODE actual,expected;FillMemory(&actual,sizeof(actual),0xa5);FillMemory(&expected,sizeof(expected),0xa5);factory_core_check(IDirect3D8_EnumAdapterModes(r,bad_adapter[i],bad_index[i],i==4?NULL:&actual)==D3DERR_INVALIDCALL&&IDirect3D8_EnumAdapterModes(baseline_root,bad_adapter[i],bad_index[i],i==4?NULL:&expected)==D3DERR_INVALIDCALL&&same_bytes(&actual,&expected,sizeof(actual)),"FAIL enum invalid output");enum_errors++;
    }
}
static void capability_compare(IDirect3D8*r,UINT round)
{
    const UINT displays[]={22,22,23,24,24,24,21,0,22,0xffffffffu,24,23},backbuffers[]={21,22,23,24,25,26,21,21,111,22,23,26};for(UINT i=0;i<12;i++){
        UINT adapter=i==10?1:0;D3DDEVTYPE type=i==11?D3DDEVTYPE_REF:D3DDEVTYPE_HAL;BOOL w=round%3==0?FALSE:round%3==1?TRUE:-1;factory_core_check(IDirect3D8_CheckDeviceType(r,adapter,type,displays[i],backbuffers[i],w)==IDirect3D8_CheckDeviceType(baseline_root,adapter,type,displays[i],backbuffers[i],w),"FAIL CheckDeviceType baseline");type_queries++;
    }
    const UINT formats[]={0,21,24,70,80,111,0x5a544e49u,0xffffffffu};for(UINT i=0;i<20;i++){
        UINT sample=i==18?0xffffffffu:i==19?4:i,adapter=i==19?1:0;factory_core_check(IDirect3D8_CheckDeviceMultiSampleType(r,adapter,D3DDEVTYPE_HAL,formats[round%8],round%2,sample)==IDirect3D8_CheckDeviceMultiSampleType(baseline_root,adapter,D3DDEVTYPE_HAL,formats[round%8],round%2,sample),"FAIL MSAA baseline");ms_queries++;
    }
    const UINT depths[]={80,75,71,73,77,79,70,0,0x5a544e49u,80};for(UINT i=0;i<10;i++){
        D3DDEVTYPE type=i==9?D3DDEVTYPE_REF:D3DDEVTYPE_HAL;factory_core_check(IDirect3D8_CheckDepthStencilMatch(r,0,type,formats[round%8],formats[(round+1)%8],depths[i])==IDirect3D8_CheckDepthStencilMatch(baseline_root,0,type,formats[round%8],formats[(round+1)%8],depths[i]),"FAIL depth baseline");depth_queries++;
    }
}
static void baseline_close(void){factory_core_check(!IDirect3D8_Release(baseline_root)&&FreeLibrary(baseline_module),"FAIL baseline final refs");}
typedef struct{HMODULE module;IDirect3D8*factory;IDirect3DDevice8*device;IDirect3DSurface8*cpu;}Context;
static Context create(UINT sdk)
{
    Context c;ZeroMemory(&c,sizeof(c));char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,"D3D8.DLL");c.module=LoadLibraryA(path);factory_core_check(c.module!=NULL,"FAIL D8 module");IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(c.module,"Direct3DCreate8");factory_core_check(make!=NULL,"FAIL D8 export");factory_core_check(!GetProcAddress(c.module,"Direct3DCreate9")&&!GetProcAddress(c.module,"d9wg_d3d9_core_create8"),"FAIL private ABI leaked");factory_core_check(!make(0)&&!make(120|0x80000000u),"FAIL SDK boundary");
    for(UINT i=0;i<64;i++){
        SetEnvironmentVariableA("D9WG_CAPS_PROFILE",i%4==1?"ffp":i%4==2?"sm2":"sm3");SetEnvironmentVariableA("D9WG_CAPS_PERMISSIVE",i%4==3?"1":"0");
        IDirect3D8*r=make(i%2?120:220);factory_core_check(r!=NULL,"FAIL root cycle");factory_core_check(IDirect3D8_GetAdapterCount(r)==1&&IDirect3D8_GetAdapterMonitor(r,0)==MonitorFromWindow(NULL,MONITOR_DEFAULTTOPRIMARY)&&!IDirect3D8_GetAdapterMonitor(r,0xffffffffu),"FAIL delegated adapter queries");
        adapter_compare(r,i);enumeration_compare(r);capability_compare(r,i);
        D3DCAPS8 caps;expect(IDirect3D8_GetDeviceCaps(r,0,D3DDEVTYPE_HAL,&caps),"FAIL core root caps");caps_compare(&caps);
        IUnknown*unknown=NULL;expect(IDirect3D8_QueryInterface(r,&IID_IUnknown,(void**)&unknown),"FAIL root IUnknown");factory_core_check((void*)unknown==r&&IUnknown_Release(unknown)==1,"FAIL root IUnknown identity");IDirect3D8*same=NULL;expect(IDirect3D8_QueryInterface(r,&IID_IDirect3D8,(void**)&same),"FAIL root QI");factory_core_check(same==r&&IDirect3D8_Release(same)==1&&!IDirect3D8_Release(r),"FAIL root public refs");root_rounds++;identity_checks+=2;
    }
    SetEnvironmentVariableA("D9WG_CAPS_PROFILE","sm3");SetEnvironmentVariableA("D9WG_CAPS_PERMISSIVE","0");
    c.factory=make(sdk);factory_core_check(c.factory!=NULL,"FAIL context root");IUnknown*unknown=NULL;expect(IDirect3D8_QueryInterface(c.factory,&IID_IUnknown,(void**)&unknown),"FAIL context QI");factory_core_check((void*)unknown==c.factory&&IUnknown_Release(unknown)==1,"FAIL context identity");identity_checks++;
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.FullScreen_PresentationInterval=D3DPRESENT_INTERVAL_DEFAULT;expect(IDirect3D8_CreateDevice(c.factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.device),"FAIL D8 device");IDirect3D8*parent=NULL;expect(IDirect3DDevice8_GetDirect3D(c.device,&parent),"FAIL D8 parent");factory_core_check(parent==c.factory&&IDirect3D8_Release(parent)==2,"FAIL D8 parent identity");identity_checks++;
    D3DCAPS8 device_caps;expect(IDirect3DDevice8_GetDeviceCaps(c.device,&device_caps),"FAIL core device caps");caps_compare(&device_caps);D3DDISPLAYMODE device_mode,old_mode;expect(IDirect3DDevice8_GetDisplayMode(c.device,&device_mode),"FAIL device display mode");expect(IDirect3D8_GetAdapterDisplayMode(baseline_root,0,&old_mode),"FAIL baseline device mode");factory_core_check(same_bytes(&device_mode,&old_mode,sizeof(device_mode)),"FAIL device mode bytes");mode_queries++;
    factory_core_check(IDirect3D8_Release(c.factory)==1,"FAIL root survives by device");expect(IDirect3DDevice8_GetDirect3D(c.device,&parent),"FAIL D8 device-only root");factory_core_check(parent==c.factory&&IDirect3D8_GetAdapterCount(parent)==1&&IDirect3D8_Release(parent)==1,"FAIL backend root lost with device owner");identity_checks++;
    expect(IDirect3DDevice8_CreateImageSurface(c.device,8,8,D3DFMT_A8R8G8B8,&c.cpu),"FAIL D8 CPU");expect(IDirect3DDevice8_SetVertexShader(c.device,68),"FAIL D8 FVF");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_ZENABLE,FALSE),"FAIL D8 Z");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_LIGHTING,FALSE),"FAIL D8 lighting");expect(IDirect3DDevice8_SetRenderState(c.device,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL D8 cull");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL D8 color op");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL D8 color arg");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL D8 alpha op");expect(IDirect3DDevice8_SetTextureStageState(c.device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL D8 alpha arg");return c;
}
static void frame(UINT step,UINT api,DWORD color){char line[160];wsprintfA(line,"CHECKCORE FRAME step=%u api=%u color=%08lX readback=1",step,api,color);report(line);}
static void draw(Context*c,UINT step,DWORD color)
{
    typedef struct{float x,y,z,w;DWORD color;}Vertex;Vertex quad[4]={{-.5f,-.5f,.5f,1,color},{7.5f,-.5f,.5f,1,color},{-.5f,7.5f,.5f,1,color},{7.5f,7.5f,.5f,1,color}},v[6];const UINT order[6]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=quad[order[i]];
    expect(IDirect3DDevice8_BeginScene(c->device),"FAIL D8 scene");expect(IDirect3DDevice8_DrawPrimitiveUP(c->device,D3DPT_TRIANGLELIST,2,v,20),"FAIL D8 draw");expect(IDirect3DDevice8_EndScene(c->device),"FAIL D8 end");IDirect3DSurface8*target=NULL;expect(IDirect3DDevice8_GetRenderTarget(c->device,&target),"FAIL D8 target");expect(IDirect3DDevice8_CopyRects(c->device,target,NULL,0,c->cpu,NULL),"FAIL D8 readback");IDirect3DSurface8_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface8_LockRect(c->cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL D8 lock");expect(IDirect3DSurface8_UnlockRect(c->cpu),"FAIL D8 unlock");frame(step,8,color);
}
static void close_context(Context*c){factory_core_check(!IDirect3DSurface8_Release(c->cpu)&&!IDirect3DDevice8_Release(c->device),"FAIL D8 final device/root refs");factory_core_check(FreeLibrary(c->module),"FAIL D8 unload");}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"CHECK8.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);factory_core_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");window=CreateWindowExA(0,"STATIC","D3D8 factory core",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);factory_core_check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);
    baseline_open();Context eight=create(120);draw(&eight,0,0xffb02010);factory_core9_open(directory,window);identity_checks+=2;factory_core9_draw(0xff1040c0);frame(1,9,0xff1040c0);close_context(&eight);factory_core9_draw(0xff20a040);frame(2,9,0xff20a040);eight=create(220);draw(&eight,3,0xffc0a020);close_context(&eight);factory_core9_draw(0xff304050);frame(4,9,0xff304050);factory_core9_close();baseline_close();DestroyWindow(window);
    factory_core_check(root_rounds==128&&identity_checks==264&&caps_queries==130&&adapter_queries==128&&mode_queries==130&&software_queries==256&&count_queries==384&&enum_entries==1152&&enum_errors==640&&type_queries==1536&&ms_queries==2560&&depth_queries==1280,"FAIL counters");report("CHECKCORE ENUM lists=128 countQueries=384 entries=1152 invalidQueries=640 formats=24,23,22 widths=640,800,1024 refresh=60 baseline=4K5");report("CHECKCORE CHECKS typeQueries=1536 msaaQueries=2560 depthQueries=1280 d9TypeQueries=12 d9MsaaQueries=160 d9DepthQueries=10 baseline=4K5");report("CHECKCORE STATUS rootRounds=128 identityChecks=264 adapterQueries=128 identifierBytes=1068 modeQueries=130 softwareQueries=256 d9IdentifierQueries=4 d9IdentifierBytes=1100 d9ModeQueries=1 d9SoftwareQueries=2 capsQueries=130 capsWords=53 envProfiles=4 baseline=4K5 sdk120=1 sdk220=1 d8Loads=2 d8Unloads=2 d9Loads=1 d9Unloads=1 devices=3 factoryRefs=0 frames=5 readbacks=5");report("PASS D3D8 checks core delegation");CloseHandle(logfile);ExitProcess(0);
}
