#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
#include "../d3d8proxy/d3d8_core_buffer.h"
void sc_check(BOOL,const char*);void sc_report(const char*);
void WINAPI ba9_draw(HMODULE,struct IDirect3DDevice9*,struct IDirect3DVertexBuffer9*,struct IDirect3DIndexBuffer9*,DWORD);
void WINAPI ba9_unbind(struct IDirect3DDevice9*);
static void ca_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
typedef struct CADevice {IUnknown iface;LONG refs;IUnknown*backend;}CADevice;
static ULONG WINAPI ca_add(IUnknown*p){CADevice*d=(CADevice*)p;ULONG n=(ULONG)InterlockedIncrement(&d->refs);if(n==1)IUnknown_AddRef(d->backend);return n;}
static ULONG WINAPI ca_release(IUnknown*p){CADevice*d=(CADevice*)p;sc_check(d->refs>0,"FAIL Cube8 device underflow");ULONG n=(ULONG)InterlockedDecrement(&d->refs);if(!n)IUnknown_Release(d->backend);return n;}
static HRESULT WINAPI ca_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;const BYTE*a=(const BYTE*)iid,*b=(const BYTE*)&IID_IUnknown,*c=(const BYTE*)&IID_IDirect3DDevice8;BOOL same1=TRUE,same2=TRUE;if(!iid)return E_NOINTERFACE;for(UINT i=0;i<16;i++){same1&=a[i]==b[i];same2&=a[i]==c[i];}if(!same1&&!same2)return E_NOINTERFACE;*out=p;ca_add(p);return S_OK;}
static IUnknownVtbl ca_device_vtbl={ca_query,ca_add,ca_release};
static const GUID ba_tag={0x84456789,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static UINT ba_words;
static void ba_vertices(IDirect3DVertexBuffer8*v,DWORD color){
 const DWORD xy[12]={0xbf800000u,0xbf800000u,0x41100000u,0xbf800000u,0xbf800000u,0x41100000u,0xbf800000u,0x41100000u,0x41100000u,0xbf800000u,0x41100000u,0x41100000u};DWORD wanted[30];for(UINT i=0;i<6;i++){wanted[i*5]=xy[i*2];wanted[i*5+1]=xy[i*2+1];wanted[i*5+2]=0x3f000000u;wanted[i*5+3]=0x3f800000u;wanted[i*5+4]=color;}
 BYTE*data=NULL,*other=(void*)99;ca_hr(IDirect3DVertexBuffer8_Lock(v,0,0,&data,0),"FAIL VB8 Lock");sc_check(IDirect3DVertexBuffer8_Lock(v,0,0,&other,0)==D3DERR_INVALIDCALL&&other==(void*)99,"FAIL VB8 double lock atomicity");for(UINT i=0;i<30;i++)((DWORD*)data)[i]=wanted[i];ca_hr(IDirect3DVertexBuffer8_Unlock(v),"FAIL VB8 Unlock");sc_check(IDirect3DVertexBuffer8_Unlock(v)==D3DERR_INVALIDCALL,"FAIL VB8 double Unlock");
 ca_hr(IDirect3DVertexBuffer8_Lock(v,0,120,&data,D3DLOCK_READONLY),"FAIL VB8 readonly");for(UINT i=0;i<30;i++){sc_check(((DWORD*)data)[i]==wanted[i],"FAIL VB8 actual CPU words");ba_words++;}ca_hr(IDirect3DVertexBuffer8_Unlock(v),"FAIL VB8 readonly Unlock");
}
static void ba_indices(IDirect3DIndexBuffer8*v,BOOL wide){
 BYTE*data=NULL,*other=NULL;ca_hr(IDirect3DIndexBuffer8_Lock(v,0,0,&data,0),"FAIL IB8 Lock");sc_check(IDirect3DIndexBuffer8_Lock(v,0,0,&other,0)==D3DERR_INVALIDCALL,"FAIL IB8 double Lock");for(UINT i=0;i<6;i++){if(wide)((DWORD*)data)[i]=i;else((WORD*)data)[i]=(WORD)i;}ca_hr(IDirect3DIndexBuffer8_Unlock(v),"FAIL IB8 Unlock");ca_hr(IDirect3DIndexBuffer8_Lock(v,0,0,&data,D3DLOCK_READONLY),"FAIL IB8 readonly");for(UINT i=0;i<(wide?6u:3u);i++){DWORD wanted=wide?i:((i*2+1)<<16)|(i*2);sc_check(((DWORD*)data)[i]==wanted,"FAIL IB8 actual CPU words");ba_words++;}ca_hr(IDirect3DIndexBuffer8_Unlock(v),"FAIL IB8 readonly Unlock");
}
void ba_probe(HMODULE module,HWND window,UINT sdk){
 ba_words=0;
 struct IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
 HRESULT(WINAPI*create)(struct IDirect3D9*,UINT,UINT,HWND,DWORD,const D3DPRESENT_PARAMETERS*,struct IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
 HRESULT(WINAPI*reset)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
 HRESULT(WINAPI*vertex)(struct IDirect3DDevice9*,UINT,DWORD,DWORD,UINT,struct IDirect3DVertexBuffer9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8VertexBuffer");
 HRESULT(WINAPI*index)(struct IDirect3DDevice9*,UINT,DWORD,UINT,UINT,struct IDirect3DIndexBuffer9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8IndexBuffer");
 HRESULT(WINAPI*wrap_v)(struct IDirect3DVertexBuffer9*,IDirect3DDevice8*,IDirect3DVertexBuffer8**)=(void*)(uintptr_t)GetProcAddress(module,"Core8WrapVertexBuffer");
 HRESULT(WINAPI*wrap_i)(struct IDirect3DIndexBuffer9*,IDirect3DDevice8*,IDirect3DIndexBuffer8**)=(void*)(uintptr_t)GetProcAddress(module,"Core8WrapIndexBuffer");
 struct IDirect3DVertexBuffer9*(WINAPI*back_v)(IDirect3DVertexBuffer8*)=(void*)(uintptr_t)GetProcAddress(module,"Core8VertexBufferBackend");
 struct IDirect3DIndexBuffer9*(WINAPI*back_i)(IDirect3DIndexBuffer8*)=(void*)(uintptr_t)GetProcAddress(module,"Core8IndexBufferBackend");
 sc_check(factory&&create&&reset&&vertex&&index&&wrap_v&&wrap_i&&back_v&&back_i,"FAIL Buffer8 exports");
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=21;pp.BackBufferCount=1;pp.SwapEffect=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=80;
 struct IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL Buffer8 root");struct IDirect3DDevice9*d=NULL;ca_hr(create(root,0,1,window,32,&pp,&d),"FAIL Buffer8 device");sc_check(IUnknown_Release((IUnknown*)root)==1,"FAIL Buffer8 root count");CADevice owner;owner.iface.lpVtbl=&ca_device_vtbl;owner.refs=1;owner.backend=(IUnknown*)d;
 struct IDirect3DVertexBuffer9*raw_v=NULL;struct IDirect3DIndexBuffer9*raw_i16=NULL,*raw_i32=NULL;
 ca_hr(vertex(d,120,0,0x44,1,&raw_v),"FAIL VB8 backend");ca_hr(index(d,12,0,101,1,&raw_i16),"FAIL IB16 backend");ca_hr(index(d,24,0,102,1,&raw_i32),"FAIL IB32 backend");
 IDirect3DVertexBuffer8*v=NULL;IDirect3DIndexBuffer8*i16=NULL,*i32=NULL;ca_hr(wrap_v(raw_v,(IDirect3DDevice8*)&owner.iface,&v),"FAIL VB8 wrapper");ca_hr(wrap_i(raw_i16,(IDirect3DDevice8*)&owner.iface,&i16),"FAIL IB16 wrapper");ca_hr(wrap_i(raw_i32,(IDirect3DDevice8*)&owner.iface,&i32),"FAIL IB32 wrapper");sc_check(back_v(v)==raw_v&&back_i(i16)==raw_i16&&back_i(i32)==raw_i32&&!back_i((IDirect3DIndexBuffer8*)v)&&!back_v((IDirect3DVertexBuffer8*)i32),"FAIL Buffer8 borrowed backend class");
 IUnknown*views[3]={(IUnknown*)v,(IUnknown*)i16,(IUnknown*)i32};for(UINT n=0;n<3;n++){
  const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,n?&IID_IDirect3DIndexBuffer8:&IID_IDirect3DVertexBuffer8};for(UINT k=0;k<3;k++){void*q=NULL;ca_hr(IUnknown_QueryInterface(views[n],ids[k],&q),"FAIL Buffer8 QI");sc_check(q==views[n]&&IUnknown_Release((IUnknown*)q)==1,"FAIL Buffer8 QI identity refs");}
  IDirect3DResource8*r=(IDirect3DResource8*)views[n];IDirect3DDevice8*device=NULL;ca_hr(IDirect3DResource8_GetDevice(r,&device),"FAIL Buffer8 GetDevice");sc_check((IUnknown*)device==&owner.iface,"FAIL Buffer8 frontend device");IUnknown_Release((IUnknown*)device);
  sc_check(IDirect3DResource8_GetType(r)==(n?D3DRTYPE_INDEXBUFFER:D3DRTYPE_VERTEXBUFFER),"FAIL Buffer8 type");sc_check(!IDirect3DResource8_SetPriority(r,0x81234567u)&&IDirect3DResource8_GetPriority(r)==0x81234567u,"FAIL Buffer8 priority");IDirect3DResource8_PreLoad(r);
  DWORD value=0x81230000u+n,got=0,size=4;ca_hr(IDirect3DResource8_SetPrivateData(r,&ba_tag,&value,4,0),"FAIL Buffer8 private set");ca_hr(IDirect3DResource8_GetPrivateData(r,&ba_tag,&got,&size),"FAIL Buffer8 private get");sc_check(got==value&&size==4,"FAIL Buffer8 private data");ca_hr(IDirect3DResource8_FreePrivateData(r,&ba_tag),"FAIL Buffer8 private free");
 }
 D3DVERTEXBUFFER_DESC vd;ca_hr(IDirect3DVertexBuffer8_GetDesc(v,&vd),"FAIL VB8 desc");sc_check(vd.Format==D3DFMT_VERTEXDATA&&vd.Type==D3DRTYPE_VERTEXBUFFER&&!vd.Usage&&vd.Pool==1&&vd.Size==120&&vd.FVF==0x44,"FAIL VB8 sixword desc");for(UINT n=0;n<2;n++){D3DINDEXBUFFER_DESC id;ca_hr(IDirect3DIndexBuffer8_GetDesc(n?i32:i16,&id),"FAIL IB8 desc");sc_check(id.Format==(n?D3DFMT_INDEX32:D3DFMT_INDEX16)&&id.Type==D3DRTYPE_INDEXBUFFER&&!id.Usage&&id.Pool==1&&id.Size==(n?24u:12u),"FAIL IB8 fiveword desc");}
 ba_vertices(v,0xffff0000u);ba_indices(i16,FALSE);ba_indices(i32,TRUE);ba9_draw(module,d,raw_v,NULL,0xffff0000u);ba9_draw(module,d,raw_v,raw_i16,0xffff0000u);ba9_draw(module,d,raw_v,raw_i32,0xffff0000u);
 IDirect3DVertexBuffer8*original_v=v;IDirect3DIndexBuffer8*original_i=i32;sc_check(!IDirect3DVertexBuffer8_Release(v)&&!IDirect3DIndexBuffer8_Release(i32),"FAIL Buffer8 public zero");IUnknown_AddRef((IUnknown*)raw_v);IUnknown_AddRef((IUnknown*)raw_i32);ca_hr(wrap_v(raw_v,(IDirect3DDevice8*)&owner.iface,&v),"FAIL VB8 revive");ca_hr(wrap_i(raw_i32,(IDirect3DDevice8*)&owner.iface,&i32),"FAIL IB8 revive");sc_check(v==original_v&&i32==original_i,"FAIL Buffer8 cached identity");
 ca_hr(reset(d,&pp),"FAIL Buffer8 Reset");ba_vertices(v,0xff0000ffu);ba9_draw(module,d,raw_v,raw_i32,0xff0000ffu);
 ba9_unbind(d);sc_check(!IDirect3DVertexBuffer8_Release(v)&&!IDirect3DIndexBuffer8_Release(i16)&&!IDirect3DIndexBuffer8_Release(i32)&&owner.refs==1,"FAIL Buffer8 final frontend counts");sc_check(!IUnknown_Release(&owner.iface),"FAIL Buffer8 final device refs");sc_check(ba_words==69,"FAIL Buffer8 CPU words");char line[200];wsprintfA(line,"BUFFERCORE CHECK sdk=%u vertexMethods=14 indexMethods=14 shared=1 index16=1 index32=1 publicZero=1 reset=1 CPUWords=69 drawnImages=4 finalRefs=0",sdk);sc_report(line);
}
