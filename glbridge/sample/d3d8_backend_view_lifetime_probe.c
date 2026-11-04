#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
void sc_check(BOOL,const char*);void sc_report(const char*);
typedef void(WINAPI*VCRetire)(void*);
static HRESULT(WINAPI*vc_get)(IUnknown*,void**);
static HRESULT(WINAPI*vc_attach)(IUnknown*,void*,VCRetire);
typedef struct VCView {IUnknown*object;UINT calls,sequence;}VCView;
static VCView vc_views[14];static UINT vc_retirements;
static void vc_hr(HRESULT hr,const char*message){sc_check(hr==D3D_OK,message);}
static void WINAPI vc_retired(void*context){VCView*v=context;v->calls++;v->sequence=++vc_retirements;sc_check(v->calls==1,"FAIL view repeated callback");void*out=(void*)99;sc_check(vc_get(v->object,&out)==D3DERR_INVALIDCALL&&!out,"FAIL retiring view lookup");sc_check(vc_attach(v->object,v,vc_retired)==D3DERR_INVALIDCALL,"FAIL retiring view reattach");}
static void vc_install(UINT index,IUnknown*object){VCView*v=&vc_views[index];v->object=object;void*out=(void*)99;vc_hr(vc_get(object,&out),"FAIL missing view query");sc_check(!out,"FAIL unexpected view");vc_hr(vc_attach(object,v,vc_retired),"FAIL attach view");vc_hr(vc_get(object,&out),"FAIL attached view query");sc_check(out==v,"FAIL view identity");sc_check(vc_attach(object,(void*)99,vc_retired)==D3DERR_INVALIDCALL,"FAIL duplicate view accepted");}
static void vc_same(UINT index,IUnknown*object){void*out=NULL;sc_check(object==vc_views[index].object,"FAIL changed resource identity");vc_hr(vc_get(object,&out),"FAIL cached view lookup");sc_check(out==&vc_views[index]&&!vc_views[index].calls,"FAIL cached view changed");}
static void vc_bound(IDirect3DDevice9*d){IDirect3DVertexBuffer9*vb=NULL;IDirect3DIndexBuffer9*ib=NULL;UINT offset=99,stride=99;vc_hr(IDirect3DDevice9_GetStreamSource(d,0,&vb,&offset,&stride),"FAIL get stream view");vc_same(3,(IUnknown*)vb);sc_check(!offset&&stride==12,"FAIL stream metadata");sc_check(!IDirect3DVertexBuffer9_Release(vb),"FAIL revived VB count");vc_hr(IDirect3DDevice9_GetIndices(d,&ib),"FAIL get index view");vc_same(4,(IUnknown*)ib);sc_check(!IDirect3DIndexBuffer9_Release(ib),"FAIL revived IB count");for(UINT i=0;i<3;i++){IDirect3DBaseTexture9*t=NULL;vc_hr(IDirect3DDevice9_GetTexture(d,i,&t),"FAIL get texture view");vc_same(i,(IUnknown*)t);sc_check(!IDirect3DBaseTexture9_Release(t),"FAIL revived texture count");}}
void vc_probe(HMODULE module,HWND window,UINT sdk){
    ZeroMemory(vc_views,sizeof(vc_views));vc_retirements=0;
    vc_get=(void*)(uintptr_t)GetProcAddress(module,"Core8ViewGet");vc_attach=(void*)(uintptr_t)GetProcAddress(module,"Core8ViewAttach");sc_check(vc_get&&vc_attach,"FAIL view exports");
    IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
    HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
    HRESULT(WINAPI*reset)(IDirect3DDevice9*,const void*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
    HRESULT(WINAPI*begin)(IDirect3DDevice9*)=(void*)(uintptr_t)GetProcAddress(module,"Core8TokenBegin");
    HRESULT(WINAPI*end)(IDirect3DDevice9*,DWORD*)=(void*)(uintptr_t)GetProcAddress(module,"Core8TokenEnd");
    HRESULT(WINAPI*apply)(IDirect3DDevice9*,DWORD)=(void*)(uintptr_t)GetProcAddress(module,"Core8TokenApply");
    HRESULT(WINAPI*drop)(IDirect3DDevice9*,DWORD)=(void*)(uintptr_t)GetProcAddress(module,"Core8TokenDelete");
    sc_check(factory&&create&&reset&&begin&&end&&apply&&drop,"FAIL view lifecycle exports");
    IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL view root");IDirect3DDevice9*d=NULL;DWORD pp[13]={8,8,21,1,0,1,0,1,1,80,0,0,0};pp[6]=(DWORD)(uintptr_t)window;
    vc_hr(create(root,0,1,window,32,pp,&d),"FAIL view device");sc_check(IDirect3D9_Release(root)==1,"FAIL view root count");
    IDirect3DVertexBuffer9*vb=NULL;IDirect3DIndexBuffer9*ib=NULL;IDirect3DTexture9*t=NULL;IDirect3DCubeTexture9*c=NULL;IDirect3DVolumeTexture9*v=NULL;
    vc_hr(IDirect3DDevice9_CreateVertexBuffer(d,48,0,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb,NULL),"FAIL view VB");
    vc_hr(IDirect3DDevice9_CreateIndexBuffer(d,12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,NULL),"FAIL view IB");
    vc_hr(IDirect3DDevice9_CreateTexture(d,8,8,1,0,21,D3DPOOL_MANAGED,&t,NULL),"FAIL view texture");
    vc_hr(IDirect3DDevice9_CreateCubeTexture(d,4,1,0,21,D3DPOOL_MANAGED,&c,NULL),"FAIL view cube");
    vc_hr(IDirect3DDevice9_CreateVolumeTexture(d,4,4,2,1,0,21,D3DPOOL_MANAGED,&v,NULL),"FAIL view volume");
    vc_install(0,(IUnknown*)t);vc_install(1,(IUnknown*)c);vc_install(2,(IUnknown*)v);vc_install(3,(IUnknown*)vb);vc_install(4,(IUnknown*)ib);
    IDirect3DSurface9*ts=NULL,*cs=NULL,*bb=NULL,*depth=NULL,*cpu=NULL,*rt=NULL,*chainbb=NULL;IDirect3DVolume9*level=NULL;IDirect3DSwapChain9*chain=NULL;
    vc_hr(IDirect3DTexture9_GetSurfaceLevel(t,0,&ts),"FAIL view level surface");vc_hr(IDirect3DCubeTexture9_GetCubeMapSurface(c,0,0,&cs),"FAIL view cube surface");vc_hr(IDirect3DVolumeTexture9_GetVolumeLevel(v,0,&level),"FAIL view volume level");
    vc_install(5,(IUnknown*)ts);vc_install(6,(IUnknown*)cs);vc_install(7,(IUnknown*)level);
    sc_check(IDirect3DSurface9_Release(ts)==1&&IDirect3DSurface9_Release(cs)==1&&IDirect3DVolume9_Release(level)==1,"FAIL view level counts");
    vc_hr(IDirect3DDevice9_GetBackBuffer(d,0,0,0,&bb),"FAIL view implicit back");vc_hr(IDirect3DDevice9_GetDepthStencilSurface(d,&depth),"FAIL view implicit depth");
    vc_install(8,(IUnknown*)bb);vc_install(9,(IUnknown*)depth);sc_check(!IDirect3DSurface9_Release(bb)&&!IDirect3DSurface9_Release(depth),"FAIL view implicit counts");
    vc_hr(IDirect3DDevice9_CreateOffscreenPlainSurface(d,4,4,21,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL view CPU surface");vc_install(10,(IUnknown*)cpu);
    D3DLOCKED_RECT lock;vc_hr(IDirect3DSurface9_LockRect(cpu,&lock,NULL,0),"FAIL view CPU lock");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]=0x12340000u+y*4+x;vc_hr(IDirect3DSurface9_UnlockRect(cpu),"FAIL view CPU unlock");
    vc_hr(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL view CPU read");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==0x12340000u+y*4+x,"FAIL actual CPU data");vc_hr(IDirect3DSurface9_UnlockRect(cpu),"FAIL view CPU read unlock");
    vc_hr(IDirect3DDevice9_CreateRenderTarget(d,4,4,21,0,0,TRUE,&rt,NULL),"FAIL view target");vc_install(11,(IUnknown*)rt);
    D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=4;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;
    vc_hr(IDirect3DDevice9_CreateAdditionalSwapChain(d,&p,&chain),"FAIL view chain");vc_install(12,(IUnknown*)chain);vc_hr(IDirect3DSwapChain9_GetBackBuffer(chain,0,0,&chainbb),"FAIL view chain back");vc_install(13,(IUnknown*)chainbb);sc_check(!IDirect3DSurface9_Release(chainbb),"FAIL chain back public count");
    vc_hr(IDirect3DDevice9_SetStreamSource(d,0,vb,0,12),"FAIL view bind VB");vc_hr(IDirect3DDevice9_SetIndices(d,ib),"FAIL view bind IB");
    vc_hr(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)t),"FAIL view bind texture");vc_hr(IDirect3DDevice9_SetTexture(d,1,(IDirect3DBaseTexture9*)c),"FAIL view bind cube");vc_hr(IDirect3DDevice9_SetTexture(d,2,(IDirect3DBaseTexture9*)v),"FAIL view bind volume");
    DWORD token=0;vc_hr(begin(d),"FAIL view record begin");vc_hr(IDirect3DDevice9_SetStreamSource(d,0,vb,0,12),"FAIL view record VB");vc_hr(IDirect3DDevice9_SetIndices(d,ib),"FAIL view record IB");for(UINT i=0;i<3;i++)vc_hr(IDirect3DDevice9_SetTexture(d,i,(IDirect3DBaseTexture9*)vc_views[i].object),"FAIL view record texture");vc_hr(end(d,&token),"FAIL view record end");
    sc_check(!IDirect3DVertexBuffer9_Release(vb)&&!IDirect3DIndexBuffer9_Release(ib)&&!IDirect3DTexture9_Release(t)&&!IDirect3DCubeTexture9_Release(c)&&!IDirect3DVolumeTexture9_Release(v),"FAIL view public zero");vc_bound(d);
    sc_check(!IDirect3DSurface9_Release(rt)&&!IDirect3DSwapChain9_Release(chain),"FAIL view target/chain counts");sc_check(vc_retirements==3&&vc_views[11].calls==1&&vc_views[12].calls==1&&vc_views[13].calls==1&&vc_views[13].sequence<vc_views[12].sequence,"FAIL view target/chain retirement");
    vc_hr(reset(d,pp),"FAIL view Reset");vc_hr(apply(d,token),"FAIL view restore");vc_bound(d);
    vc_hr(IDirect3DTexture9_GetSurfaceLevel(t,0,&ts),"FAIL retained texture level");vc_hr(IDirect3DCubeTexture9_GetCubeMapSurface(c,0,0,&cs),"FAIL retained cube level");vc_hr(IDirect3DVolumeTexture9_GetVolumeLevel(v,0,&level),"FAIL retained volume level");vc_same(5,(IUnknown*)ts);vc_same(6,(IUnknown*)cs);vc_same(7,(IUnknown*)level);sc_check(!IDirect3DSurface9_Release(ts)&&!IDirect3DSurface9_Release(cs)&&!IDirect3DVolume9_Release(level),"FAIL retained level zero");
    vc_hr(IDirect3DDevice9_GetBackBuffer(d,0,0,0,&bb),"FAIL retained back");vc_hr(IDirect3DDevice9_GetDepthStencilSurface(d,&depth),"FAIL retained depth");vc_same(8,(IUnknown*)bb);vc_same(9,(IUnknown*)depth);sc_check(!IDirect3DSurface9_Release(bb)&&!IDirect3DSurface9_Release(depth),"FAIL retained implicit zero");vc_same(10,(IUnknown*)cpu);
    for(UINT i=0;i<3;i++){vc_hr(IDirect3DDevice9_SetTexture(d,i,NULL),"FAIL view texture unbind");}
    vc_hr(IDirect3DDevice9_SetStreamSource(d,0,NULL,0,0),"FAIL view stream unbind");vc_hr(IDirect3DDevice9_SetIndices(d,NULL),"FAIL view index unbind");vc_hr(drop(d,token),"FAIL view snapshot delete");
    sc_check(vc_retirements==11&&vc_views[5].sequence<vc_views[0].sequence&&vc_views[6].sequence<vc_views[1].sequence&&vc_views[7].sequence<vc_views[2].sequence,"FAIL view child before parent retirement");sc_check(!IDirect3DSurface9_Release(cpu)&&vc_retirements==12,"FAIL view CPU retirement");sc_check(!IDirect3DDevice9_Release(d)&&vc_retirements==14,"FAIL view final device cleanup");for(UINT i=0;i<14;i++)sc_check(vc_views[i].calls==1,"FAIL missing retirement");
    char line[160];wsprintfA(line,"VIEWCORE CHECK sdk=%u views=14 callbacks=14 publicZero=1 cachedLevels=1 reset=1 CPUWords=16 finalRefs=0",sdk);sc_report(line);
}
