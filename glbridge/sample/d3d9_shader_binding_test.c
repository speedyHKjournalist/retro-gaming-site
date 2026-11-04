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
    IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL capture target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL actual readback request");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL CPU readback access");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL CPU unlock");char line[180];wsprintfA(line,"SBIND FRAME step=%u color=%08lX readback=1",step,color);report(line);
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
static void bind(IDirect3DDevice9*d,IDirect3DVertexDeclaration9*decl,IDirect3DVertexShader9*vs,IDirect3DPixelShader9*ps)
{
    expect(IDirect3DDevice9_SetVertexDeclaration(d,decl),"FAIL bind declaration");expect(IDirect3DDevice9_SetVertexShader(d,vs),"FAIL bind VS");expect(IDirect3DDevice9_SetPixelShader(d,ps),"FAIL bind PS");
}
static void empty(IDirect3DDevice9*d)
{
    IDirect3DVertexDeclaration9*decl=(void*)1;IDirect3DVertexShader9*vs=(void*)1;IDirect3DPixelShader9*ps=(void*)1;expect(IDirect3DDevice9_GetVertexDeclaration(d,&decl),"FAIL empty declaration");expect(IDirect3DDevice9_GetVertexShader(d,&vs),"FAIL empty VS");expect(IDirect3DDevice9_GetPixelShader(d,&ps),"FAIL empty PS");check(!decl&&!vs&&!ps,"FAIL Reset retains bindings");
}
static void draw(IDirect3DDevice9*d,DWORD color)
{
    Vertex quad[4]={{-1.25f,1.25f,.5f,1,color},{1.25f,1.25f,.5f,1,color},{-1.25f,-1.25f,.5f,1,color},{1.25f,-1.25f,.5f,1,color}},triangles[6];const UINT indices[]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)triangles[i]=quad[indices[i]];
    expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");expect(IDirect3DDevice9_BeginScene(d),"FAIL scene");expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,triangles,sizeof(Vertex)),"FAIL public-zero shader draw");expect(IDirect3DDevice9_EndScene(d),"FAIL end scene");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"SBIND9.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Shader binding",WS_POPUP,30,40,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");check(make!=NULL,"FAIL export");IDirect3D9*factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*d=NULL;expect(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL device");IDirect3DSurface9*cpu=NULL;expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL CPU readback surface");

    ULONG baseline=refs((IUnknown*)d);IDirect3DVertexDeclaration9*decl=NULL;IDirect3DVertexShader9*vs=NULL;IDirect3DPixelShader9*ps=NULL;
    expect(IDirect3DDevice9_CreateVertexDeclaration(d,elements,&decl),"FAIL declaration A");expect(IDirect3DDevice9_CreateVertexShader(d,vertex_code,&vs),"FAIL VS A");expect(IDirect3DDevice9_CreatePixelShader(d,pixel_code,&ps),"FAIL PS A");bind(d,decl,vs,ps);
    check(refs((IUnknown*)decl)==1&&refs((IUnknown*)vs)==1&&refs((IUnknown*)ps)==1&&refs((IUnknown*)d)==baseline+3,"FAIL bindings expose public refs");check(!IDirect3DVertexDeclaration9_Release(decl)&&!IDirect3DVertexShader9_Release(vs)&&!IDirect3DPixelShader9_Release(ps)&&refs((IUnknown*)d)==baseline,"FAIL public-zero shader refs");
    for(UINT i=0;i<128;i++){
        IUnknown*dq=NULL,*vq=NULL,*pq=NULL;expect(IDirect3DVertexDeclaration9_QueryInterface(decl,&IID_IUnknown,(void**)&dq),"FAIL QI declaration revival");expect(IDirect3DVertexShader9_QueryInterface(vs,&IID_IUnknown,(void**)&vq),"FAIL QI VS revival");expect(IDirect3DPixelShader9_QueryInterface(ps,&IID_IUnknown,(void**)&pq),"FAIL QI PS revival");check((void*)dq==decl&&(void*)vq==vs&&(void*)pq==ps&&refs((IUnknown*)d)==baseline+3,"FAIL QI identity/device pins");
        IDirect3DVertexDeclaration9*dg=NULL;IDirect3DVertexShader9*vg=NULL;IDirect3DPixelShader9*pg=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&dg),"FAIL getter declaration");expect(IDirect3DDevice9_GetVertexShader(d,&vg),"FAIL getter VS");expect(IDirect3DDevice9_GetPixelShader(d,&pg),"FAIL getter PS");check(dg==decl&&vg==vs&&pg==ps&&refs((IUnknown*)dg)==2&&refs((IUnknown*)vg)==2&&refs((IUnknown*)pg)==2&&refs((IUnknown*)d)==baseline+3,"FAIL getter identity/device pins");metadata(dg,vg,pg);
        check(IDirect3DVertexDeclaration9_Release(dg)==1&&IDirect3DVertexShader9_Release(vg)==1&&IDirect3DPixelShader9_Release(pg)==1,"FAIL getter refs");check(!IUnknown_Release(dq)&&!IUnknown_Release(vq)&&!IUnknown_Release(pq)&&refs((IUnknown*)d)==baseline,"FAIL QI revival debt");
    }
    bind(d,decl,vs,ps);draw(d,0xffb02010);capture(d,cpu,0,0xffb02010);
    expect(IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"FAIL FVF declaration retire");IDirect3DVertexDeclaration9*dg=(void*)1;expect(IDirect3DDevice9_GetVertexDeclaration(d,&dg),"FAIL FVF empty declaration");check(!dg,"FAIL FVF retained declaration");expect(IDirect3DDevice9_CreateVertexDeclaration(d,elements,&decl),"FAIL declaration B");expect(IDirect3DDevice9_SetVertexDeclaration(d,decl),"FAIL bind declaration B");check(!IDirect3DVertexDeclaration9_Release(decl),"FAIL declaration B public refs");draw(d,0xff1040c0);capture(d,cpu,1,0xff1040c0);
    expect(IDirect3DDevice9_SetVertexShader(d,NULL),"FAIL retire VS A");expect(IDirect3DDevice9_SetPixelShader(d,NULL),"FAIL retire PS A");expect(IDirect3DDevice9_CreateVertexShader(d,vertex_code,&vs),"FAIL VS B");expect(IDirect3DDevice9_CreatePixelShader(d,pixel_code,&ps),"FAIL PS B");expect(IDirect3DDevice9_SetVertexShader(d,vs),"FAIL bind VS B");expect(IDirect3DDevice9_SetPixelShader(d,ps),"FAIL bind PS B");check(!IDirect3DVertexShader9_Release(vs)&&!IDirect3DPixelShader9_Release(ps),"FAIL shader B public refs");draw(d,0xff20a040);capture(d,cpu,2,0xff20a040);
    IDirect3DVertexShader9*vg=NULL;IDirect3DPixelShader9*pg=NULL;dg=NULL;expect(IDirect3DDevice9_GetVertexDeclaration(d,&dg),"FAIL retained Reset declaration");expect(IDirect3DDevice9_GetVertexShader(d,&vg),"FAIL retained Reset VS");expect(IDirect3DDevice9_GetPixelShader(d,&pg),"FAIL retained Reset PS");expect(IDirect3DDevice9_Reset(d,&pp),"FAIL public shaders block Reset");empty(d);metadata(dg,vg,pg);bind(d,dg,vg,pg);check(!IDirect3DVertexDeclaration9_Release(dg)&&!IDirect3DVertexShader9_Release(vg)&&!IDirect3DPixelShader9_Release(pg),"FAIL retained Reset public refs");draw(d,0xffc0a020);capture(d,cpu,3,0xffc0a020);
    expect(IDirect3DDevice9_Reset(d,&pp),"FAIL private-only shader Reset");empty(d);check(refs((IUnknown*)d)==baseline,"FAIL final shader device pins");expect(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff304050,1,0),"FAIL post Reset clear");capture(d,cpu,4,0xff304050);
    check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(factory),"FAIL final references");FreeLibrary(module);DestroyWindow(window);report("SBIND STATUS declarations=2 vertexShaders=2 pixelShaders=2 qiRevivals=384 getterQueries=384 metadataRounds=129 draws=4 resetRefusals=0 resetSuccesses=2 frames=5 readbacks=5 factoryRefs=0");report("PASS D3D9 shader bindings");CloseHandle(logfile);ExitProcess(0);
}
