/* Missing-declaration UP storage retirement and preserved failure metadata. */
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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"UPF FRAME step=%u color=%08lX readback=1",step,color);report(line);
}

typedef struct Vertex{float x,y,z,w;DWORD color;}Vertex;
static UINT stream_queries,index_queries,refusals;
static const WORD indices[6]={0,1,2,2,1,3};
static void vertices(Vertex*v,DWORD color){v[0]=(Vertex){-.5f,-.5f,.5f,1,color};v[1]=(Vertex){7.5f,-.5f,.5f,1,color};v[2]=(Vertex){-.5f,7.5f,.5f,1,color};v[3]=(Vertex){7.5f,7.5f,.5f,1,color};}
static IDirect3DVertexBuffer9*make_vertex(IDirect3DDevice9*d,UINT offset,DWORD color)
{
    IDirect3DVertexBuffer9*b=NULL;expect(IDirect3DDevice9_CreateVertexBuffer(d,offset+4*sizeof(Vertex),0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,D3DPOOL_DEFAULT,&b,NULL),"FAIL vertex buffer");void*data=NULL;expect(IDirect3DVertexBuffer9_Lock(b,0,0,&data,0),"FAIL vertex fill");for(UINT i=0;i<offset;i++)((BYTE*)data)[i]=0xa5;vertices((Vertex*)((BYTE*)data+offset),color);expect(IDirect3DVertexBuffer9_Unlock(b),"FAIL vertex upload");return b;
}
static void query(IDirect3DDevice9*d,UINT slot,IDirect3DVertexBuffer9*wanted,UINT wanted_offset,UINT wanted_stride)
{
    IDirect3DVertexBuffer9*b=(void*)1;UINT offset=0xffffffff,stride=0xffffffff;expect(IDirect3DDevice9_GetStreamSource(d,slot,&b,&offset,&stride),"FAIL stream query");stream_queries++;check(b==wanted&&offset==wanted_offset&&stride==wanted_stride,"FAIL stream identity/metadata");if(b)check(!IDirect3DVertexBuffer9_Release(b),"FAIL stream query public debt");
}
static IDirect3DIndexBuffer9*make_index(IDirect3DDevice9*d)
{
    IDirect3DIndexBuffer9*b=NULL;expect(IDirect3DDevice9_CreateIndexBuffer(d,12,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&b,NULL),"FAIL index buffer");void*data=NULL;expect(IDirect3DIndexBuffer9_Lock(b,0,0,&data,0),"FAIL index lock");CopyMemory(data,indices,sizeof(indices));expect(IDirect3DIndexBuffer9_Unlock(b),"FAIL index upload");expect(IDirect3DDevice9_SetIndices(d,b),"FAIL bind index");check(!IDirect3DIndexBuffer9_Release(b),"FAIL index public zero");return b;
}
static void query_index(IDirect3DDevice9*d,IDirect3DIndexBuffer9*wanted){IDirect3DIndexBuffer9*b=(void*)1;expect(IDirect3DDevice9_GetIndices(d,&b),"FAIL index query");check(b==wanted,"FAIL index identity");if(b)check(!IDirect3DIndexBuffer9_Release(b),"FAIL index query debt");index_queries++;}
static void refused(HRESULT hr){check(hr==D3DERR_INVALIDCALL,"FAIL expected UP refusal");refusals++;}
static void up_draw(IDirect3DDevice9*d,DWORD color,BOOL indexed)
{
    Vertex quad[4],triangles[6];vertices(quad,color);for(UINT i=0;i<6;i++)triangles[i]=quad[indices[i]];
    expect(IDirect3DDevice9_BeginScene(d),"FAIL UP scene");if(indexed)expect(IDirect3DDevice9_DrawIndexedPrimitiveUP(d,D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,quad,sizeof(Vertex)),"FAIL successful indexed UP");else expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,triangles,sizeof(Vertex)),"FAIL successful UP");expect(IDirect3DDevice9_EndScene(d),"FAIL UP end");
}
static void draw(IDirect3DDevice9*d)
{
    expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawPrimitive(d,D3DPT_TRIANGLESTRIP,0,2),"FAIL bound stream draw");expect(IDirect3DDevice9_EndScene(d),"FAIL scene end");
}
static void state(IDirect3DDevice9*d)
{
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_LIGHTING,FALSE),"FAIL lighting");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");expect(IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"FAIL FVF");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL color op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL color arg");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL alpha op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL alpha arg");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"UPF9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Missing-format UP",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");

    ULONG baseline=refs((IUnknown*)d);Vertex quad[4],triangles[6];vertices(quad,0xffb02010);for(UINT i=0;i<6;i++)triangles[i]=quad[indices[i]];
    IDirect3DVertexBuffer9*a=make_vertex(d,16,0xffb02010),*side=make_vertex(d,32,0xff8020c0);expect(IDirect3DDevice9_SetStreamSource(d,0,a,16,sizeof(Vertex)),"FAIL initial stream");expect(IDirect3DDevice9_SetStreamSource(d,1,side,32,sizeof(Vertex)),"FAIL side stream");check(!IDirect3DVertexBuffer9_Release(a)&&!IDirect3DVertexBuffer9_Release(side),"FAIL VB public zero");IDirect3DIndexBuffer9*ib=make_index(d);query(d,0,a,16,20);
    expect(IDirect3DDevice9_BeginScene(d),"FAIL refusal scene");refused(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,triangles,20));expect(IDirect3DDevice9_EndScene(d),"FAIL refusal end");query(d,0,NULL,16,20);query(d,1,side,32,20);query_index(d,ib);check(refs((IUnknown*)d)==baseline,"FAIL failure parent debt");
    IDirect3DVertexBuffer9*c=make_vertex(d,32,0xff1040c0);expect(IDirect3DDevice9_SetStreamSource(d,0,c,32,20),"FAIL failed indexed stream");check(!IDirect3DVertexBuffer9_Release(c),"FAIL C public zero");expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL Begin record");expect(IDirect3DDevice9_BeginScene(d),"FAIL recorded refusal scene");refused(IDirect3DDevice9_DrawIndexedPrimitiveUP(d,D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,quad,20));expect(IDirect3DDevice9_EndScene(d),"FAIL recorded refusal end");IDirect3DStateBlock9*block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL End record");query(d,0,NULL,32,20);query(d,1,side,32,20);query_index(d,NULL);
    expect(IDirect3DDevice9_SetStreamSource(d,0,side,32,20),"FAIL restore side as stream0");ib=make_index(d);expect(IDirect3DStateBlock9_Capture(block),"FAIL empty Capture");expect(IDirect3DStateBlock9_Apply(block),"FAIL empty Apply");query(d,0,side,32,20);query(d,1,side,32,20);query_index(d,ib);check(!IDirect3DStateBlock9_Release(block)&&refs((IUnknown*)d)==baseline,"FAIL recording changed parent ownership");
    state(d);draw(d);capture(d,cpu,0,0xff8020c0);
    up_draw(d,0xff1040c0,FALSE);query(d,0,NULL,0,0);query_index(d,ib);capture(d,cpu,1,0xff1040c0);
    expect(IDirect3DDevice9_SetStreamSource(d,0,side,32,20),"FAIL successful indexed stream");up_draw(d,0xffc0a020,TRUE);query(d,0,NULL,0,0);query_index(d,NULL);capture(d,cpu,2,0xffc0a020);
    expect(IDirect3DDevice9_SetStreamSource(d,0,side,32,20),"FAIL second missing stream");expect(IDirect3DDevice9_SetVertexDeclaration(d,NULL),"FAIL missing declaration");expect(IDirect3DDevice9_BeginScene(d),"FAIL second refusal scene");refused(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,NULL,20));query(d,0,side,32,20);refused(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,triangles,20));expect(IDirect3DDevice9_EndScene(d),"FAIL second refusal end");query(d,0,NULL,32,20);query(d,1,side,32,20);query_index(d,NULL);
    expect(IDirect3DDevice9_SetFVF(d,68),"FAIL restore FVF");up_draw(d,0xff20a040,FALSE);query(d,0,NULL,0,0);capture(d,cpu,3,0xff20a040);
    expect(IDirect3DDevice9_SetStreamSource(d,1,NULL,0,0),"FAIL side unbind");query(d,1,NULL,32,20);expect(IDirect3DDevice9_Reset(d,&pp),"FAIL Reset");query(d,0,NULL,0,0);query(d,1,NULL,0,0);expect(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff304050,1,0),"FAIL post Reset clear");capture(d,cpu,4,0xff304050);
    check(stream_queries==16&&index_queries==6&&refusals==4,"FAIL counters");check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final refs");FreeLibrary(module);DestroyWindow(window);char line[240];wsprintfA(line,"UPF STATUS vertexBuffers=3 indexBuffers=2 streamQueries=%u indexQueries=%u refusals=%u missingDeclaration=3 basicArgument=1 applies=1 captures=1 upDraws=2 indexedUpDraws=1 resetSuccesses=1 frames=5 readbacks=5 factoryRefs=0",stream_queries,index_queries,refusals);report(line);report("PASS D3D9 missing-format UP");CloseHandle(logfile);ExitProcess(0);
}
