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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"ROOT FRAME step=%u color=%08lX readback=1",step,color);report(line);
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

static IUnknown*anchor(Group g,UINT kind)
{
    switch(kind){case 0:return (IUnknown*)g.t;case 1:return (IUnknown*)g.c;case 2:return (IUnknown*)g.v;case 3:return (IUnknown*)g.vb;case 4:return (IUnknown*)g.ib;case 5:return (IUnknown*)g.decl;case 6:return (IUnknown*)g.vs;default:return (IUnknown*)g.ps;}
}
static void get_owner(Group g,UINT kind,IDirect3DDevice9**out)
{
    HRESULT hr;switch(kind){case 0:hr=IDirect3DTexture9_GetDevice(g.t,out);break;case 1:hr=IDirect3DCubeTexture9_GetDevice(g.c,out);break;case 2:hr=IDirect3DVolumeTexture9_GetDevice(g.v,out);break;case 3:hr=IDirect3DVertexBuffer9_GetDevice(g.vb,out);break;case 4:hr=IDirect3DIndexBuffer9_GetDevice(g.ib,out);break;case 5:hr=IDirect3DVertexDeclaration9_GetDevice(g.decl,out);break;case 6:hr=IDirect3DVertexShader9_GetDevice(g.vs,out);break;default:hr=IDirect3DPixelShader9_GetDevice(g.ps,out);break;}expect(hr,"FAIL child GetDevice recovery");
}
static void release_group_except(Group g,UINT except)
{
    for(UINT i=0;i<8;i++){if(i!=except)check(!IUnknown_Release(anchor(g,i)),"FAIL resource public zero");}
}
static void snapshot(IDirect3DDevice9*d,Group wanted,UINT anchor_kind)
{
    Group g;ZeroMemory(&g,sizeof(g));expect(IDirect3DDevice9_GetTexture(d,0,(IDirect3DBaseTexture9**)&g.t),"FAIL retained 2D");expect(IDirect3DDevice9_GetTexture(d,1,(IDirect3DBaseTexture9**)&g.c),"FAIL retained cube");expect(IDirect3DDevice9_GetTexture(d,2,(IDirect3DBaseTexture9**)&g.v),"FAIL retained volume");UINT offset=0,stride=0;expect(IDirect3DDevice9_GetStreamSource(d,0,&g.vb,&offset,&stride),"FAIL retained VB");expect(IDirect3DDevice9_GetIndices(d,&g.ib),"FAIL retained IB");expect(IDirect3DDevice9_GetVertexDeclaration(d,&g.decl),"FAIL retained declaration");expect(IDirect3DDevice9_GetVertexShader(d,&g.vs),"FAIL retained VS");expect(IDirect3DDevice9_GetPixelShader(d,&g.ps),"FAIL retained PS");check(equal(&g,&wanted,sizeof(g))&&offset==16&&stride==sizeof(Vertex),"FAIL child-root state identity");metadata(g.decl,g.vs,g.ps);
    for(UINT i=0;i<8;i++){check(refs(anchor(g,i))==(i==anchor_kind?2u:1u),"FAIL retained public refs");check(IUnknown_Release(anchor(g,i))==(i==anchor_kind?1u:0u),"FAIL getter release debt");}
    DWORD value=0;expect(IDirect3DDevice9_GetRenderState(d,D3DRS_CULLMODE,&value),"FAIL retained cull query");check(value==D3DCULL_NONE,"FAIL retained cull value");float constants[4];static const float wanted_constants[4]={.25f,.5f,.75f,1};expect(IDirect3DDevice9_GetVertexShaderConstantF(d,3,constants,1),"FAIL retained constant query");check(equal(constants,wanted_constants,sizeof(constants)),"FAIL retained shader constants");
}
static void prepare(IDirect3DDevice9*d)
{
    static const float constants[4]={.25f,.5f,.75f,1};expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");expect(IDirect3DDevice9_SetVertexShaderConstantF(d,3,constants,1),"FAIL custom constants");
}
static void draw_capture(IDirect3DDevice9*d,UINT step,DWORD color)
{
    IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU surface");expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawIndexedPrimitive(d,D3DPT_TRIANGLELIST,0,0,4,0,2),"FAIL preserved root draw");expect(IDirect3DDevice9_EndScene(d),"FAIL scene end");capture(d,cpu,step,color);check(!IDirect3DSurface9_Release(cpu),"FAIL CPU surface refs");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"ROOT9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Device root state",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    static const DWORD colors[]={0xffb02010,0xff1040c0,0xff20a040,0xffc0a020,0xff8020c0,0xff20b0c0,0xffe06020,0xff304050,0xff70b030};
    for(UINT kind=0;kind<8;kind++){
        IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");Group g=make_group(d,colors[kind]);bind_group(d,g);prepare(d);release_group_except(g,kind);check(refs((IUnknown*)d)==2&&refs(anchor(g,kind))==1,"FAIL anchor refs");IDirect3DDevice9*original=d;check(IDirect3DDevice9_Release(d)==1,"FAIL child does not retain device");d=NULL;get_owner(g,kind,&d);check(d==original&&refs((IUnknown*)d)==2,"FAIL child device identity");snapshot(d,g,kind);check(refs((IUnknown*)d)==2,"FAIL snapshot device debt");draw_capture(d,kind,colors[kind]);check(IDirect3DDevice9_Release(d)==1,"FAIL recovered device release");check(!IUnknown_Release(anchor(g,kind)),"FAIL last anchor release");
    }
    /* An unfinished recording is an internal object and owns no parent pin. */
    IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL pending device");Group pending=make_group(d,0xff8020c0);bind_group(d,pending);release_group_except(pending,8);check(refs((IUnknown*)d)==1,"FAIL pending baseline");expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL pending Begin");bind_group(d,pending);check(refs((IUnknown*)d)==1,"FAIL Begin adds public device pin");check(!IDirect3DDevice9_Release(d),"FAIL unfinished recording cycle");
    /* A completed public stateblock independently keeps full device state. */
    d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL block device");Group g=make_group(d,colors[8]);bind_group(d,g);prepare(d);release_group_except(g,8);expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL block Begin");bind_group(d,g);check(refs((IUnknown*)d)==1,"FAIL block Begin pin");IDirect3DStateBlock9*block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL block End");check(refs((IUnknown*)d)==2,"FAIL End lacks public device pin");IDirect3DDevice9*original=d;check(IDirect3DDevice9_Release(d)==1,"FAIL completed block device pin");d=NULL;expect(IDirect3DStateBlock9_GetDevice(block,&d),"FAIL block GetDevice recovery");check(d==original&&refs((IUnknown*)d)==2,"FAIL block recovered device identity");snapshot(d,g,8);expect(IDirect3DStateBlock9_Apply(block),"FAIL block Apply");draw_capture(d,8,colors[8]);check(IDirect3DDevice9_Release(d)==1&&!IDirect3DStateBlock9_Release(block),"FAIL completed block final release");
    check(!IDirect3D9_Release(factory),"FAIL factory refs");FreeLibrary(module);DestroyWindow(window);report("ROOT STATUS devices=10 resources=80 anchorFamilies=8 childRecoveries=8 stateblockRecoveries=1 getterQueries=72 metadataRounds=9 pendingCancelled=1 beginDevicePins=0 endDevicePins=1 frames=9 readbacks=9 factoryRefs=0");report("PASS D3D9 external-child root state");CloseHandle(logfile);ExitProcess(0);
}
