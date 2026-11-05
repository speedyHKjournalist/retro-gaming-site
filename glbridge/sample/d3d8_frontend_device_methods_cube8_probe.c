#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
#include "../d3d8proxy/d3d8_core_cube_texture.h"
void sc_check(BOOL,const char*);void sc_report(const char*);
void WINAPI ca9_draw(HMODULE,struct IDirect3DDevice9*,struct IDirect3DCubeTexture9*,UINT,DWORD);
void WINAPI ta9_unbind(struct IDirect3DDevice9*);
static void ca_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
typedef struct CADevice {IUnknown iface;LONG refs;IUnknown*backend;}CADevice;
static ULONG WINAPI ca_add(IUnknown*p){CADevice*d=(CADevice*)p;ULONG n=(ULONG)InterlockedIncrement(&d->refs);if(n==1)IUnknown_AddRef(d->backend);return n;}
static ULONG WINAPI ca_release(IUnknown*p){CADevice*d=(CADevice*)p;sc_check(d->refs>0,"FAIL Cube8 device underflow");ULONG n=(ULONG)InterlockedDecrement(&d->refs);if(!n)IUnknown_Release(d->backend);return n;}
static HRESULT WINAPI ca_query(IUnknown*p,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;const BYTE*a=(const BYTE*)iid,*b=(const BYTE*)&IID_IUnknown,*c=(const BYTE*)&IID_IDirect3DDevice8;BOOL same1=TRUE,same2=TRUE;if(!iid)return E_NOINTERFACE;for(UINT i=0;i<16;i++){same1&=a[i]==b[i];same2&=a[i]==c[i];}if(!same1&&!same2)return E_NOINTERFACE;*out=p;ca_add(p);return S_OK;}
static IUnknownVtbl ca_device_vtbl={ca_query,ca_add,ca_release};
static const GUID ca_tag={0x82456789,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static const DWORD ca_colors[6]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u,0xffff00ffu,0xff00ffffu};
static UINT ca_words;
static void ca_write(IDirect3DCubeTexture8*t,UINT f,UINT level,IDirect3DSurface8*s,DWORD color,UINT w){
 D3DLOCKED_RECT lock,other;BOOL direct=f&1u;
 ca_hr(direct?IDirect3DCubeTexture8_LockRect(t,(D3DCUBEMAP_FACES)f,level,&lock,NULL,0):IDirect3DSurface8_LockRect(s,&lock,NULL,0),"FAIL Cube8 lock");sc_check(lock.Pitch==(INT)(w*4),"FAIL Cube8 pitch");
 sc_check((direct?IDirect3DSurface8_LockRect(s,&other,NULL,0):IDirect3DCubeTexture8_LockRect(t,(D3DCUBEMAP_FACES)f,level,&other,NULL,0))==D3DERR_INVALIDCALL,"FAIL Cube8 shared lock");
 ca_hr(IDirect3DCubeTexture8_LockRect(t,(D3DCUBEMAP_FACES)((f+1)%6),level,&other,NULL,D3DLOCK_READONLY),"FAIL Cube8 independent face lock");ca_hr(IDirect3DCubeTexture8_UnlockRect(t,(D3DCUBEMAP_FACES)((f+1)%6),level),"FAIL Cube8 independent face unlock");
 for(UINT y=0;y<w;y++)for(UINT x=0;x<w;x++)((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]=color;
 ca_hr(direct?IDirect3DCubeTexture8_UnlockRect(t,(D3DCUBEMAP_FACES)f,level):IDirect3DSurface8_UnlockRect(s),"FAIL Cube8 unlock");
 ca_hr(IDirect3DSurface8_LockRect(s,&lock,NULL,D3DLOCK_READONLY),"FAIL Cube8 shared read");for(UINT y=0;y<w;y++)for(UINT x=0;x<w;x++){sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL Cube8 actual shadow bytes");ca_words++;}ca_hr(IDirect3DSurface8_UnlockRect(s),"FAIL Cube8 read unlock");
}
void ca_probe(HMODULE module,HWND window,UINT sdk){
 ca_words=0;
 struct IDirect3D9*(WINAPI*factory)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");
 HRESULT(WINAPI*create)(struct IDirect3D9*,UINT,UINT,HWND,DWORD,const D3DPRESENT_PARAMETERS*,struct IDirect3DDevice9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");
 HRESULT(WINAPI*reset)(struct IDirect3DDevice9*,const D3DPRESENT_PARAMETERS*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");
 HRESULT(WINAPI*cube)(struct IDirect3DDevice9*,UINT,UINT,DWORD,UINT,UINT,struct IDirect3DCubeTexture9**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CubeTexture");
 HRESULT(WINAPI*wrap)(struct IDirect3DCubeTexture9*,IDirect3DDevice8*,IDirect3DCubeTexture8**)=(void*)(uintptr_t)GetProcAddress(module,"Core8WrapCubeTexture");
 struct IDirect3DCubeTexture9*(WINAPI*backend)(IDirect3DCubeTexture8*)=(void*)(uintptr_t)GetProcAddress(module,"Core8CubeTextureBackend");
 sc_check(factory&&create&&reset&&cube&&wrap&&backend,"FAIL Cube8 exports");
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=21;pp.BackBufferCount=1;pp.SwapEffect=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=80;
 struct IDirect3D9*root=factory(sdk);sc_check(root!=NULL,"FAIL Cube8 root");struct IDirect3DDevice9*d=NULL;ca_hr(create(root,0,1,window,32,&pp,&d),"FAIL Cube8 device");sc_check(IUnknown_Release((IUnknown*)root)==1,"FAIL Cube8 root count");CADevice owner;owner.iface.lpVtbl=&ca_device_vtbl;owner.refs=1;owner.backend=(IUnknown*)d;
 struct IDirect3DCubeTexture9*raw=NULL;ca_hr(cube(d,8,0,0,21,1,&raw),"FAIL Cube8 backend");IDirect3DCubeTexture8*t=NULL;ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&t),"FAIL Cube8 wrapper");sc_check(backend(t)==raw,"FAIL Cube8 backend identity");
 const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,&IID_IDirect3DBaseTexture8,&IID_IDirect3DCubeTexture8};for(UINT i=0;i<4;i++){void*q=NULL;ca_hr(IDirect3DCubeTexture8_QueryInterface(t,ids[i],&q),"FAIL Cube8 QI");sc_check(q==t&&IUnknown_Release((IUnknown*)q)==1,"FAIL Cube8 QI count");}
 sc_check(IDirect3DCubeTexture8_GetType(t)==D3DRTYPE_CUBETEXTURE&&IDirect3DCubeTexture8_GetLevelCount(t)==4,"FAIL Cube8 type levels");sc_check(!IDirect3DCubeTexture8_SetPriority(t,0x81234567u)&&IDirect3DCubeTexture8_GetPriority(t)==0x81234567u,"FAIL Cube8 priority");sc_check(!IDirect3DCubeTexture8_SetLOD(t,99)&&IDirect3DCubeTexture8_GetLOD(t)==3&&IDirect3DCubeTexture8_SetLOD(t,0)==3&&!IDirect3DCubeTexture8_GetLOD(t),"FAIL Cube8 LOD");IDirect3DCubeTexture8_PreLoad(t);
 DWORD value=0x87654321u,got=0,size=4;ca_hr(IDirect3DCubeTexture8_SetPrivateData(t,&ca_tag,&value,4,0),"FAIL Cube8 private set");ca_hr(IDirect3DCubeTexture8_GetPrivateData(t,&ca_tag,&got,&size),"FAIL Cube8 private get");sc_check(got==value&&size==4,"FAIL Cube8 private bytes");ca_hr(IDirect3DCubeTexture8_FreePrivateData(t,&ca_tag),"FAIL Cube8 private free");
 IDirect3DDevice8*parent=NULL;ca_hr(IDirect3DCubeTexture8_GetDevice(t,&parent),"FAIL Cube8 GetDevice");sc_check((IUnknown*)parent==&owner.iface,"FAIL Cube8 frontend parent");IUnknown_Release((IUnknown*)parent);
 IDirect3DSurface8*bad=(void*)99;sc_check(IDirect3DCubeTexture8_GetCubeMapSurface(t,6,0,&bad)==D3DERR_INVALIDCALL&&!bad&&IDirect3DCubeTexture8_GetCubeMapSurface(t,0,4,&bad)==D3DERR_INVALIDCALL&&!bad&&IDirect3DCubeTexture8_GetCubeMapSurface(t,0,0,NULL)==D3DERR_INVALIDCALL,"FAIL Cube8 invalid face level output");
 IDirect3DSurface8*primary=NULL;
 for(UINT f=0;f<6;f++)for(UINT l=0;l<4;l++){
  IDirect3DSurface8*s=NULL,*again=NULL;ca_hr(IDirect3DCubeTexture8_GetCubeMapSurface(t,(D3DCUBEMAP_FACES)f,l,&s),"FAIL Cube8 real Surface8");ca_hr(IDirect3DCubeTexture8_GetCubeMapSurface(t,(D3DCUBEMAP_FACES)f,l,&again),"FAIL Cube8 cached Surface8");sc_check(again==s,"FAIL Cube8 face mip identity");IDirect3DSurface8_Release(again);
  D3DSURFACE_DESC a,b;ca_hr(IDirect3DCubeTexture8_GetLevelDesc(t,l,&a),"FAIL Cube8 level desc");ca_hr(IDirect3DSurface8_GetDesc(s,&b),"FAIL Cube8 surface desc");sc_check(a.Width==(8u>>l)&&a.Height==a.Width&&a.Size==(256u>>(l*2))&&a.Format==21&&a.Pool==1,"FAIL Cube8 desc words");sc_check(a.Width==b.Width&&a.Height==b.Height&&a.Size==b.Size,"FAIL Cube8 shared desc");
  void*container=NULL;ca_hr(IDirect3DSurface8_GetContainer(s,&IID_IDirect3DCubeTexture8,&container),"FAIL Cube8 real container");sc_check(container==t,"FAIL Cube8 container identity");IUnknown_Release((IUnknown*)container);
  if(!l)ca_write(t,f,l,s,ca_colors[f],8);
  if(f==5&&l==1)ca_write(t,f,l,s,0xff00ff00u,4);
  if(!f&&!l)primary=s;else IDirect3DSurface8_Release(s);
 }
 RECT dirty={0,0,4,4};ca_hr(IDirect3DCubeTexture8_AddDirtyRect(t,5,&dirty),"FAIL Cube8 dirty");ca_hr(IDirect3DCubeTexture8_AddDirtyRect(t,0,NULL),"FAIL Cube8 full dirty");
 for(UINT f=0;f<6;f++)ca9_draw(module,d,raw,f,ca_colors[f]);
 IDirect3DSurface8*original=primary;sc_check(IDirect3DCubeTexture8_Release(t)==1&&!IDirect3DSurface8_Release(primary),"FAIL Cube8 public zero");IUnknown_AddRef((IUnknown*)raw);IDirect3DCubeTexture8*revived=NULL;ca_hr(wrap(raw,(IDirect3DDevice8*)&owner.iface,&revived),"FAIL Cube8 revive");sc_check(revived==t,"FAIL Cube8 revived identity");ca_hr(IDirect3DCubeTexture8_GetCubeMapSurface(revived,0,0,&primary),"FAIL Cube8 child revive");sc_check(primary==original&&IDirect3DCubeTexture8_Release(revived)==1,"FAIL Cube8 child revive identity");
 ca_hr(reset(d,&pp),"FAIL Cube8 Reset");ca_write(t,0,0,primary,0xffffffffu,8);ca9_draw(module,d,raw,0,0xffffffffu);
 ta9_unbind(d);sc_check(!IDirect3DSurface8_Release(primary)&&owner.refs==1,"FAIL Cube8 final frontend count");sc_check(!IUnknown_Release(&owner.iface),"FAIL Cube8 final device count");sc_check(ca_words==464,"FAIL Cube8 actual CPU words");char line[200];wsprintfA(line,"CUBECORE CHECK sdk=%u cubeMethods=19 realContainer=1 faces=6 mipLevels=4 crossLock=1 publicZero=1 reset=1 CPUWords=464 drawnImages=7 finalRefs=0",sdk);sc_report(line);
}
