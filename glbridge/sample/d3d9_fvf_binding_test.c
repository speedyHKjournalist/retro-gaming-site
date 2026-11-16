/* D3D9 public-zero declaration/VS/PS binding lifetime and Reset boundaries. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s actual=%08lX",s,(DWORD)hr);report(line);ExitProcess(1);}}
static ULONG refs(IUnknown*p){IUnknown_AddRef(p);return IUnknown_Release(p);}
static void capture(IDirect3DDevice9*d,IDirect3DSurface9*cpu,UINT step,DWORD color)
{
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"FVF FRAME step=%u color=%08lX readback=1",step,color);report(line);
}


static UINT metadata_rounds,loop_switches,loop_revivals;
static const DWORD keys[6]={2,4,16,68,0x112,0x1c4};
static const UINT counts[6]={1,1,1,2,3,4};
static const D3DVERTEXELEMENT9 wanted[6][4]={{{0,0,2,0,0,0}},{{0,0,3,0,9,0}},{{0,0,2,0,3,0}},{{0,0,3,0,9,0},{0,16,4,0,10,0}},{{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,1,0,5,0}},{{0,0,3,0,9,0},{0,16,4,0,10,0},{0,20,4,0,10,1},{0,24,1,0,5,0}}};
static BOOL equal(const void*a,const void*b,UINT n){for(UINT i=0;i<n;i++)if(((const BYTE*)a)[i]!=((const BYTE*)b)[i])return FALSE;return TRUE;}
static void metadata(IDirect3DVertexDeclaration9*decl,UINT index)
{
    D3DVERTEXELEMENT9 out[MAXD3DDECLLENGTH+1],end=D3DDECL_END();UINT n=0;expect(IDirect3DVertexDeclaration9_GetDeclaration(decl,NULL,&n),"FAIL metadata count");check(n==counts[index]+1,"FAIL cached element count");n=0;expect(IDirect3DVertexDeclaration9_GetDeclaration(decl,out,&n),"FAIL cached metadata");check(n==counts[index]+1&&equal(out,wanted[index],counts[index]*sizeof(*out))&&equal(&out[counts[index]],&end,sizeof(end)),"FAIL independent cached layout");metadata_rounds++;
}
static void fvf_value(IDirect3DDevice9*d,DWORD wanted_value){DWORD value=0xdeadbeef;expect(IDirect3DDevice9_GetFVF(d,&value),"FAIL GetFVF");check(value==wanted_value,"FAIL FVF roundtrip");}
typedef struct Vertex{float x,y,z,w;DWORD color;}Vertex;
typedef struct WideVertex{float x,y,z,w;DWORD color,specular;float u,v;}WideVertex;
static void draw(IDirect3DDevice9*d,DWORD color,BOOL wide)
{
    Vertex quad[4]={{-.5f,-.5f,.5f,1,color},{7.5f,-.5f,.5f,1,color},{-.5f,7.5f,.5f,1,color},{7.5f,7.5f,.5f,1,color}},triangles[6];WideVertex expanded[6];const UINT order[6]={0,1,2,2,1,3};for(UINT i=0;i<6;i++){triangles[i]=quad[order[i]];expanded[i]=(WideVertex){triangles[i].x,triangles[i].y,.5f,1,color,0,.25f,.75f};}expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,wide?(const void*)expanded:(const void*)triangles,wide?sizeof(WideVertex):sizeof(Vertex)),"FAIL cached FVF draw");expect(IDirect3DDevice9_EndScene(d),"FAIL end scene");
}
static void scene_state(IDirect3DDevice9*d)
{
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_LIGHTING,FALSE),"FAIL lighting");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL color op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL color arg");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL alpha op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL alpha arg");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"FVF9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","FVF declaration cache",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");


    ULONG baseline=refs((IUnknown*)d);IDirect3DVertexDeclaration9*views[6]={0};
    for(UINT i=0;i<6;i++){expect(IDirect3DDevice9_SetFVF(d,keys[i]),"FAIL initial FVF");expect(IDirect3DDevice9_GetVertexDeclaration(d,&views[i]),"FAIL initial converted declaration");check(views[i]&&refs((IUnknown*)views[i])==1,"FAIL initial public view");metadata(views[i],i);fvf_value(d,keys[i]);}
    check(refs((IUnknown*)d)==baseline+6,"FAIL cached views device pins");for(UINT i=0;i<6;i++){check(!IDirect3DVertexDeclaration9_Release(views[i]),"FAIL cache public zero");}check(refs((IUnknown*)d)==baseline,"FAIL cache owns public parent pins");
    for(UINT round=0;round<128;round++)for(UINT i=0;i<6;i++){expect(IDirect3DDevice9_SetFVF(d,keys[i]),"FAIL cached switch");loop_switches++;IDirect3DVertexDeclaration9*view=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL cached revival");loop_revivals++;check(view==views[i]&&refs((IUnknown*)view)==1&&refs((IUnknown*)d)==baseline+1,"FAIL cached identity/device pin");metadata(view,i);IUnknown*unknown=NULL;expect(IDirect3DVertexDeclaration9_QueryInterface(view,&IID_IUnknown,(void**)&unknown),"FAIL cached QI");check((void*)unknown==view&&IUnknown_Release(unknown)==1,"FAIL cached QI identity");fvf_value(d,keys[i]);check(!IDirect3DVertexDeclaration9_Release(view)&&refs((IUnknown*)d)==baseline,"FAIL cached revival debt");}
    scene_state(d);expect(IDirect3DDevice9_SetFVF(d,68),"FAIL red FVF");expect(IDirect3DDevice9_SetFVF(d,0),"FAIL zero FVF");fvf_value(d,68);IDirect3DVertexDeclaration9*view=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL zero retained view");check(view==views[3],"FAIL zero changes declaration");metadata(view,3);check(!IDirect3DVertexDeclaration9_Release(view),"FAIL zero view refs");draw(d,0xffb02010,FALSE);capture(d,cpu,0,0xffb02010);
    expect(IDirect3DDevice9_SetVertexDeclaration(d,views[3]),"FAIL direct cached declaration");fvf_value(d,68);draw(d,0xff1040c0,FALSE);capture(d,cpu,1,0xff1040c0);
    expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL recorded Begin");expect(IDirect3DDevice9_SetFVF(d,keys[5]),"FAIL recorded converted FVF");expect(IDirect3DDevice9_SetFVF(d,0),"FAIL recorded zero no-op");fvf_value(d,68);IDirect3DStateBlock9*block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL recorded End");expect(IDirect3DStateBlock9_Apply(block),"FAIL converted Apply");fvf_value(d,keys[5]);expect(IDirect3DStateBlock9_Capture(block),"FAIL converted Capture");expect(IDirect3DDevice9_SetFVF(d,68),"FAIL temporary switch");expect(IDirect3DStateBlock9_Apply(block),"FAIL converted reapply");fvf_value(d,keys[5]);draw(d,0xff20a040,TRUE);capture(d,cpu,2,0xff20a040);check(!IDirect3DStateBlock9_Release(block),"FAIL block refs");
    IDirect3DVertexDeclaration9*held=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&held),"FAIL Reset held cache view");check(held==views[5],"FAIL held cache identity");expect(IDirect3DDevice9_Reset(d,&pp),"FAIL public cache Reset");view=(void*)1;expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL Reset empty declaration");check(!view,"FAIL Reset keeps current declaration");fvf_value(d,0);metadata(held,5);scene_state(d);expect(IDirect3DDevice9_SetFVF(d,keys[5]),"FAIL Reset cached switch");expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL Reset cached view");check(view==held&&view==views[5]&&refs((IUnknown*)view)==2&&refs((IUnknown*)d)==baseline+1,"FAIL Reset recreates declaration");metadata(view,5);check(IDirect3DVertexDeclaration9_Release(view)==1&&!IDirect3DVertexDeclaration9_Release(held)&&refs((IUnknown*)d)==baseline,"FAIL Reset cache public debt");draw(d,0xffc0a020,TRUE);capture(d,cpu,3,0xffc0a020);
    D3DVERTEXELEMENT9 user_elements[3]={wanted[3][0],wanted[3][1],D3DDECL_END()};IDirect3DVertexDeclaration9*user=NULL;expect(IDirect3DDevice9_CreateVertexDeclaration(d,user_elements,&user),"FAIL user declaration");expect(IDirect3DDevice9_SetVertexDeclaration(d,user),"FAIL user bind");expect(IDirect3DDevice9_SetFVF(d,0),"FAIL zero on user declaration");view=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL zero user view");check(view==user,"FAIL zero replaces user declaration");metadata(view,3);check(IDirect3DVertexDeclaration9_Release(view)==1&&!IDirect3DVertexDeclaration9_Release(user),"FAIL user refs");expect(IDirect3DDevice9_Reset(d,&pp),"FAIL second Reset");expect(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff304050,1,0),"FAIL post Reset clear");capture(d,cpu,4,0xff304050);
    check(loop_switches==768&&loop_revivals==768&&metadata_rounds==778,"FAIL probe counters");check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final refs");FreeLibrary(module);DestroyWindow(window);char line[220];wsprintfA(line,"FVF STATUS cacheDeclarations=6 userDeclarations=1 layouts=6 loopSwitches=%u loopRevivals=%u metadataRounds=%u applies=2 captures=1 resetSuccesses=2 frames=5 readbacks=5 factoryRefs=0",loop_switches,loop_revivals,metadata_rounds);report(line);report("PASS D3D9 FVF declaration cache");CloseHandle(logfile);ExitProcess(0);
}
