#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
#include "../d3d8proxy/d3d8_core_swap_chain.h"
void sc_check(BOOL,const char*);void sc_report(const char*);
void WINAPI sa9_read(struct IDirect3DDevice9*,struct IDirect3DSwapChain9*,DWORD);
static void ca_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
typedef struct CADevice {IUnknown iface;LONG refs;IUnknown*backend;}CADevice;
static ULONG WINAPI ca_add(IUnknown*p){CADevice*d=(CADevice*)p;ULONG n=(ULONG)InterlockedIncrement(&d->refs);if(n==1)IUnknown_AddRef(d->backend);return n;}
static ULONG WINAPI ca_release(IUnknown*p){CADevice*d=(CADevice*)p;sc_check(d->refs>0,"FAIL Cube8 device underflow");ULONG n=(ULONG)InterlockedDecrement(&d->refs);if(!n)IUnknown_Release(d->backend);return n;}
static HRESULT WINAPI ca_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;const BYTE*a=(const BYTE*)iid,*b=(const BYTE*)&IID_IUnknown,*c=(const BYTE*)&IID_IDirect3DDevice8;BOOL same1=TRUE,same2=TRUE;if(!iid)return E_NOINTERFACE;for(UINT i=0;i<16;i++){same1&=a[i]==b[i];same2&=a[i]==c[i];}if(!same1&&!same2)return E_NOINTERFACE;*out=p;ca_add(p);return S_OK;}
static IUnknownVtbl ca_device_vtbl={ca_query,ca_add,ca_release};
static UINT sa_words;
static void sa_pixels(IDirect3DSurface8*s,DWORD color){D3DLOCKED_RECT lock,other;ca_hr(IDirect3DSurface8_LockRect(s,&lock,NULL,0),"FAIL Swap8 back Lock");sc_check(lock.Pitch==16,"FAIL Swap8 back pitch");sc_check(IDirect3DSurface8_LockRect(s,&other,NULL,0)==D3DERR_INVALIDCALL,"FAIL Swap8 back double Lock");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]=color;for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++){sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL Swap8 writable CPU bytes");sa_words++;}ca_hr(IDirect3DSurface8_UnlockRect(s),"FAIL Swap8 back Unlock");ca_hr(IDirect3DSurface8_LockRect(s,&lock,NULL,D3DLOCK_READONLY),"FAIL Swap8 readonly");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++){sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL Swap8 actual CPU bytes");}ca_hr(IDirect3DSurface8_UnlockRect(s),"FAIL Swap8 readonly Unlock");}
void sa_probe(HMODULE module,HWND window,UINT sdk){
 sa_words=0;
 struct IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
 HRESULT(WINAPI*create)(struct IDirect3D9*,UINT,UINT,HWND,DWORD,const D3DPRESENT_PARAMETERS*,struct IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
 HRESULT(WINAPI*reset)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
 HRESULT(WINAPI*additional)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*,struct IDirect3DSwapChain9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8Additional");
 HRESULT(WINAPI*wrap)(struct IDirect3DSwapChain9*,IDirect3DDevice8*,IDirect3DSwapChain8**)=(void*)(uintptr_t)GetProcAddress(module,"Core8WrapSwapChain");
 struct IDirect3DSwapChain9*(WINAPI*backend)(IDirect3DSwapChain8*)=(void*)(uintptr_t)GetProcAddress(module,"Core8SwapChainBackend");sc_check(factory&&create&&reset&&additional&&wrap&&backend,"FAIL Swap8 exports");
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=21;pp.BackBufferCount=1;pp.SwapEffect=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=80;
 struct IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL Swap8 root");struct IDirect3DDevice9*d=NULL;ca_hr(create(root,0,1,window,32,&pp,&d),"FAIL Swap8 device");sc_check(IUnknown_Release((IUnknown*)root)==1,"FAIL Swap8 root count");CADevice owner;owner.iface.lpVtbl=&ca_device_vtbl;owner.refs=1;owner.backend=(IUnknown*)d;
 D3DPRESENT_PARAMETERS chain_pp;ZeroMemory(&chain_pp,sizeof(chain_pp));chain_pp.BackBufferWidth=chain_pp.BackBufferHeight=4;chain_pp.BackBufferFormat=21;chain_pp.BackBufferCount=1;chain_pp.SwapEffect=3;chain_pp.hDeviceWindow=window;chain_pp.Windowed=TRUE;chain_pp.Flags=1;
 struct IDirect3DSwapChain9*raw=NULL;ca_hr(additional(d,&chain_pp,&raw),"FAIL Swap8 additional");sc_check(chain_pp.BackBufferWidth==4&&chain_pp.SwapEffect==3&&chain_pp.Flags==1,"FAIL Swap8 immutable input");IDirect3DSwapChain8*x=NULL;ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&x),"FAIL Swap8 wrapper");sc_check(backend(x)==raw&&!backend(NULL),"FAIL Swap8 backend identity");
 const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DSwapChain8};for(UINT i=0;i<2;i++){void*q=NULL;ca_hr(IDirect3DSwapChain8_QueryInterface(x,ids[i],&q),"FAIL Swap8 QI");sc_check(q==x&&IUnknown_Release((IUnknown*)q)==1,"FAIL Swap8 QI identity refs");}void*q=(void*)99;sc_check(IDirect3DSwapChain8_QueryInterface(x,&IID_IDirect3DResource8,&q)==E_NOINTERFACE&&!q&&IDirect3DSwapChain8_QueryInterface(x,&IID_IUnknown,NULL)==E_POINTER,"FAIL Swap8 invalid QI");
 IDirect3DSurface8*bad=(void*)99;sc_check(IDirect3DSwapChain8_GetBackBuffer(x,1,0,&bad)==D3DERR_INVALIDCALL&&bad==(void*)99&&IDirect3DSwapChain8_GetBackBuffer(x,0,0,NULL)==D3DERR_INVALIDCALL,"FAIL Swap8 preserve invalid output");
 IDirect3DSurface8*back=NULL,*again_back=NULL;ca_hr(IDirect3DSwapChain8_GetBackBuffer(x,0,0,&back),"FAIL Swap8 real Surface8");ca_hr(IDirect3DSwapChain8_GetBackBuffer(x,0,(D3DBACKBUFFER_TYPE)0xffffffffu,&again_back),"FAIL Swap8 ignored stereo type");sc_check(again_back==back&&IDirect3DSurface8_Release(again_back)==1&&IDirect3DSwapChain8_AddRef(x)==2&&IDirect3DSwapChain8_Release(x)==1,"FAIL Swap8 independent counts");
 q=NULL;ca_hr(IDirect3DSurface8_GetContainer(back,&IID_IDirect3DDevice8,&q),"FAIL Swap8 device container");sc_check(q==&owner.iface,"FAIL Swap8 frontend container");IUnknown_Release((IUnknown*)q);q=(void*)99;sc_check(IDirect3DSurface8_GetContainer(back,&IID_IDirect3DSwapChain8,&q)==E_NOINTERFACE&&!q,"FAIL Swap8 container not chain");
 D3DSURFACE_DESC desc;ca_hr(IDirect3DSurface8_GetDesc(back,&desc),"FAIL Swap8 back desc");sc_check(desc.Width==4&&desc.Height==4&&desc.Size==64&&desc.Type==D3DRTYPE_SURFACE&&desc.Format==21&&desc.Pool==0&&desc.Usage==1,"FAIL Swap8 back descriptor");
 IDirect3DSwapChain8*original=x;sc_check(!IDirect3DSwapChain8_Release(x),"FAIL Swap8 public zero");ca_hr(IDirect3DSurface8_GetDesc(back,&desc),"FAIL Swap8 surface outlives chain public ref");IUnknown_AddRef((IUnknown*)raw);ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&x),"FAIL Swap8 revival");sc_check(x==original,"FAIL Swap8 cached identity");
 sa_pixels(back,0xffff0000u);ca_hr(IDirect3DSwapChain8_Present(x,NULL,NULL,NULL,NULL),"FAIL Swap8 real Present red");sa9_read(d,raw,0xffff0000u);
 RECT full={0,0,4,4};sa_pixels(back,0xff0000ffu);ca_hr(IDirect3DSwapChain8_Present(x,&full,&full,window,NULL),"FAIL Swap8 real Present blue");sa9_read(d,raw,0xff0000ffu);
 sc_check(!IDirect3DSwapChain8_Release(x),"FAIL Swap8 second public zero");IUnknown_AddRef((IUnknown*)raw);ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&x),"FAIL Swap8 second revival");ca_hr(IDirect3DSwapChain8_GetBackBuffer(x,0,0,&again_back),"FAIL Swap8 cached back after revival");sc_check(x==original&&again_back==back&&IDirect3DSurface8_Release(again_back)==1&&!IDirect3DSwapChain8_Release(x),"FAIL Swap8 revived back identity/counts");sc_check(!IDirect3DSurface8_Release(back)&&owner.refs==1,"FAIL Swap8 final frontend counts");ca_hr(reset(d,&pp),"FAIL Swap8 postrelease Reset");sc_check(!IUnknown_Release(&owner.iface),"FAIL Swap8 final device refs");
 sc_check(sa_words==32,"FAIL Swap8 CPU words");char line[220];wsprintfA(line,"SWAPCORE CHECK sdk=%u methods=5 realSurface=1 deviceContainer=1 independentRefs=1 preserveError=1 publicZero=1 reset=1 CPUWords=32 presents=2 images=6 finalRefs=0",sdk);sc_report(line);
}
