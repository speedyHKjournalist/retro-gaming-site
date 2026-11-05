#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
void sc_check(BOOL,const char*);void sc_report(const char*);
HRESULT WINAPI ta9_back(struct IDirect3DDevice9*,struct IDirect3DSurface9**);
HRESULT WINAPI ta9_depth(struct IDirect3DDevice9*,struct IDirect3DSurface9**);
HRESULT WINAPI dv9_private(struct IDirect3DSurface9*,IUnknown*);
HRESULT WINAPI ds9_bind(struct IDirect3DDevice9*,IUnknown*,IUnknown*,IUnknown*);
void ds9_query(struct IDirect3DDevice9*,IUnknown*,IUnknown*,IUnknown*);
void ds9_metadata(struct IDirect3DDevice9*,IUnknown*,IUnknown*,IUnknown*);
typedef HRESULT(WINAPI*DVStorageRef)(struct IDirect3DDevice9*,IUnknown*);
typedef HRESULT(WINAPI*DVGet)(IUnknown*,void**);
typedef HRESULT(WINAPI*DVAttach)(IUnknown*,void*,void(WINAPI*)(void*));
typedef struct DVDevice DVDevice;
typedef struct DVChild {DVDevice*owner;IUnknown*object;volatile LONG retired;}DVChild;
typedef struct DVFinalizer {IUnknown iface;volatile LONG refs,calls,done;DVDevice*owner;BOOL retain;}DVFinalizer;
struct DVDevice {IUnknown iface;volatile LONG refs,retired,children,callbacks;IUnknown*backend;DVGet get;DVAttach attach;DVFinalizer*finalizer;IUnknown*storage[3];DVStorageRef storage_release;UINT storage_released;};
static void dv_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
static BOOL dv_guid(REFIID a,REFIID b){if(!a||!b)return FALSE;const BYTE*x=(const BYTE*)a,*y=(const BYTE*)b;for(UINT i=0;i<16;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
/* Explicit IUnknown-only frontend-device fixture;metadata is on the stack for
 * these lifetime probes. This is not the full IDirect3DDevice8 implementation. */
static ULONG WINAPI dv_add(IUnknown*p){DVDevice*x=(DVDevice*)p;ULONG n=(ULONG)InterlockedIncrement(&x->refs);if(n==1)IUnknown_AddRef(x->backend);return n;}
static ULONG WINAPI dv_release(IUnknown*p){DVDevice*x=(DVDevice*)p;sc_check(x->refs>0,"FAIL device-view frontend underflow");ULONG n=(ULONG)InterlockedDecrement(&x->refs);if(!n){IUnknown_Release(x->backend);return (ULONG)InterlockedCompareExchange(&x->refs,0,0);}return n;}
static HRESULT WINAPI dv_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(!dv_guid(iid,&IID_IUnknown)&&!dv_guid(iid,&IID_IDirect3DDevice8))return E_NOINTERFACE;*out=p;dv_add(p);return S_OK;}
static IUnknownVtbl dv_device_vtbl={dv_query,dv_add,dv_release};
static void WINAPI dv_device_retired(void*p){DVDevice*x=p;sc_check(!x->retired&&!x->refs&&x->children==2&&x->callbacks==2&&x->finalizer->calls==1&&!x->finalizer->refs&&x->finalizer->done,"FAIL device metadata retired before children/finalizer");void*found=(void*)99;sc_check(x->get(x->backend,&found)==D3DERR_INVALIDCALL&&!found,"FAIL device retiring lookup tombstone");sc_check(x->attach(x->backend,x,dv_device_retired)==D3DERR_INVALIDCALL,"FAIL device retiring reattach");for(UINT i=0;i<3;i++){dv_hr(x->storage_release((struct IDirect3DDevice9*)x->backend,x->storage[i]),"FAIL late shader storage release");x->storage_released++;}InterlockedIncrement(&x->callbacks);InterlockedExchange(&x->retired,1);}
static void WINAPI dv_child_retired(void*p){DVChild*x=p;DVDevice*owner=x->owner;void*found=NULL;sc_check(!x->retired&&!owner->retired,"FAIL implicit metadata after device metadata");dv_hr(owner->get(owner->backend,&found),"FAIL parent device view during child retirement");sc_check(found==owner,"FAIL parent metadata identity during child retirement");found=(void*)99;sc_check(owner->get(x->object,&found)==D3DERR_INVALIDCALL&&!found,"FAIL implicit retiring tombstone");InterlockedIncrement(&owner->children);InterlockedIncrement(&owner->callbacks);InterlockedExchange(&x->retired,1);}
static ULONG WINAPI dv_final_add(IUnknown*p){return (ULONG)InterlockedIncrement(&((DVFinalizer*)p)->refs);}
static ULONG WINAPI dv_final_release(IUnknown*p){DVFinalizer*x=(DVFinalizer*)p;sc_check(x->refs>0,"FAIL device private finalizer underflow");ULONG n=(ULONG)InterlockedDecrement(&x->refs);if(!n){DVDevice*owner=x->owner;void*found=NULL;sc_check(!x->calls&&!owner->retired&&!owner->children&&!owner->refs,"FAIL private finalizer metadata lifetime");dv_hr(owner->get(owner->backend,&found),"FAIL device metadata lookup in private callback");sc_check(found==owner,"FAIL finalizer device metadata identity");ds9_metadata((struct IDirect3DDevice9*)owner->backend,owner->storage[0],owner->storage[1],owner->storage[2]);IUnknown*retained=NULL;dv_hr(IUnknown_QueryInterface(&owner->iface,&IID_IDirect3DDevice8,(void**)&retained),"FAIL frontend device revival in finalizer");sc_check(retained==&owner->iface&&owner->refs==1,"FAIL frontend logical-zero revival");if(!x->retain)sc_check(!IUnknown_Release(retained)&&!owner->refs,"FAIL transient device revival release");InterlockedIncrement(&x->calls);InterlockedExchange(&x->done,1);}return n;}
static HRESULT WINAPI dv_final_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(!dv_guid(iid,&IID_IUnknown))return E_NOINTERFACE;*out=p;dv_final_add(p);return S_OK;}
static IUnknownVtbl dv_finalizer_vtbl={dv_final_query,dv_final_add,dv_final_release};
static void dv_wait(volatile LONG*value,const char*m){for(UINT i=0;i<200&&!*value;i++)Sleep(10);sc_check(*value==1,m);}
void dv_probe(HMODULE module,HWND window,UINT sdk){
 struct IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
 HRESULT(WINAPI*create)(struct IDirect3D9*,UINT,UINT,HWND,DWORD,const D3DPRESENT_PARAMETERS*,struct IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
 HRESULT(WINAPI*reset)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
 DVGet get=(void*)(uintptr_t)GetProcAddress(module,"Core8ViewGet");DVAttach attach=(void*)(uintptr_t)GetProcAddress(module,"Core8ViewAttach");sc_check(factory&&create&&reset&&get&&attach,"FAIL device view exports");
 DVStorageRef hold=(void*)(uintptr_t)GetProcAddress(module,"Core8StorageAdd"),release=(void*)(uintptr_t)GetProcAddress(module,"Core8StorageRelease");
 HRESULT(WINAPI*vertex)(struct IDirect3DDevice9*,const DWORD*,const DWORD*,struct IDirect3DVertexDeclaration9**,struct IDirect3DVertexShader9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8VertexShader");
 HRESULT(WINAPI*pixel)(struct IDirect3DDevice9*,const DWORD*,struct IDirect3DPixelShader9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8PixelShader");sc_check(hold&&release&&vertex&&pixel,"FAIL shader storage exports");
 const DWORD declaration[]={0x20000000u,0x40030000u,0xffffffffu},vs[]={0xfffe0101u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0x90e40000u,0xffffu},ps[]={0xffff0101u,1,0x800f0000u,0x90e40000u,0xffffu};
 UINT total_callbacks=0,total_released=0;
 for(UINT mode=0;mode<2;mode++){
  D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=21;pp.BackBufferCount=1;pp.SwapEffect=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=80;
  struct IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL device-view root");struct IDirect3DDevice9*d=NULL;dv_hr(create(root,0,1,window,32,&pp,&d),"FAIL device-view create");sc_check(IUnknown_Release((IUnknown*)root)==1,"FAIL device-view root pin");
  DVDevice meta;ZeroMemory(&meta,sizeof(meta));meta.iface.lpVtbl=&dv_device_vtbl;meta.refs=1;meta.backend=(IUnknown*)d;meta.get=get;meta.attach=attach;
  void*found=(void*)99;dv_hr(get((IUnknown*)d,&found),"FAIL empty device-view lookup");sc_check(!found,"FAIL stale device metadata reused");dv_hr(attach((IUnknown*)d,&meta,dv_device_retired),"FAIL weak device attach");dv_hr(get((IUnknown*)d,&found),"FAIL attached device lookup");sc_check(found==&meta&&attach((IUnknown*)d,&meta,dv_device_retired)==D3DERR_INVALIDCALL,"FAIL device view identity/duplicate");
  struct IDirect3DVertexDeclaration9*layout=NULL;struct IDirect3DVertexShader9*vs_object=NULL;struct IDirect3DPixelShader9*ps_object=NULL;
  dv_hr(vertex(d,declaration,vs,&layout,&vs_object),"FAIL storage shader pair");dv_hr(pixel(d,ps,&ps_object),"FAIL storage pixel shader");meta.storage[0]=(IUnknown*)layout;meta.storage[1]=(IUnknown*)vs_object;meta.storage[2]=(IUnknown*)ps_object;meta.storage_release=release;
  for(UINT i=0;i<3;i++){dv_hr(hold(d,meta.storage[i]),"FAIL private shader hold");sc_check(!IUnknown_Release(meta.storage[i]),"FAIL shader public-zero adoption");dv_hr(hold(d,meta.storage[i]),"FAIL public-zero private clone");dv_hr(release(d,meta.storage[i]),"FAIL cloned hold release");}
  dv_hr(ds9_bind(d,meta.storage[0],meta.storage[1],meta.storage[2]),"FAIL storage initial bind");ds9_query(d,meta.storage[0],meta.storage[1],meta.storage[2]);
  struct IDirect3DSurface9*back=NULL,*depth=NULL;dv_hr(ta9_back(d,&back),"FAIL device-view implicit back");dv_hr(ta9_depth(d,&depth),"FAIL device-view implicit depth");DVChild back_view={&meta,(IUnknown*)back,0},depth_view={&meta,(IUnknown*)depth,0};dv_hr(attach((IUnknown*)back,&back_view,dv_child_retired),"FAIL implicit back view attach");dv_hr(attach((IUnknown*)depth,&depth_view,dv_child_retired),"FAIL implicit depth view attach");sc_check(!IUnknown_Release((IUnknown*)back)&&!IUnknown_Release((IUnknown*)depth),"FAIL implicit public zero");
  dv_hr(reset(d,&pp),"FAIL device-view Reset");dv_hr(get((IUnknown*)d,&found),"FAIL device view after Reset");sc_check(found==&meta&&!meta.retired&&!meta.children,"FAIL device metadata Reset identity");struct IDirect3DSurface9*again=NULL;dv_hr(ta9_back(d,&again),"FAIL postReset back");sc_check(again==back,"FAIL implicit back storage Reset identity");dv_hr(get((IUnknown*)again,&found),"FAIL postReset back metadata");sc_check(found==&back_view&&!IUnknown_Release((IUnknown*)again),"FAIL implicit back view Reset identity");dv_hr(ta9_depth(d,&again),"FAIL postReset depth");sc_check(again==depth,"FAIL implicit depth storage Reset identity");dv_hr(get((IUnknown*)again,&found),"FAIL postReset depth metadata");sc_check(found==&depth_view&&!IUnknown_Release((IUnknown*)again),"FAIL implicit depth view Reset identity");
  dv_hr(ds9_bind(d,meta.storage[0],meta.storage[1],meta.storage[2]),"FAIL storage postReset bind");ds9_query(d,meta.storage[0],meta.storage[1],meta.storage[2]);
  DVFinalizer finalizer;ZeroMemory(&finalizer,sizeof(finalizer));finalizer.iface.lpVtbl=&dv_finalizer_vtbl;finalizer.owner=&meta;finalizer.retain=mode!=0;meta.finalizer=&finalizer;dv_hr(dv9_private(back,&finalizer.iface),"FAIL actual implicit private-data finalizer");sc_check(finalizer.refs==1,"FAIL actual private IUnknown ownership");
  IUnknown_AddRef((IUnknown*)d);sc_check(!IUnknown_Release(&meta.iface)&&!meta.refs&&!meta.retired&&!meta.children&&!finalizer.calls,"FAIL logical-zero device prematurely retired");dv_hr(get((IUnknown*)d,&found),"FAIL logical-zero device metadata");sc_check(found==&meta,"FAIL weak device identity with backend pin");IUnknown_Release((IUnknown*)d);
  dv_wait(&finalizer.done,"FAIL missing device private callback");if(mode){ds9_metadata(d,meta.storage[0],meta.storage[1],meta.storage[2]);sc_check(meta.refs==1&&!meta.retired&&!meta.children,"FAIL retained frontend device metadata");dv_hr(get((IUnknown*)d,&found),"FAIL retained device metadata lookup");sc_check(found==&meta&&!IUnknown_Release(&meta.iface),"FAIL retained device final release");}
  dv_wait(&meta.retired,"FAIL device weak metadata leak/pin");sc_check(!meta.refs&&meta.children==2&&meta.callbacks==3&&back_view.retired==1&&depth_view.retired==1&&!finalizer.refs&&finalizer.calls==1,"FAIL device view final cleanup");sc_check(meta.storage_released==3,"FAIL shader storage ownership leak");total_released+=meta.storage_released;total_callbacks+=(UINT)meta.callbacks;
 }
 sc_check(total_callbacks==6&&total_released==6,"FAIL device-view/storage callback count");char line[200];wsprintfA(line,"DEVICEVIEW CHECK sdk=%u devices=2 modes=2 callbacks=6 deviceLast=1 transient=1 retained=1 reset=1 noPins=1 finalRefs=0",sdk);sc_report(line);wsprintfA(line,"SHADERSTORAGE CHECK sdk=%u devices=2 privateHolds=6 publicZero=1 metadata=1 reset=1 finalizerRevival=1 lateDestroy=6 finalRefs=0",sdk);sc_report(line);
}
