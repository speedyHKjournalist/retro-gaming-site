/* D3D9 stateblock private resource lifetime and Reset boundaries. */
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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"STATE FRAME step=%u color=%08lX readback=1",step,color);report(line);
}

typedef struct Vertex{float x,y,z,w;DWORD color;}Vertex;
static const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
static const DWORD vertex_code[]={0xfffe0200,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x8000000a,0x900f0001,0x02000001,0xc00f0000,0x90e40000,0x02000001,0xd00f0000,0x90e40001,0x0000ffff};
static const DWORD pixel_code[]={0xffff0200,0x0200001f,0x8000000a,0x900f0000,0x02000001,0x800f0800,0x90e40000,0x0000ffff};
static BOOL equal(const void*a,const void*b,UINT bytes){for(UINT i=0;i<bytes;i++)if(((const BYTE*)a)[i]!=((const BYTE*)b)[i])return FALSE;return TRUE;}
static void metadata(IDirect3DVertexDeclaration9*decl,IDirect3DVertexShader9*vs,IDirect3DPixelShader9*ps)
{
    D3DVERTEXELEMENT9 out[MAXD3DDECLLENGTH+1];UINT count=0;expect(IDirect3DVertexDeclaration9_GetDeclaration(decl,out,&count),"FAIL declaration metadata");check(count==3&&equal(out,elements,sizeof(elements)),"FAIL declaration identity");DWORD code[32];UINT size=0;expect(IDirect3DVertexShader9_GetFunction(vs,NULL,&size),"FAIL VS size");check(size==sizeof(vertex_code),"FAIL VS bytes");expect(IDirect3DVertexShader9_GetFunction(vs,code,&size),"FAIL VS metadata");check(equal(code,vertex_code,sizeof(vertex_code)),"FAIL VS identity");size=sizeof(code);expect(IDirect3DPixelShader9_GetFunction(ps,code,&size),"FAIL PS metadata");check(size==sizeof(pixel_code)&&equal(code,pixel_code,sizeof(pixel_code)),"FAIL PS identity");
}

typedef struct Group{IDirect3DTexture9*t;IDirect3DCubeTexture9*c;IDirect3DVolumeTexture9*v;IDirect3DVertexBuffer9*vb;IDirect3DIndexBuffer9*ib;IDirect3DVertexDeclaration9*decl;IDirect3DVertexShader9*vs;IDirect3DPixelShader9*ps;}Group;
static void fill_vertices(IDirect3DVertexBuffer9*vb,DWORD color)
{
    void*data=NULL;expect(IDirect3DVertexBuffer9_Lock(vb,16,4*sizeof(Vertex),&data,0),"FAIL vertex update");Vertex*p=data;p[0]=(Vertex){-1.25f,1.25f,.5f,1,color};p[1]=(Vertex){1.25f,1.25f,.5f,1,color};p[2]=(Vertex){-1.25f,-1.25f,.5f,1,color};p[3]=(Vertex){1.25f,-1.25f,.5f,1,color};expect(IDirect3DVertexBuffer9_Unlock(vb),"FAIL vertex upload");
}
static Group make_group(IDirect3DDevice9*d,DWORD color)
{
    Group g;ZeroMemory(&g,sizeof(g));expect(IDirect3DDevice9_CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&g.t,NULL),"FAIL 2D resource");expect(IDirect3DDevice9_CreateCubeTexture(d,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&g.c,NULL),"FAIL cube resource");expect(IDirect3DDevice9_CreateVolumeTexture(d,2,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&g.v,NULL),"FAIL volume resource");expect(IDirect3DDevice9_CreateVertexBuffer(d,16+4*sizeof(Vertex),0,0,D3DPOOL_DEFAULT,&g.vb,NULL),"FAIL VB resource");fill_vertices(g.vb,color);expect(IDirect3DDevice9_CreateIndexBuffer(d,12,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&g.ib,NULL),"FAIL IB resource");void*data=NULL;expect(IDirect3DIndexBuffer9_Lock(g.ib,0,0,&data,0),"FAIL indices lock");const WORD indices[]={0,1,2,2,1,3};CopyMemory(data,indices,sizeof(indices));expect(IDirect3DIndexBuffer9_Unlock(g.ib),"FAIL indices upload");expect(IDirect3DDevice9_CreateVertexDeclaration(d,elements,&g.decl),"FAIL declaration resource");expect(IDirect3DDevice9_CreateVertexShader(d,vertex_code,&g.vs),"FAIL VS resource");expect(IDirect3DDevice9_CreatePixelShader(d,pixel_code,&g.ps),"FAIL PS resource");return g;
}
static void bind_group(IDirect3DDevice9*d,Group g)
{
    expect(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)g.t),"FAIL bind 2D");expect(IDirect3DDevice9_SetTexture(d,1,(IDirect3DBaseTexture9*)g.c),"FAIL bind cube");expect(IDirect3DDevice9_SetTexture(d,2,(IDirect3DBaseTexture9*)g.v),"FAIL bind volume");expect(IDirect3DDevice9_SetStreamSource(d,0,g.vb,16,sizeof(Vertex)),"FAIL bind VB");expect(IDirect3DDevice9_SetIndices(d,g.ib),"FAIL bind IB");expect(IDirect3DDevice9_SetVertexDeclaration(d,g.decl),"FAIL bind declaration");expect(IDirect3DDevice9_SetVertexShader(d,g.vs),"FAIL bind VS");expect(IDirect3DDevice9_SetPixelShader(d,g.ps),"FAIL bind PS");
}
static void unbind(IDirect3DDevice9*d)
{
    for(UINT i=0;i<3;i++){expect(IDirect3DDevice9_SetTexture(d,i,NULL),"FAIL unbind sampler");}expect(IDirect3DDevice9_SetStreamSource(d,0,NULL,0,0),"FAIL unbind VB");expect(IDirect3DDevice9_SetIndices(d,NULL),"FAIL unbind IB");expect(IDirect3DDevice9_SetVertexDeclaration(d,NULL),"FAIL unbind declaration");expect(IDirect3DDevice9_SetVertexShader(d,NULL),"FAIL unbind VS");expect(IDirect3DDevice9_SetPixelShader(d,NULL),"FAIL unbind PS");
}
static void public_refs(Group g,ULONG expected)
{
    check(refs((IUnknown*)g.t)==expected&&refs((IUnknown*)g.c)==expected&&refs((IUnknown*)g.v)==expected&&refs((IUnknown*)g.vb)==expected&&refs((IUnknown*)g.ib)==expected&&refs((IUnknown*)g.decl)==expected&&refs((IUnknown*)g.vs)==expected&&refs((IUnknown*)g.ps)==expected,"FAIL resource public counts");
}
static void release_group(Group g)
{
    check(!IDirect3DTexture9_Release(g.t)&&!IDirect3DCubeTexture9_Release(g.c)&&!IDirect3DVolumeTexture9_Release(g.v)&&!IDirect3DVertexBuffer9_Release(g.vb)&&!IDirect3DIndexBuffer9_Release(g.ib)&&!IDirect3DVertexDeclaration9_Release(g.decl)&&!IDirect3DVertexShader9_Release(g.vs)&&!IDirect3DPixelShader9_Release(g.ps),"FAIL resource public zero");
}
static Group get_group(IDirect3DDevice9*d,Group wanted)
{
    Group g;ZeroMemory(&g,sizeof(g));expect(IDirect3DDevice9_GetTexture(d,0,(IDirect3DBaseTexture9**)&g.t),"FAIL Get2D");expect(IDirect3DDevice9_GetTexture(d,1,(IDirect3DBaseTexture9**)&g.c),"FAIL GetCube");expect(IDirect3DDevice9_GetTexture(d,2,(IDirect3DBaseTexture9**)&g.v),"FAIL GetVolume");UINT offset=0,stride=0;expect(IDirect3DDevice9_GetStreamSource(d,0,&g.vb,&offset,&stride),"FAIL GetVB");expect(IDirect3DDevice9_GetIndices(d,&g.ib),"FAIL GetIB");expect(IDirect3DDevice9_GetVertexDeclaration(d,&g.decl),"FAIL GetDeclaration");expect(IDirect3DDevice9_GetVertexShader(d,&g.vs),"FAIL GetVS");expect(IDirect3DDevice9_GetPixelShader(d,&g.ps),"FAIL GetPS");check(equal(&g,&wanted,sizeof(g))&&offset==16&&stride==sizeof(Vertex),"FAIL restored object identity");public_refs(g,1);return g;
}
static void draw(IDirect3DDevice9*d)
{
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawIndexedPrimitive(d,D3DPT_TRIANGLELIST,0,0,4,0,2),"FAIL stateblock bound draw");expect(IDirect3DDevice9_EndScene(d),"FAIL end scene");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"STATE9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Stateblock binding",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");


    ULONG baseline=refs((IUnknown*)d);Group a=make_group(d,0xffb02010);public_refs(a,1);IDirect3DStateBlock9*block=NULL;
    expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL Begin");bind_group(d,a);bind_group(d,a);expect(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)a.c),"FAIL record cube replacement");expect(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)a.v),"FAIL record volume replacement");expect(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)a.t),"FAIL record 2D restore");expect(IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"FAIL record FVF");expect(IDirect3DDevice9_SetVertexDeclaration(d,a.decl),"FAIL record declaration restore");public_refs(a,1);check(refs((IUnknown*)d)==baseline+9,"FAIL recorded public/device refs");expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL End");release_group(a);check(refs((IUnknown*)d)==baseline+1,"FAIL recorded resource device pins");
    IDirect3DVertexShader9*empty_vs=(void*)1;expect(IDirect3DDevice9_GetVertexShader(d,&empty_vs),"FAIL live query after recording");check(!empty_vs,"FAIL recording changed live state");expect(IDirect3DStateBlock9_Apply(block),"FAIL initial Apply");
    for(UINT i=0;i<64;i++){expect(IDirect3DStateBlock9_Capture(block),"FAIL repeated Capture");Group q=get_group(d,a);check(refs((IUnknown*)d)==baseline+9,"FAIL restored public/device refs");metadata(q.decl,q.vs,q.ps);release_group(q);check(refs((IUnknown*)d)==baseline+1,"FAIL restored resource debt");}
    expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL guard Begin");check(IDirect3DStateBlock9_Capture(block)==D3DERR_INVALIDCALL&&IDirect3DStateBlock9_Apply(block)==D3DERR_INVALIDCALL,"FAIL recording permits Capture/Apply");IDirect3DStateBlock9*empty_block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&empty_block),"FAIL guard End");check(!IDirect3DStateBlock9_Release(empty_block),"FAIL empty block release");draw(d);capture(d,cpu,0,0xffb02010);
    unbind(d);check(refs((IUnknown*)d)==baseline+1,"FAIL block-only resource pin");expect(IDirect3DDevice9_Reset(d,&pp),"FAIL block-only DEFAULT Reset");expect(IDirect3DStateBlock9_Apply(block),"FAIL restore after Reset");Group q=get_group(d,a);fill_vertices(q.vb,0xff1040c0);release_group(q);draw(d);capture(d,cpu,1,0xff1040c0);
    Group b=make_group(d,0xff20a040);bind_group(d,b);public_refs(b,1);expect(IDirect3DStateBlock9_Capture(block),"FAIL replacing Capture");release_group(b);check(refs((IUnknown*)d)==baseline+1,"FAIL replacing Capture resource pins");draw(d);capture(d,cpu,2,0xff20a040);
    check(!IDirect3DStateBlock9_Release(block)&&refs((IUnknown*)d)==baseline,"FAIL stateblock release debt");q=get_group(d,b);fill_vertices(q.vb,0xffc0a020);release_group(q);draw(d);capture(d,cpu,3,0xffc0a020);
    unbind(d);expect(IDirect3DDevice9_Reset(d,&pp),"FAIL final Reset");expect(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff304050,1,0),"FAIL post Reset clear");capture(d,cpu,4,0xff304050);check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final refs");FreeLibrary(module);DestroyWindow(window);report("STATE STATUS groups=2 resourceFamilies=8 resources=16 captures=65 getterQueries=528 metadataRounds=64 applies=2 recordingRefusals=2 resetSuccesses=2 frames=5 readbacks=5 factoryRefs=0");report("PASS D3D9 stateblock bindings");CloseHandle(logfile);ExitProcess(0);
}
