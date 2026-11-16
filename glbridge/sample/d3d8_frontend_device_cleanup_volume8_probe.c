#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
#include "../d3d8proxy/d3d8_core_volume_texture.h"
void sc_check(BOOL,const char*);void sc_report(const char*);
void WINAPI va9_draw(HMODULE,struct IDirect3DDevice9*,struct IDirect3DVolumeTexture9*,UINT,DWORD);
void WINAPI ta9_unbind(struct IDirect3DDevice9*);
static void ca_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
typedef struct CADevice {IUnknown iface;LONG refs;IUnknown*backend;}CADevice;
static ULONG WINAPI ca_add(IUnknown*p){CADevice*d=(CADevice*)p;ULONG n=(ULONG)InterlockedIncrement(&d->refs);if(n==1)IUnknown_AddRef(d->backend);return n;}
static ULONG WINAPI ca_release(IUnknown*p){CADevice*d=(CADevice*)p;sc_check(d->refs>0,"FAIL Cube8 device underflow");ULONG n=(ULONG)InterlockedDecrement(&d->refs);if(!n)IUnknown_Release(d->backend);return n;}
static HRESULT WINAPI ca_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;const BYTE*a=(const BYTE*)iid,*b=(const BYTE*)&IID_IUnknown,*c=(const BYTE*)&IID_IDirect3DDevice8;BOOL same1=TRUE,same2=TRUE;if(!iid)return E_NOINTERFACE;for(UINT i=0;i<16;i++){same1&=a[i]==b[i];same2&=a[i]==c[i];}if(!same1&&!same2)return E_NOINTERFACE;*out=p;ca_add(p);return S_OK;}
static IUnknownVtbl ca_device_vtbl={ca_query,ca_add,ca_release};
static const GUID va_tag={0x83456789,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static const DWORD va_colors[4]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u};
static UINT va_words;
static DWORD va_color(UINT mode,UINT z){return mode==2?0xffffffffu:mode==1?0xffff00ffu:va_colors[z];}
static void va_write(IDirect3DVolumeTexture8*t,UINT level,IDirect3DVolume8*v,UINT mode){
 const UINT w=level?4:8,h=level?2:4,depth=level?2:4;D3DLOCKED_BOX lock,other;
 ca_hr(level?IDirect3DVolumeTexture8_LockBox(t,level,&lock,NULL,0):IDirect3DVolume8_LockBox(v,&lock,NULL,0),"FAIL Volume8 lock");sc_check(lock.RowPitch==(INT)(w*4)&&lock.SlicePitch==(INT)(w*h*4),"FAIL Volume8 row slice pitch");
 sc_check((level?IDirect3DVolume8_LockBox(v,&other,NULL,0):IDirect3DVolumeTexture8_LockBox(t,level,&other,NULL,0))==D3DERR_INVALIDCALL,"FAIL Volume8 shared lock");
 ca_hr(IDirect3DVolumeTexture8_LockBox(t,level?0:1,&other,NULL,D3DLOCK_READONLY),"FAIL Volume8 independent mip lock");ca_hr(IDirect3DVolumeTexture8_UnlockBox(t,level?0:1),"FAIL Volume8 independent mip unlock");
 for(UINT z=0;z<depth;z++)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++)((DWORD*)((BYTE*)lock.pBits+z*lock.SlicePitch+y*lock.RowPitch))[x]=va_color(mode,z);
 ca_hr(level?IDirect3DVolumeTexture8_UnlockBox(t,level):IDirect3DVolume8_UnlockBox(v),"FAIL Volume8 unlock");
 ca_hr(IDirect3DVolume8_LockBox(v,&lock,NULL,D3DLOCK_READONLY),"FAIL Volume8 shared read");for(UINT z=0;z<depth;z++)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){sc_check(((DWORD*)((BYTE*)lock.pBits+z*lock.SlicePitch+y*lock.RowPitch))[x]==va_color(mode,z),"FAIL Volume8 actual CPU bytes");va_words++;}ca_hr(IDirect3DVolume8_UnlockBox(v),"FAIL Volume8 read unlock");
}
void va_probe(HMODULE module,HWND window,UINT sdk){
 va_words=0;
 struct IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
 HRESULT(WINAPI*create)(struct IDirect3D9*,UINT,UINT,HWND,DWORD,const D3DPRESENT_PARAMETERS*,struct IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
 HRESULT(WINAPI*reset)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
 HRESULT(WINAPI*volume)(struct IDirect3DDevice9*,UINT,UINT,UINT,UINT,DWORD,UINT,UINT,struct IDirect3DVolumeTexture9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8VolumeTexture");
 HRESULT(WINAPI*wrap)(struct IDirect3DVolumeTexture9*,IDirect3DDevice8*,IDirect3DVolumeTexture8**)=(void*)(uintptr_t)GetProcAddress(module,"Core8WrapVolumeTexture");
 struct IDirect3DVolumeTexture9*(WINAPI*backend)(IDirect3DVolumeTexture8*)=(void*)(uintptr_t)GetProcAddress(module,"Core8VolumeTextureBackend");
 sc_check(factory&&create&&reset&&volume&&wrap&&backend,"FAIL Volume8 exports");
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=21;pp.BackBufferCount=1;pp.SwapEffect=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=80;
 struct IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL Volume8 root");struct IDirect3DDevice9*d=NULL;ca_hr(create(root,0,1,window,32,&pp,&d),"FAIL Volume8 device");sc_check(IUnknown_Release((IUnknown*)root)==1,"FAIL Volume8 root count");CADevice owner;owner.iface.lpVtbl=&ca_device_vtbl;owner.refs=1;owner.backend=(IUnknown*)d;
 struct IDirect3DVolumeTexture9*raw=NULL;ca_hr(volume(d,8,4,4,0,0,21,1,&raw),"FAIL Volume8 backend");IDirect3DVolumeTexture8*t=NULL;ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&t),"FAIL Volume8 wrapper");sc_check(backend(t)==raw,"FAIL Volume8 backend identity");
 const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,&IID_IDirect3DBaseTexture8,&IID_IDirect3DVolumeTexture8};for(UINT i=0;i<4;i++){void*q=NULL;ca_hr(IDirect3DVolumeTexture8_QueryInterface(t,ids[i],&q),"FAIL VolumeTexture8 QI");sc_check(q==t&&IUnknown_Release((IUnknown*)q)==1,"FAIL VolumeTexture8 QI count");}
 sc_check(IDirect3DVolumeTexture8_GetType(t)==D3DRTYPE_VOLUMETEXTURE&&IDirect3DVolumeTexture8_GetLevelCount(t)==4,"FAIL VolumeTexture8 type levels");sc_check(!IDirect3DVolumeTexture8_SetPriority(t,0x81234567u)&&IDirect3DVolumeTexture8_GetPriority(t)==0x81234567u,"FAIL VolumeTexture8 priority");sc_check(!IDirect3DVolumeTexture8_SetLOD(t,99)&&IDirect3DVolumeTexture8_GetLOD(t)==3&&IDirect3DVolumeTexture8_SetLOD(t,0)==3&&!IDirect3DVolumeTexture8_GetLOD(t),"FAIL VolumeTexture8 LOD");IDirect3DVolumeTexture8_PreLoad(t);
 DWORD value=0x87654321u,got=0,size=4;ca_hr(IDirect3DVolumeTexture8_SetPrivateData(t,&va_tag,&value,4,0),"FAIL VolumeTexture8 private set");ca_hr(IDirect3DVolumeTexture8_GetPrivateData(t,&va_tag,&got,&size),"FAIL VolumeTexture8 private get");sc_check(got==value&&size==4,"FAIL VolumeTexture8 private bytes");ca_hr(IDirect3DVolumeTexture8_FreePrivateData(t,&va_tag),"FAIL VolumeTexture8 private free");
 IDirect3DDevice8*parent=NULL;ca_hr(IDirect3DVolumeTexture8_GetDevice(t,&parent),"FAIL VolumeTexture8 GetDevice");sc_check((IUnknown*)parent==&owner.iface,"FAIL VolumeTexture8 frontend parent");IUnknown_Release((IUnknown*)parent);
 IDirect3DVolume8*bad=(void*)99;sc_check(IDirect3DVolumeTexture8_GetVolumeLevel(t,4,&bad)==D3DERR_INVALIDCALL&&!bad&&IDirect3DVolumeTexture8_GetVolumeLevel(t,0,NULL)==D3DERR_INVALIDCALL,"FAIL VolumeTexture8 invalid level output");
 IDirect3DVolume8*levels[4]={0};
 for(UINT l=0;l<4;l++){
  IDirect3DVolume8*v=NULL,*again=NULL;ca_hr(IDirect3DVolumeTexture8_GetVolumeLevel(t,l,&v),"FAIL real Volume8 child");levels[l]=v;ca_hr(IDirect3DVolumeTexture8_GetVolumeLevel(t,l,&again),"FAIL cached Volume8");sc_check(again==v,"FAIL Volume8 mip identity");IDirect3DVolume8_Release(again);
  D3DVOLUME_DESC a,b;const UINT w=8u>>l,h=l<3?4u>>l:1;ca_hr(IDirect3DVolumeTexture8_GetLevelDesc(t,l,&a),"FAIL VolumeTexture8 desc");ca_hr(IDirect3DVolume8_GetDesc(v,&b),"FAIL Volume8 desc");sc_check(a.Width==w&&a.Height==h&&a.Depth==h&&a.Size==w*h*h*4&&a.Format==21&&a.Type==D3DRTYPE_VOLUME&&a.Pool==1,"FAIL VolumeTexture8 eightword desc");sc_check(a.Width==b.Width&&a.Height==b.Height&&a.Depth==b.Depth&&a.Size==b.Size,"FAIL Volume8 shared desc");
  void*q=NULL;ca_hr(IDirect3DVolume8_QueryInterface(v,&IID_IDirect3DVolume8,&q),"FAIL Volume8 QI");sc_check(q==v,"FAIL Volume8 QI identity");IUnknown_Release((IUnknown*)q);q=(void*)99;sc_check(IDirect3DVolume8_QueryInterface(v,&IID_IDirect3DResource8,&q)==E_NOINTERFACE&&!q,"FAIL Volume8 nonresource QI");
  ca_hr(IDirect3DVolume8_GetContainer(v,&IID_IDirect3DVolumeTexture8,&q),"FAIL Volume8 real container");sc_check(q==t,"FAIL Volume8 real parent identity");IUnknown_Release((IUnknown*)q);
  ca_hr(IDirect3DVolume8_GetDevice(v,&parent),"FAIL Volume8 GetDevice");sc_check((IUnknown*)parent==&owner.iface,"FAIL Volume8 frontend device");IUnknown_Release((IUnknown*)parent);
  value=0x12340000u+l;got=0;size=4;ca_hr(IDirect3DVolume8_SetPrivateData(v,&va_tag,&value,4,0),"FAIL Volume8 private set");ca_hr(IDirect3DVolume8_GetPrivateData(v,&va_tag,&got,&size),"FAIL Volume8 private get");sc_check(got==value&&size==4,"FAIL Volume8 private level bytes");ca_hr(IDirect3DVolume8_FreePrivateData(v,&va_tag),"FAIL Volume8 private free");
 }
 va_write(t,0,levels[0],0);va_write(t,1,levels[1],1);for(UINT l=1;l<4;l++)IDirect3DVolume8_Release(levels[l]);
 D3DBOX dirty={0,0,4,2,0,2};ca_hr(IDirect3DVolumeTexture8_AddDirtyBox(t,&dirty),"FAIL VolumeTexture8 dirty");ca_hr(IDirect3DVolumeTexture8_AddDirtyBox(t,NULL),"FAIL VolumeTexture8 full dirty");
 for(UINT z=0;z<4;z++)va9_draw(module,d,raw,z,va_colors[z]);
 IDirect3DVolume8*original=levels[0];sc_check(IDirect3DVolumeTexture8_Release(t)==1&&!IDirect3DVolume8_Release(levels[0]),"FAIL Volume8 public zero");IUnknown_AddRef((IUnknown*)raw);IDirect3DVolumeTexture8*revived=NULL;ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&revived),"FAIL VolumeTexture8 revive");sc_check(revived==t,"FAIL VolumeTexture8 revived identity");ca_hr(IDirect3DVolumeTexture8_GetVolumeLevel(revived,0,&levels[0]),"FAIL Volume8 child revive");sc_check(levels[0]==original&&IDirect3DVolumeTexture8_Release(revived)==1,"FAIL Volume8 revived count");
 ca_hr(reset(d,&pp),"FAIL Volume8 Reset");IDirect3DVolume8*again=NULL;ca_hr(IDirect3DVolumeTexture8_GetVolumeLevel(t,0,&again),"FAIL Volume8 postReset child");sc_check(again==original&&IDirect3DVolume8_Release(again)==1,"FAIL Volume8 postReset identity");va_write(t,0,levels[0],2);va9_draw(module,d,raw,0,0xffffffffu);
 ta9_unbind(d);sc_check(!IDirect3DVolume8_Release(levels[0])&&owner.refs==1,"FAIL Volume8 final frontend count");sc_check(!IUnknown_Release(&owner.iface),"FAIL Volume8 final device count");sc_check(va_words==272,"FAIL Volume8 CPU words");char line[210];wsprintfA(line,"VOLUMECORE CHECK sdk=%u textureMethods=19 volumeMethods=11 realContainer=1 mipLevels=4 crossLock=1 publicZero=1 reset=1 CPUWords=272 drawnImages=5 finalRefs=0",sdk);sc_report(line);
}
