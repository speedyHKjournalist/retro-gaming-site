/* Windows POSITION-only user association and distinct converted FVF cache. */
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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"DECL FRAME step=%u color=%08lX readback=1",step,color);report(line);
}


static UINT metadata_rounds,queries,pairs;
static const D3DVERTEXELEMENT9 layouts[7][2]={
    {{0,0,2,0,0,0},D3DDECL_END()},{{0,0,3,0,9,0},D3DDECL_END()},
    {{0,0,2,0,3,0},D3DDECL_END()},{{0,0,4,0,10,0},D3DDECL_END()},
    {{0,0,1,0,5,0},D3DDECL_END()},{{0,0,2,0,0,1},D3DDECL_END()},
    {D3DDECL_END(),D3DDECL_END()}};
static BOOL equal(const void*a,const void*b,UINT n){for(UINT i=0;i<n;i++)if(((const BYTE*)a)[i]!=((const BYTE*)b)[i])return FALSE;return TRUE;}
static void metadata(IDirect3DVertexDeclaration9*decl,UINT index)
{
    D3DVERTEXELEMENT9 out[MAXD3DDECLLENGTH+1];UINT n=0,wanted=index==6?1:2;
    expect(IDirect3DVertexDeclaration9_GetDeclaration(decl,NULL,&n),"FAIL metadata count");check(n==wanted,"FAIL declaration count");
    expect(IDirect3DVertexDeclaration9_GetDeclaration(decl,out,&n),"FAIL metadata");check(n==wanted&&equal(out,layouts[index],n*sizeof(*out)),"FAIL independent declaration layout");metadata_rounds++;
}
static void query(IDirect3DDevice9*d,IDirect3DVertexDeclaration9*wanted,DWORD key,UINT index,ULONG public_refs)
{
    DWORD value=0xdeadbeef;expect(IDirect3DDevice9_GetFVF(d,&value),"FAIL GetFVF");check(value==key,"FAIL user/cached FVF association");
    IDirect3DVertexDeclaration9*view=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&view),"FAIL GetVertexDeclaration");check(view==wanted,"FAIL user/cache identity collision");
    if(view){check(refs((IUnknown*)view)==public_refs+1,"FAIL declaration public refs");metadata(view,index);check(IDirect3DVertexDeclaration9_Release(view)==public_refs,"FAIL query refs");}queries++;
}
static void scene_state(IDirect3DDevice9*d)
{
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_LIGHTING,FALSE),"FAIL lighting");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");
    expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"FAIL color op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_TFACTOR),"FAIL color arg");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),"FAIL alpha op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR),"FAIL alpha arg");
}
static void draw(IDirect3DDevice9*d,DWORD color,BOOL transformed)
{
    const float xyz[6][3]={{-1.25f,1.25f,.5f},{1.25f,1.25f,.5f},{-1.25f,-1.25f,.5f},{-1.25f,-1.25f,.5f},{1.25f,1.25f,.5f},{1.25f,-1.25f,.5f}};
    const float rhw[6][4]={{-.5f,-.5f,.5f,1},{7.5f,-.5f,.5f,1},{-.5f,7.5f,.5f,1},{-.5f,7.5f,.5f,1},{7.5f,-.5f,.5f,1},{7.5f,7.5f,.5f,1}};
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_TEXTUREFACTOR,color),"FAIL texture factor");expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,transformed?(const void*)rhw:(const void*)xyz,transformed?16:12),"FAIL position-only draw");expect(IDirect3DDevice9_EndScene(d),"FAIL end scene");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"DECL9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","User declaration FVF",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");


    ULONG baseline=refs((IUnknown*)d);IDirect3DVertexDeclaration9 *xyz=NULL,*rhw=NULL,*cache_xyz=NULL,*cache_rhw=NULL;
    expect(IDirect3DDevice9_CreateVertexDeclaration(d,layouts[0],&xyz),"FAIL user XYZ");expect(IDirect3DDevice9_SetVertexDeclaration(d,xyz),"FAIL bind XYZ");query(d,xyz,2,0,1);check(refs((IUnknown*)d)==baseline+1,"FAIL user parent pin");
    scene_state(d);draw(d,0xffb02010,FALSE);capture(d,cpu,0,0xffb02010);
    expect(IDirect3DDevice9_SetFVF(d,0),"FAIL zero FVF");query(d,xyz,2,0,1);
    expect(IDirect3DDevice9_SetFVF(d,2),"FAIL cache XYZ");expect(IDirect3DDevice9_GetVertexDeclaration(d,&cache_xyz),"FAIL cache XYZ view");check(cache_xyz!=xyz,"FAIL user first aliases cache");check(!IDirect3DVertexDeclaration9_Release(cache_xyz),"FAIL cache XYZ public zero");
    for(UINT i=0;i<64;i++){expect(IDirect3DDevice9_SetVertexDeclaration(d,xyz),"FAIL loop user XYZ");query(d,xyz,2,0,1);expect(IDirect3DDevice9_SetFVF(d,2),"FAIL loop cache XYZ");query(d,cache_xyz,2,0,0);pairs++;}
    check(refs((IUnknown*)d)==baseline+1,"FAIL XYZ loop pin debt");draw(d,0xff1040c0,FALSE);capture(d,cpu,1,0xff1040c0);check(!IDirect3DVertexDeclaration9_Release(xyz),"FAIL user XYZ release");
    expect(IDirect3DDevice9_SetFVF(d,4),"FAIL precreate cache RHW");expect(IDirect3DDevice9_GetVertexDeclaration(d,&cache_rhw),"FAIL cache RHW view");check(!IDirect3DVertexDeclaration9_Release(cache_rhw),"FAIL cache RHW public zero");
    expect(IDirect3DDevice9_CreateVertexDeclaration(d,layouts[1],&rhw),"FAIL user RHW");check(rhw!=cache_rhw,"FAIL cache first aliases user");expect(IDirect3DDevice9_SetVertexDeclaration(d,rhw),"FAIL bind user RHW");query(d,rhw,4,1,1);draw(d,0xff20a040,TRUE);capture(d,cpu,2,0xff20a040);
    expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL Begin");expect(IDirect3DDevice9_SetVertexDeclaration(d,rhw),"FAIL record user RHW");IDirect3DStateBlock9*block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL End");check(!IDirect3DVertexDeclaration9_Release(rhw),"FAIL recorded user public zero");
    expect(IDirect3DDevice9_SetFVF(d,4),"FAIL cache with recorded user");query(d,cache_rhw,4,1,0);expect(IDirect3DStateBlock9_Apply(block),"FAIL Apply user FVF");query(d,rhw,4,1,0);expect(IDirect3DStateBlock9_Capture(block),"FAIL Capture user FVF");
    for(UINT i=0;i<64;i++){expect(IDirect3DDevice9_SetVertexDeclaration(d,rhw),"FAIL loop user RHW");query(d,rhw,4,1,0);expect(IDirect3DDevice9_SetFVF(d,4),"FAIL loop cache RHW");query(d,cache_rhw,4,1,0);pairs++;}
    check(refs((IUnknown*)d)==baseline+1,"FAIL recorded user pin debt");draw(d,0xffc0a020,TRUE);capture(d,cpu,3,0xffc0a020);check(!IDirect3DStateBlock9_Release(block),"FAIL block release");
    for(UINT i=2;i<7;i++){IDirect3DVertexDeclaration9*user=NULL;expect(IDirect3DDevice9_CreateVertexDeclaration(d,layouts[i],&user),"FAIL negative declaration");expect(IDirect3DDevice9_SetVertexDeclaration(d,user),"FAIL bind negative declaration");query(d,user,0,i,1);expect(IDirect3DDevice9_SetVertexDeclaration(d,NULL),"FAIL unbind negative declaration");check(!IDirect3DVertexDeclaration9_Release(user),"FAIL negative declaration release");}
    check(refs((IUnknown*)d)==baseline,"FAIL negative declaration debt");check(IDirect3DVertexDeclaration9_AddRef(cache_xyz)==1,"FAIL retain public cache");expect(IDirect3DDevice9_Reset(d,&pp),"FAIL public declaration Reset");query(d,NULL,0,0,0);metadata(cache_xyz,0);
    expect(IDirect3DDevice9_SetFVF(d,2),"FAIL Reset XYZ cache");query(d,cache_xyz,2,0,1);check(!IDirect3DVertexDeclaration9_Release(cache_xyz),"FAIL held cache release");expect(IDirect3DDevice9_SetFVF(d,4),"FAIL Reset RHW cache");query(d,cache_rhw,4,1,0);scene_state(d);draw(d,0xff304050,TRUE);capture(d,cpu,4,0xff304050);
    check(pairs==128&&queries==269&&metadata_rounds==269,"FAIL counters");check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final refs");FreeLibrary(module);DestroyWindow(window);
    char line[240];wsprintfA(line,"DECL STATUS userDeclarations=7 cacheDeclarations=2 collisionPairs=%u queries=%u metadataRounds=%u applies=1 captures=1 resetSuccesses=1 frames=5 readbacks=5 factoryRefs=0",pairs,queries,metadata_rounds);report(line);report("PASS D3D9 user declaration FVF");CloseHandle(logfile);ExitProcess(0);
}
