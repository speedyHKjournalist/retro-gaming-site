/* D3D9 public-zero VB/IB draw lifetime, UP unbind and Reset boundaries. */
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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"STRM FRAME step=%u color=%08lX readback=1",step,color);report(line);
}

typedef struct Vertex{float x,y,z,w;DWORD color;}Vertex;
static UINT stream_queries;
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
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"STRM9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Stream metadata",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");

    ULONG baseline=refs((IUnknown*)d);for(UINT i=0;i<16;i++){query(d,i,NULL,0,0);}IDirect3DVertexBuffer9*a=make_vertex(d,16,0xffb02010);for(UINT i=0;i<16;i++){expect(IDirect3DDevice9_SetStreamSource(d,i,a,16,sizeof(Vertex)),"FAIL initial streams");}check(!IDirect3DVertexBuffer9_Release(a),"FAIL initial public zero");for(UINT i=0;i<16;i++){query(d,i,a,16,sizeof(Vertex));}for(UINT i=1;i<16;i++){expect(IDirect3DDevice9_SetStreamSource(d,i,NULL,0xfffffffc,0xffffffff),"FAIL NULL huge metadata");query(d,i,NULL,16,sizeof(Vertex));expect(IDirect3DDevice9_SetStreamSource(d,i,NULL,0,0),"FAIL duplicate NULL");}
    state(d);draw(d);capture(d,cpu,0,0xffb02010);expect(IDirect3DDevice9_SetStreamSource(d,0,NULL,0xfffffffc,0xffffffff),"FAIL last NULL");query(d,0,NULL,16,sizeof(Vertex));check(refs((IUnknown*)d)==baseline,"FAIL NULL release device debt");
    IDirect3DVertexBuffer9*live_buffer=make_vertex(d,32,0xff20a040);expect(IDirect3DDevice9_SetStreamSource(d,0,live_buffer,32,sizeof(Vertex)),"FAIL live stream");check(!IDirect3DVertexBuffer9_Release(live_buffer),"FAIL live public zero");query(d,0,live_buffer,32,sizeof(Vertex));expect(IDirect3DDevice9_BeginStateBlock(d),"FAIL Begin");IDirect3DVertexBuffer9*recorded=make_vertex(d,64,0xff1040c0);expect(IDirect3DDevice9_SetStreamSource(d,0,recorded,64,28),"FAIL recorded different metadata");expect(IDirect3DDevice9_SetStreamSource(d,0,NULL,0xfffffffc,0xffffffff),"FAIL recorded NULL");check(!IDirect3DVertexBuffer9_Release(recorded),"FAIL recorded replaced storage");IDirect3DStateBlock9*block=NULL;expect(IDirect3DDevice9_EndStateBlock(d,&block),"FAIL End");query(d,0,live_buffer,32,sizeof(Vertex));draw(d);capture(d,cpu,1,0xff20a040);
    expect(IDirect3DDevice9_SetStreamSource(d,0,live_buffer,0,4),"FAIL live metadata change");expect(IDirect3DStateBlock9_Apply(block),"FAIL NULL snapshot Apply");query(d,0,NULL,32,sizeof(Vertex));expect(IDirect3DStateBlock9_Capture(block),"FAIL NULL snapshot Capture");IDirect3DVertexBuffer9*c=make_vertex(d,16,0xff8020c0);expect(IDirect3DDevice9_SetStreamSource(d,0,c,16,sizeof(Vertex)),"FAIL replacement stream");check(!IDirect3DVertexBuffer9_Release(c),"FAIL replacement public zero");expect(IDirect3DStateBlock9_Apply(block),"FAIL NULL capture Apply");query(d,0,NULL,32,sizeof(Vertex));check(!IDirect3DStateBlock9_Release(block)&&refs((IUnknown*)d)==baseline,"FAIL block refs");
    Vertex quad[4],triangles[6];vertices(quad,0xff1040c0);for(UINT i=0;i<6;i++)triangles[i]=quad[indices[i]];expect(IDirect3DDevice9_BeginScene(d),"FAIL UP scene");expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,triangles,sizeof(Vertex)),"FAIL UP draw");expect(IDirect3DDevice9_EndScene(d),"FAIL UP end");query(d,0,NULL,0,0);capture(d,cpu,2,0xff1040c0);
    IDirect3DVertexBuffer9*e=make_vertex(d,16,0xff8020c0);expect(IDirect3DDevice9_SetStreamSource(d,0,e,16,sizeof(Vertex)),"FAIL indexed UP stream");check(!IDirect3DVertexBuffer9_Release(e),"FAIL indexed UP public zero");IDirect3DIndexBuffer9*ib=NULL;expect(IDirect3DDevice9_CreateIndexBuffer(d,12,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,NULL),"FAIL index buffer");void*data=NULL;expect(IDirect3DIndexBuffer9_Lock(ib,0,0,&data,0),"FAIL index lock");CopyMemory(data,indices,sizeof(indices));expect(IDirect3DIndexBuffer9_Unlock(ib),"FAIL index upload");expect(IDirect3DDevice9_SetIndices(d,ib),"FAIL indices bind");check(!IDirect3DIndexBuffer9_Release(ib),"FAIL index public zero");vertices(quad,0xffc0a020);expect(IDirect3DDevice9_BeginScene(d),"FAIL indexed UP scene");expect(IDirect3DDevice9_DrawIndexedPrimitiveUP(d,D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,quad,sizeof(Vertex)),"FAIL indexed UP draw");expect(IDirect3DDevice9_EndScene(d),"FAIL indexed UP end");query(d,0,NULL,0,0);ib=(void*)1;expect(IDirect3DDevice9_GetIndices(d,&ib),"FAIL indexed UP empty query");check(!ib,"FAIL indexed UP retained indices");capture(d,cpu,3,0xffc0a020);
    expect(IDirect3DDevice9_Reset(d,&pp),"FAIL Reset");for(UINT i=0;i<16;i++){query(d,i,NULL,0,0);}expect(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff304050,1,0),"FAIL post Reset clear");capture(d,cpu,4,0xff304050);check(stream_queries==70,"FAIL query count");check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final refs");FreeLibrary(module);DestroyWindow(window);char line[220];wsprintfA(line,"STRM STATUS vertexBuffers=5 indexBuffers=1 slots=16 streamQueries=%u recordedNulls=1 applies=2 captures=1 upDraws=1 indexedUpDraws=1 resetSuccesses=1 frames=5 readbacks=5 factoryRefs=0",stream_queries);report(line);report("PASS D3D9 stream metadata");CloseHandle(logfile);ExitProcess(0);
}
