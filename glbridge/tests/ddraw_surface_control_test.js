"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_PLATFORM_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),platform=fs.readFileSync(process.env.DDRAW_PLATFORM_INCLUDE||root+'/ddrawproxy/ddraw_platform_control.inc','utf8'),surfaceControl=fs.readFileSync(process.env.DDRAW_SURFACE_CONTROL_INCLUDE||root+'/ddrawproxy/ddraw_surface_control.inc','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-surface-control-'));
const primaryNative=JSON.parse(fs.readFileSync(path.resolve(root,'../../v86/tests/glbridge/fixtures/ddraw_native_primary_loss.json')));assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(root+'/../'+primaryNative.source)).digest('hex'),primaryNative.sourceSha256);
const native=JSON.parse(fs.readFileSync(path.resolve(root,'../../v86/tests/glbridge/fixtures/ddraw_native_control_lifetime.json')));assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(root+'/../'+native.source)).digest('hex'),native.sourceSha256);
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
const newTests=String.raw`
static const struct{const char*name;DWORD desc,lost,cooperative,width,height,bpp;LONG pitch;}native_surfaces[]={NATIVE_SURFACES};
static void check_primary(DDrawObject*o,DDSurface*s,const char*name){unsigned i;for(i=0;i<sizeof(native_surfaces)/sizeof(*native_surfaces);i++)if(!strcmp(native_surfaces[i].name,name))break;MUST(i<sizeof(native_surfaces)/sizeof(*native_surfaces));DDSURFACEDESC2 desc={.dwSize=sizeof(desc)};DDPIXELFORMAT pf={0};MUST((DWORD)surface_GetSurfaceDesc(s,&desc)==native_surfaces[i].desc);MUST((DWORD)surface_IsLost(s)==native_surfaces[i].lost);MUST((DWORD)ddraw_TestCooperativeLevel(o)==native_surfaces[i].cooperative);MUST(desc.dwWidth==native_surfaces[i].width&&desc.dwHeight==native_surfaces[i].height&&desc.lPitch==native_surfaces[i].pitch&&desc.ddpfPixelFormat.dwRGBBitCount==native_surfaces[i].bpp);MUST(!surface_GetPixelFormat(s,&pf)&&pf.dwRGBBitCount==native_surfaces[i].bpp);}
static void native_marker_lifetimes(void){const unsigned views[]={1,2,3,4,7};for(unsigned i=0;i<5;i++)for(unsigned flip=0;flip<2;flip++){DDrawObject*o=owner(i==4);DDSurface s={.owner=o,.primary=TRUE,.ref=1,.front_of_chain=flip,.desc={.dwBackBufferCount=flip}};o->surfaces=o->primary=&s;BOOL pin=views[i]>=4;ddraw_child_attach(o,pin);test_primary_attach(o,&s,views[i]);MUST(o->platform_primary==&native_surface&&native_surface.version==views[i]&&native.refs==(unsigned)(pin?2:1));if(flip){MUST(!ddraw_SetCooperativeLevelForVersion(o,views[i],(void*)(uintptr_t)11,DDSCL_NORMAL));MUST(surface_IsLost(&s)==DDERR_SURFACELOST);MUST(surface_Restore(&s)==DDERR_WRONGMODE);}
 ddraw_platform_primary_detach(o);MUST(!native_surface.refs&&native.refs==1);o->primary=o->surfaces=NULL;MUST(ddraw_Release(o)==(unsigned)pin);ddraw_child_detach(o,pin);MUST(!native.refs);}
 DDrawObject*o=owner(FALSE);DDSurface s={.owner=o,.primary=TRUE,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_VIDEOMEMORY}}};o->surfaces=o->primary=&s;ddraw_child_attach(o,FALSE);test_primary_attach(o,&s,1);MUST(!ddraw_Release(o)&&!native.refs&&!native_surface.refs&&!o->platform_primary);o->surfaces=NULL;ddraw_child_detach(o,FALSE);o=owner(FALSE);s.owner=o;o->surfaces=o->primary=&s;ddraw_child_attach(o,FALSE);create_callback=release_public;test_primary_attach(o,&s,1);MUST(!o->ref&&o->lifetime_ref==1&&!o->platform_primary&&!native.refs&&!native_surface.refs);o->surfaces=NULL;ddraw_child_detach(o,FALSE);
 o=owner(FALSE);s.owner=o;o->surfaces=o->primary=&s;ddraw_child_attach(o,FALSE);test_primary_attach(o,&s,1);surface_callback=release_public;MUST(!surface_IsLost(&s)&&!o->ref&&!native.refs&&!native_surface.refs);o->surfaces=NULL;ddraw_child_detach(o,FALSE);
 o=owner(TRUE);s.owner=o;o->surfaces=o->primary=&s;fail_primary_create=TRUE;test_primary_attach(o,&s,7);MUST(!o->platform_primary&&native.refs==1&&!native_surface.refs);fail_primary_create=FALSE;o->surfaces=o->primary=NULL;release_public();}
static void proxy_exclusive_exit_allocations(void){
 const unsigned versions[]={1,2,3,4,7};
 for(unsigned v=0;v<5;v++)for(unsigned factory=0;factory<3;factory++)for(unsigned fullscreen=0;fullscreen<2;fullscreen++){
  native_hr=0;DDrawObject*o=owner(TRUE);o->factory_kind=factory;
  /* Frozen original lost_device: v1 preserves on NORMAL; v2/4/7 lose.
   * The v3 method is ABI-compatible with v2. The invoked view owns policy. */
  BOOL lost_on_exit=versions[v]!=1&&!fullscreen&&factory!=DDRAW_FACTORY_EMULATION;
  MUST(!ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));
  DDSurface video={.owner=o,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},system={.owner=o,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY}}},managed={.owner=o,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY,.dwCaps2=DDSCAPS2_TEXTUREMANAGE}}};
  o->surfaces=&video;video.next=&system;system.next=&managed;
  native_hr=DDERR_INVALIDPARAMS;MUST(ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)11,DDSCL_NORMAL)==DDERR_INVALIDPARAMS&&!video.cooperative_lost&&!system.cooperative_lost&&!managed.cooperative_lost&&(o->cooperative_flags&DDSCL_EXCLUSIVE));
  native_hr=0;MUST(!ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)11,DDSCL_NORMAL|(fullscreen?DDSCL_FULLSCREEN:0)));
  MUST(video.cooperative_lost==lost_on_exit&&!system.cooperative_lost&&!managed.cooperative_lost);
  MUST(surface_IsLost(&video)==(lost_on_exit?DDERR_SURFACELOST:DD_OK));
  if(fullscreen){MUST(!ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)11,DDSCL_NORMAL)&&!video.cooperative_lost);MUST(!ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)33,DDSCL_NORMAL|DDSCL_FULLSCREEN)&&!video.cooperative_lost);}
  MUST(!ddraw_SetCooperativeLevelForVersion(o,versions[v],(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)&&video.cooperative_lost&&!system.cooperative_lost&&!managed.cooperative_lost);
  o->surfaces=NULL;release_public();
 }
}
static void native_global_loss(void){DDrawObject*o=owner(TRUE),*peer=owner(TRUE);DDSurface a={.owner=o,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},b={.owner=peer,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},system={.owner=peer,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY}}},managed={.owner=peer,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY,.dwCaps2=DDSCAPS2_TEXTUREMANAGE}}};o->surfaces=&a;peer->surfaces=&b;b.next=&system;system.next=&managed;active=o;MUST(!ddraw_SetDisplayMode(o,640,480,16,0,0));MUST(a.cooperative_lost&&b.cooperative_lost&&!system.cooperative_lost&&!managed.cooperative_lost);int allocation;a.storage=b.storage=&allocation;a.next=&b;o->surfaces=&a;b.owner=o;MUST(!surface_Restore(&a)&&!a.cooperative_lost&&!b.cooperative_lost);o->surfaces=peer->surfaces=NULL;active=peer;release_public();active=o;release_public();current_width=800;current_height=600;current_bpp=32;}
static void proxy_allocation_mode(void){native_hr=0;DDrawObject*o=owner(TRUE);MUST(!ddraw_SetCooperativeLevel(o,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));DDSurface s={.ref=1,.owner=o,.primary=TRUE,.width=800,.height=600,.desc={.dwSize=sizeof(DDSURFACEDESC2),.dwWidth=800,.dwHeight=600,.lPitch=3200,.ddpfPixelFormat={.dwRGBBitCount=32},.ddsCaps={.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_VIDEOMEMORY}}};o->surfaces=o->primary=&s;ddraw_child_attach(o,TRUE);test_primary_attach(o,&s,7);native_surface.width=current_width=640;native_surface.height=current_height=480;native_surface.lost=TRUE;MUST(surface_Restore(&s)==DDERR_WRONGMODE&&s.cooperative_lost&&native_surface.lost);current_width=800;current_height=600;ddraw_platform_primary_detach(o);o->primary=o->surfaces=NULL;MUST(ddraw_Release(o)==1);ddraw_child_detach(o,TRUE);}
static void native_primary_contract(void){native_hr=0;DDrawObject*o=owner(TRUE);MUST(!ddraw_SetCooperativeLevel(o,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));DDSurface s={.ref=1,.owner=o,.primary=TRUE,.width=800,.height=600,.desc={.dwSize=sizeof(DDSURFACEDESC2),.dwWidth=800,.dwHeight=600,.lPitch=3200,.ddpfPixelFormat={.dwRGBBitCount=32},.ddsCaps={.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_VIDEOMEMORY}}};o->surfaces=o->primary=&s;ddraw_child_attach(o,TRUE);test_primary_attach(o,&s,7);MUST(o->platform_primary==&native_surface&&native_surface.refs==1&&native.refs==2);check_primary(o,&s,"initial");MUST(!ddraw_SetDisplayMode(o,640,480,16,0,0));check_primary(o,&s,"after_640");MUST(surface_Restore(&s)==DDERR_WRONGMODE);check_primary(o,&s,"after_wrong_restore");MUST(!ddraw_SetDisplayMode(o,800,600,32,0,0));check_primary(o,&s,"after_800");MUST(!surface_Restore(&s));check_primary(o,&s,"restored_matching");MUST(!ddraw_SetDisplayMode(o,800,600,32,0,0));check_primary(o,&s,"after_duplicate");force_no_exclusive=TRUE;native_surface.lost=TRUE;check_primary(o,&s,"focus_away");MUST(surface_Restore(&s)==DDERR_WRONGMODE);force_no_exclusive=FALSE;check_primary(o,&s,"focus_back");MUST(!surface_Restore(&s));check_primary(o,&s,"restored_focus");MUST(!ddraw_SetCooperativeLevel(o,(void*)(uintptr_t)11,DDSCL_NORMAL));check_primary(o,&s,"after_normal");MUST(!ddraw_SetCooperativeLevel(o,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));check_primary(o,&s,"after_exclusive");MUST(!surface_Restore(&s));check_primary(o,&s,"restored_exclusive");ddraw_platform_primary_detach(o);o->primary=o->surfaces=NULL;MUST(ddraw_Release(o)==1);ddraw_child_detach(o,TRUE);}
`;
const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef size_t SIZE_T;typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HWND,*HMODULE,*HDC;typedef struct{unsigned version;}GUID;typedef const GUID*REFIID;
typedef struct Native{unsigned refs,version,width,height,bpp;BOOL surface,lost,flip;}IUnknown,IDirectDraw,IDirectDraw2,IDirectDraw3,IDirectDraw4,IDirectDrawSurface,IDirectDrawSurface4;
typedef struct{DWORD dwSize,dwFlags,dwRGBBitCount;}DDPIXELFORMAT,*LPDDPIXELFORMAT;
typedef struct{DWORD dwSize,dwFlags,dwWidth,dwHeight;LONG lPitch;DWORD dwBackBufferCount;void*lpSurface;DDPIXELFORMAT ddpfPixelFormat;struct{DWORD dwCaps,dwCaps2;}ddsCaps;}DDSURFACEDESC;
typedef struct{DWORD dwSize,dwFlags,dwWidth,dwHeight;LONG lPitch;DWORD dwBackBufferCount;void*lpSurface;DDPIXELFORMAT ddpfPixelFormat;struct{DWORD dwCaps,dwCaps2;}ddsCaps;DWORD extended[4];}DDSURFACEDESC2,*LPDDSURFACEDESC2;
typedef struct DDSurface{struct DDSurface*next,*flip_next;DDSURFACEDESC2 desc;BOOL cooperative_lost,primary,front_of_chain;struct DDrawObject*owner;void*storage;UINT width,height;LONG ref;}DDSurface,IDirectDrawSurface7;
typedef struct DDrawObject{const void*vtbl7;DDSurface*surfaces,*primary;IUnknown*platform_ddraw,*platform_primary;BOOL platform_primary_lost,platform_exclusive_lost;struct DDrawObject*next;unsigned factory_kind;BOOL initialized,extended_factory;LONG ref,lifetime_ref,control_serial;HWND window;DWORD cooperative_flags;UINT mode_width,mode_height,mode_bpp,mode_refresh;BOOL mode_set,guest_mode_changed;}DDrawObject,IDirectDraw7;
typedef struct{DWORD dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel,dmFields,dmDisplayFrequency;}DEVMODEA;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define DD_VERSION_1 1
#define DD_VERSION_2 2
#define DD_VERSION_3 3
#define DD_VERSION_4 4
#define DD_VERSION_7 7
#define DD_OK 0
#define DDERR_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define E_NOINTERFACE ((HRESULT)0x80004002u)
#define E_FAIL ((HRESULT)0x80004005u)
#define E_NOTIMPL ((HRESULT)0x80004001u)
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057u)
#define DDERR_INVALIDMODE ((HRESULT)0x88760078u)
#define DDERR_UNSUPPORTEDMODE ((HRESULT)0x8876024eu)
#define DDSCAPS_PRIMARYSURFACE 0x200u
#define DDSD_BACKBUFFERCOUNT 0x20u
#define DDSCAPS_COMPLEX 0x8u
#define DDSCAPS_FLIP 0x10u
#define DDSD_CAPS 1u
#define DDSD_LPSURFACE 0x800u
#define DDERR_WRONGMODE ((HRESULT)0x8876024bu)
#define DDERR_NOEXCLUSIVEMODE ((HRESULT)0x887600e1u)
#define DDSCAPS_VIDEOMEMORY 0x4000u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS2_TEXTUREMANAGE 0x10u
#define DDSCAPS2_D3DTEXTUREMANAGE 0x20000u
#define DDERR_SURFACELOST ((HRESULT)0x887601c2u)
#define DDSCL_FULLSCREEN 1u
#define DDSCL_EXCLUSIVE 16u
#define DDSCL_NORMAL 8u
#define DDRAW_FACTORY_EMULATION 2u
#define DM_PELSWIDTH 1u
#define DM_PELSHEIGHT 2u
#define DM_BITSPERPEL 4u
#define DM_DISPLAYFREQUENCY 8u
#define CDS_FULLSCREEN 4u
#define DISP_CHANGE_SUCCESSFUL 0
#define ZeroMemory(p,n) memset(p,0,n)
#define FAILED(h) ((HRESULT)(h)<0)
#define SUCCEEDED(h) ((HRESULT)(h)>=0)
#define DDWG_TRACE_ERROR(h) (h)
#define DDWG_TRACE_RESULT(h) (h)
static GUID IID_IDirectDraw={1},IID_IDirectDraw2={2},IID_IDirectDraw3={3},IID_IDirectDraw4={4},IID_IDirectDraw7={7};
static int g_object_lock;static HMODULE g_instance=(void*)(uintptr_t)1;static struct Native native,native_surface;static DDrawObject*g_objects;static BOOL force_no_exclusive;static DWORD native_coop_flags;static unsigned primary_creates,primary_destroys;static void(*surface_callback)(void),(*create_callback)(void);static BOOL fail_primary_create;static unsigned checks,hides,loads,frees,creates,ex_creates,queries,native_releases,emits,cds_calls,deaths,control_calls;static HRESULT native_hr,query_hr,create_hr;static unsigned actual_version,fallback_queries;static BOOL native_no_draw3,missing_module,self_module,missing_create;static DDrawObject*active;static void(*callback)(void);static DWORD current_width=800,current_height=600,current_bpp=32;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"platform line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static LONG InterlockedDecrement(LONG*p){return --*p;}
static void EnterCriticalSection(int*p){MUST(*p>=0);++*p;}static void LeaveCriticalSection(int*p){MUST(*p>0);--*p;}
static UINT GetSystemDirectoryA(char*p,UINT n){MUST(n==MAX_PATH);strcpy(p,"C:\\WINDOWS\\system32");return strlen(p);}static void lstrcatA(char*p,const char*s){strcat(p,s);}
static HMODULE LoadLibraryA(const char*p){MUST(!strcmp(p,"C:\\WINDOWS\\system32\\ddraw.dll"));loads++;return missing_module?NULL:self_module?g_instance:(void*)(uintptr_t)2;}static void FreeLibrary(HMODULE m){MUST(m==g_instance);frees++;}
static ULONG IUnknown_AddRef(IUnknown*p){MUST((p==&native||p==&native_surface)&&p->refs);return ++p->refs;}
static ULONG IUnknown_Release(IUnknown*p){MUST(!g_object_lock&&(p==&native||p==&native_surface)&&p->refs);native_releases++;ULONG n=--p->refs;if(!n&&p==&native_surface){primary_destroys++;if(p->version>=4)IUnknown_Release(&native);}if(!n&&p==&native&&native_surface.refs&&native_surface.version<4)native_surface.refs=0;return n;}
static HRESULT IUnknown_QueryInterface(IUnknown*p,REFIID iid,void**out){MUST(!g_object_lock&&p==&native);queries++;if(native_no_draw3&&iid->version==3){*out=NULL;return E_NOINTERFACE;}if(FAILED(query_hr)){*out=NULL;return query_hr;}if(native_no_draw3&&iid->version==2)fallback_queries++;native.version=iid->version;native.refs++;*out=p;return 0;}
static HRESULT create_old(GUID*g,IDirectDraw**out,IUnknown*outer){MUST(!g_object_lock&&!g&&!outer);creates++;if(FAILED(create_hr))return create_hr;if(!native.refs)native_coop_flags=0;native.refs++;*out=&native;return 0;}
static HRESULT create_ex(GUID*g,void**out,REFIID iid,IUnknown*outer){MUST(iid==&IID_IDirectDraw7);ex_creates++;return create_old(g,(IDirectDraw**)out,outer);}
static void*GetProcAddress(HMODULE m,const char*n){MUST(m==(void*)(uintptr_t)2);if(missing_create)return NULL;return !strcmp(n,"DirectDrawCreateEx")?(void*)create_ex:(void*)create_old;}
static HRESULT cooperative(IUnknown*p,HWND w,DWORD f,unsigned v){MUST(!g_object_lock&&p==&native&&(p->version==v||(v==3&&p->version==2))&&active->lifetime_ref>=2&&active->window==w&&active->cooperative_flags==f);control_calls++;actual_version=v;if(callback){void(*cb)(void)=callback;callback=NULL;cb();}if(SUCCEEDED(native_hr)&&f!=native_coop_flags&&native_surface.refs)native_surface.lost=TRUE;if(SUCCEEDED(native_hr))native_coop_flags=f;if(SUCCEEDED(native_hr)&&!(f&DDSCL_EXCLUSIVE)){current_width=800;current_height=600;current_bpp=32;}return native_hr;}
static HRESULT mode(IUnknown*p,DWORD w,DWORD h,DWORD b,DWORD r,DWORD f,unsigned v){MUST(!g_object_lock&&p==&native&&(p->version==v||(v==3&&p->version==2))&&active->lifetime_ref>=2&&active->mode_width==w&&active->mode_height==h&&active->mode_bpp==b&&active->mode_refresh==r);MUST(!f);control_calls++;actual_version=v;if(callback){void(*cb)(void)=callback;callback=NULL;cb();}if(SUCCEEDED(native_hr)&&native_surface.refs)native_surface.lost=TRUE;if(SUCCEEDED(native_hr)){current_width=w;current_height=h;current_bpp=b;}return native_hr;}
static HRESULT restore(IUnknown*p,unsigned v){MUST(!g_object_lock&&p==&native&&(p->version==v||(v==3&&p->version==2))&&active->lifetime_ref>=2);control_calls++;actual_version=v;if(SUCCEEDED(native_hr)){current_width=800;current_height=600;current_bpp=32;}return native_hr;}
static HRESULT current(void*p,DDSURFACEDESC2*d){MUST(!g_object_lock&&p==&native&&native.version==7&&d->dwSize==sizeof(*d));d->dwWidth=current_width;d->dwHeight=current_height;d->ddpfPixelFormat.dwRGBBitCount=current_bpp;return native_hr;}
#define IDirectDraw_SetCooperativeLevel(p,w,f) cooperative((IUnknown*)p,w,f,1)
#define IDirectDraw2_SetCooperativeLevel(p,w,f) cooperative((IUnknown*)p,w,f,2)
#define IDirectDraw3_SetCooperativeLevel(p,w,f) cooperative((IUnknown*)p,w,f,3)
#define IDirectDraw4_SetCooperativeLevel(p,w,f) cooperative((IUnknown*)p,w,f,4)
#define IDirectDraw7_SetCooperativeLevel(p,w,f) cooperative((IUnknown*)p,w,f,7)
#define IDirectDraw_SetDisplayMode(p,w,h,b) mode((IUnknown*)p,w,h,b,0,0,1)
#define IDirectDraw2_SetDisplayMode(p,w,h,b,r,f) mode((IUnknown*)p,w,h,b,r,f,2)
#define IDirectDraw3_SetDisplayMode(p,w,h,b,r,f) mode((IUnknown*)p,w,h,b,r,f,3)
#define IDirectDraw4_SetDisplayMode(p,w,h,b,r,f) mode((IUnknown*)p,w,h,b,r,f,4)
#define IDirectDraw7_SetDisplayMode(p,w,h,b,r,f) mode((IUnknown*)p,w,h,b,r,f,7)
#define IDirectDraw_RestoreDisplayMode(p) restore((IUnknown*)p,1)
#define IDirectDraw2_RestoreDisplayMode(p) restore((IUnknown*)p,2)
#define IDirectDraw3_RestoreDisplayMode(p) restore((IUnknown*)p,3)
#define IDirectDraw4_RestoreDisplayMode(p) restore((IUnknown*)p,4)
#define IDirectDraw7_RestoreDisplayMode(p) restore((IUnknown*)p,7)
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
static BOOL desktop_fail;static unsigned desktop_dcs,current_planes=1;
static HDC GetDC(HWND w){MUST(!w&&!g_object_lock);if(desktop_fail)return NULL;MUST(!desktop_dcs++);return(void*)(uintptr_t)0x101;}
static int GetDeviceCaps(HDC dc,int field){MUST(dc==(void*)(uintptr_t)0x101&&desktop_dcs==1);if(field==HORZRES)return current_width;if(field==VERTRES)return current_height;if(field==BITSPIXEL)return current_bpp;MUST(field==PLANES);return current_planes;}
static int ReleaseDC(HWND w,HDC dc){MUST(!w&&dc==(void*)(uintptr_t)0x101&&desktop_dcs==1);desktop_dcs--;return 1;}
#define IDirectDraw7_GetDisplayMode(p,d) current(p,d)
#define IDirectDraw7_TestCooperativeLevel(p) test_coop(p)
static HRESULT test_coop(void*p){MUST(p==&native);return force_no_exclusive?DDERR_NOEXCLUSIVEMODE:native_hr;}
static IUnknown*ddraw_platform_primary_take_locked(DDrawObject*);
static void*heap_alloc_zero(size_t n){return calloc(1,n);}static void heap_free(void*p){free(p);}
static ULONG surface_private_addref(DDSurface*s){MUST(s->ref>=0);return ++s->ref;}static ULONG surface_private_release(DDSurface*s){MUST(s->ref>0);return --s->ref;}
static void mode_pixel_format(UINT b,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){sizeof(*p),0,b};}
static HRESULT create_primary(void*p,void*desc,void**out,void*outer,unsigned v){MUST(!g_object_lock&&p==&native&&!outer&&!native_surface.refs);if(v<4){DDSURFACEDESC*d=desc;MUST(d->dwSize==sizeof(*d)&&d->dwFlags==(DDSD_CAPS|(active->primary->front_of_chain?DDSD_BACKBUFFERCOUNT:0))&&d->ddsCaps.dwCaps==(DDSCAPS_PRIMARYSURFACE|(active->primary->front_of_chain?(DDSCAPS_COMPLEX|DDSCAPS_FLIP):0))&&d->dwBackBufferCount==active->primary->desc.dwBackBufferCount);}else{DDSURFACEDESC2*d=desc;MUST(d->dwSize==sizeof(*d)&&d->dwFlags==(DDSD_CAPS|(active->primary->front_of_chain?DDSD_BACKBUFFERCOUNT:0))&&d->ddsCaps.dwCaps==(DDSCAPS_PRIMARYSURFACE|(active->primary->front_of_chain?(DDSCAPS_COMPLEX|DDSCAPS_FLIP):0))&&d->dwBackBufferCount==active->primary->desc.dwBackBufferCount);}if(fail_primary_create)return E_FAIL;native_surface=(IUnknown){.refs=1,.version=v,.surface=TRUE,.width=current_width,.height=current_height,.bpp=current_bpp,.flip=active->primary->front_of_chain};if(v>=4)IUnknown_AddRef(&native);primary_creates++;*out=&native_surface;if(create_callback){void(*cb)(void)=create_callback;create_callback=NULL;cb();}return 0;}
static HRESULT primary_lost(void*p){MUST(!g_object_lock&&p==&native_surface&&native_surface.refs&&native.refs);if(surface_callback){void(*cb)(void)=surface_callback;surface_callback=NULL;cb();}MUST(native_surface.refs&&native.refs);return native_surface.lost?DDERR_SURFACELOST:0;}
static HRESULT primary_restore(void*p){MUST(!g_object_lock&&p==&native_surface&&native_surface.refs&&native.refs);if(force_no_exclusive||current_width!=native_surface.width||current_height!=native_surface.height||current_bpp!=native_surface.bpp)return DDERR_WRONGMODE;native_surface.lost=FALSE;return 0;}
static HRESULT primary_desc(void*p,void*desc,BOOL extended){MUST(!g_object_lock&&p==&native_surface&&native_surface.refs&&native.refs);if(extended){DDSURFACEDESC2*d=desc;MUST(d->dwSize==sizeof(*d));d->dwWidth=native_surface.width;d->dwHeight=native_surface.height;d->ddpfPixelFormat.dwRGBBitCount=native_surface.bpp;}else{DDSURFACEDESC*d=desc;MUST(d->dwSize==sizeof(*d));d->dwWidth=native_surface.width;d->dwHeight=native_surface.height;d->ddpfPixelFormat.dwRGBBitCount=native_surface.bpp;}return 0;}
#define IDirectDrawSurface_GetSurfaceDesc(p,d) primary_desc(p,d,FALSE)
#define IDirectDrawSurface7_GetSurfaceDesc(p,d) primary_desc(p,d,TRUE)
#define IDirectDraw_CreateSurface(p,d,s,o) create_primary(p,d,(void**)s,o,1)
#define IDirectDraw2_CreateSurface(p,d,s,o) create_primary(p,d,(void**)s,o,2)
#define IDirectDraw3_CreateSurface(p,d,s,o) create_primary(p,d,(void**)s,o,3)
#define IDirectDraw4_CreateSurface(p,d,s,o) create_primary(p,d,(void**)s,o,4)
#define IDirectDraw7_CreateSurface(p,d,s,o) create_primary(p,d,(void**)s,o,7)
#define IDirectDrawSurface_IsLost(p) primary_lost(p)
#define IDirectDrawSurface_Restore(p) primary_restore(p)
static BOOL ddraw_platform_release(DDrawObject*);
static void ddraw_lifetime_release(DDrawObject*o){MUST(o->lifetime_ref>0);if(!--o->lifetime_ref){MUST(!o->ref);ddraw_platform_release(o);DDrawObject**link=&g_objects;while(*link&&*link!=o)link=&(*link)->next;if(*link)*link=o->next;deaths++;free(o);}}
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}
static DDrawObject*object_from_any(IDirectDraw7*p,void*v){(void)v;return p;}
static void emit_window_state_not_showing(DDrawObject*o){MUST(o->lifetime_ref>0&&!o->primary&&!o->guest_mode_changed);hides++;}
static void emit_display_mode(DDrawObject*o){MUST(o->lifetime_ref>0);emits++;}
static void object_display_size(DDrawObject*o,UINT*w,UINT*h){(void)o;*w=current_width;*h=current_height;}
static LONG ChangeDisplaySettingsA(DEVMODEA*m,DWORD f){cds_calls++;MUST(!m?!f:(f==CDS_FULLSCREEN&&m->dmSize==sizeof(*m)));return 0;}
PLATFORM
SURFACE_CONTROL
METHODS
static DDrawObject*owner(BOOL ex){DDrawObject*o=calloc(1,sizeof(*o));MUST(o);o->ref=o->lifetime_ref=1;o->initialized=TRUE;o->extended_factory=ex;active=o;o->next=g_objects;g_objects=o;ddraw_platform_attach(o);return o;}
static void release_public(void){MUST(active->ref>0);ddraw_Release(active);}
static void nested_mode(void){MUST(!ddraw_SetDisplayModeForVersion(active,4,1024,768,32,0,0));}
static void nested_coop(void){MUST(!ddraw_SetCooperativeLevelForVersion(active,2,(void*)(uintptr_t)22,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));}
static void test_primary_attach(DDrawObject*o,DDSurface*s,unsigned v){DDPlatformPrimary p;ddraw_platform_primary_prepare(o,v,s->front_of_chain,s->desc.dwBackBufferCount,&p);MUST(p.metadata==!fail_primary_create);if(p.metadata)MUST(p.desc.dwWidth==native_surface.width&&p.desc.dwHeight==native_surface.height&&p.desc.ddpfPixelFormat.dwRGBBitCount==native_surface.bpp);ddraw_platform_primary_attach(o,s,&p);MUST(!p.surface&&!p.view&&!p.metadata);}
NEW_TESTS
int main(void){(void)&ddraw_platform_load_mode;(void)&ddraw_platform_lose_all_video_surfaces;(void)&ddraw_platform_restore_surface;const unsigned versions[]={1,2,3,4,7};for(unsigned repeat=0;repeat<16;repeat++)for(unsigned i=0;i<5;i++){
 unsigned v=versions[i];native_hr=query_hr=create_hr=0;DDrawObject*o=owner(i&1);MUST(o->platform_ddraw==&native&&native.refs==1&&o->lifetime_ref==1);MUST(!ddraw_TestCooperativeLevel(o));native_hr=E_FAIL;MUST(ddraw_TestCooperativeLevel(o)==E_FAIL);native_hr=0;unsigned c=creates;ddraw_platform_attach(o);MUST(creates==c);MUST(!ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)&&actual_version==v&&o->lifetime_ref==1&&native.refs==1);
 MUST(!ddraw_SetDisplayModeForVersion(o,v,640,480,16,0,0)&&actual_version==v&&o->guest_mode_changed&&o->mode_set);DDSURFACEDESC2 d;HRESULT hr;MUST(ddraw_platform_mode(o,&d,&hr)&&!hr&&d.dwWidth==640&&d.dwHeight==480&&d.ddpfPixelFormat.dwRGBBitCount==16);current_width=800;current_height=600;current_bpp=32;ddraw_platform_sync(o);MUST(o->mode_width==640&&o->mode_height==480&&o->mode_bpp==16&&o->mode_set);current_width=640;current_height=480;current_bpp=16;UINT desktop_w=0,desktop_h=0,desktop_bpp=0;MUST(ddraw_platform_desktop_size(&desktop_w,&desktop_h,&desktop_bpp)&&desktop_w==640&&desktop_h==480&&desktop_bpp==16);desktop_fail=TRUE;desktop_w=0xdeadbeef;MUST(!ddraw_platform_desktop_size(&desktop_w,&desktop_h,&desktop_bpp)&&desktop_w==0xdeadbeef);desktop_fail=FALSE;
 DDPlatformModeState before=ddraw_platform_save_mode(o);native_hr=DDERR_INVALIDPARAMS;MUST(ddraw_SetDisplayModeForVersion(o,v,123,456,24,0,0)==native_hr);DDPlatformModeState after=ddraw_platform_save_mode(o);MUST(!memcmp(&before,&after,sizeof(before)));MUST(ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)33,DDSCL_NORMAL)==native_hr&&o->window==(void*)(uintptr_t)11&&o->cooperative_flags==(DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));native_hr=0;
 MUST(!ddraw_RestoreDisplayModeForVersion(o,v)&&actual_version==v&&!o->mode_set&&!o->guest_mode_changed&&!o->mode_refresh&&o->mode_width==800&&o->mode_height==600&&o->mode_bpp==32);
 native_hr=E_NOTIMPL;MUST(!ddraw_SetDisplayModeForVersion(o,v,320,240,32,0,0)&&o->mode_set&&!o->guest_mode_changed);unsigned q=queries;MUST(!ddraw_platform_mode(o,&d,&hr)&&queries==q);native_hr=0;
 MUST(!ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)11,DDSCL_NORMAL)&&!o->mode_set&&o->mode_width==800);query_hr=E_FAIL;MUST(ddraw_RestoreDisplayModeForVersion(o,v)==E_FAIL&&native.refs==1);query_hr=0;
 unsigned emitted=emits;callback=nested_mode;MUST(!ddraw_SetDisplayModeForVersion(o,v,640,480,16,0,0)&&o->mode_width==1024&&o->mode_height==768&&o->mode_bpp==32&&emits==emitted+1);emitted=emits;callback=nested_coop;MUST(!ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)11,DDSCL_NORMAL)&&o->window==(void*)(uintptr_t)11&&o->cooperative_flags==DDSCL_NORMAL&&!o->mode_set&&emits==emitted+2);
 unsigned died=deaths;callback=release_public;MUST(!ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)11,DDSCL_NORMAL)&&deaths==died+1&&!native.refs);
 }
 MUST(loads==1&&ex_creates==32&&creates==80&&!frees&&!cds_calls);native_hr=0;DDrawObject*o=owner(FALSE);MUST(ddraw_SetCooperativeLevel(o,(void*)(uintptr_t)11,DDSCL_NORMAL)==0&&actual_version==7);MUST(ddraw_SetDisplayMode(o,640,480,32,0,0)==0&&actual_version==7);MUST(ddraw_RestoreDisplayMode(o)==0&&actual_version==7);release_public();
 g_platform_ddraw_tried=FALSE;g_platform_ddraw=NULL;missing_module=TRUE;o=owner(FALSE);MUST(!o->platform_ddraw&&!ddraw_TestCooperativeLevel(o));MUST(ddraw_SetCooperativeLevelForVersion(o,7,NULL,DDSCL_EXCLUSIVE)==DDERR_INVALIDPARAMS&&!o->cooperative_flags);MUST(!ddraw_SetCooperativeLevelForVersion(o,7,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));MUST(!ddraw_SetDisplayModeForVersion(o,7,640,480,32,75,0)&&o->guest_mode_changed&&o->mode_refresh==75&&cds_calls==1);MUST(!ddraw_RestoreDisplayModeForVersion(o,7)&&cds_calls==2);release_public();missing_module=FALSE;
 g_platform_ddraw_tried=FALSE;self_module=TRUE;o=owner(FALSE);MUST(!o->platform_ddraw&&frees==1);release_public();self_module=FALSE;
 g_platform_ddraw_tried=FALSE;create_hr=E_FAIL;o=owner(FALSE);MUST(!o->platform_ddraw&&native.refs==0);release_public();create_hr=0;missing_create=TRUE;o=owner(TRUE);MUST(!o->platform_ddraw);release_public();missing_create=FALSE;
 DDSurface video={.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},system={.desc={.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY}}},managed={.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY,.dwCaps2=DDSCAPS2_TEXTUREMANAGE}}};video.next=&system;system.next=&managed;DDrawObject local={.surfaces=&video};video.owner=system.owner=managed.owner=&local;ddraw_platform_lose_video_surfaces(&local);MUST(surface_IsLost(&video)==DDERR_SURFACELOST&&!surface_IsLost(&system)&&!surface_IsLost(&managed)&&!system.cooperative_lost&&!managed.cooperative_lost);DDSurface back={.owner=&local,.cooperative_lost=TRUE,.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}};video.flip_next=&back;back.flip_next=&video;MUST(!surface_Restore(&video)&&!surface_IsLost(&video)&&!surface_IsLost(&back));ddraw_platform_lose_video_surfaces(&local);MUST(!ddraw_RestoreAllSurfaces(&local)&&!surface_IsLost(&video));
 o=owner(FALSE);o->lifetime_ref++;o->guest_mode_changed=TRUE;o->primary=&video;unsigned hidden=hides;MUST(!ddraw_Release(o)&&o->lifetime_ref==1&&!o->platform_ddraw&&!o->primary&&!o->guest_mode_changed&&!native.refs&&hides==hidden+1);ddraw_lifetime_release(o);
 const struct{unsigned version,refs;}native_lifetimes[]={NATIVE_LIFETIMES};for(unsigned i=0;i<4;i++){o=owner(native_lifetimes[i].version==7);BOOL pin=native_lifetimes[i].version>=4;ddraw_child_attach(o,pin);MUST(ddraw_Release(o)==native_lifetimes[i].refs);MUST(!!o->platform_ddraw==!!native_lifetimes[i].refs&&native.refs==native_lifetimes[i].refs);unsigned died=deaths;ddraw_child_detach(o,pin);MUST(deaths==died+1&&!native.refs);}
 MUST(!desktop_dcs&&!g_object_lock&&!native.refs&&deaths==90);DDrawObject*fallback=owner(FALSE);native_no_draw3=TRUE;MUST(!ddraw_SetCooperativeLevelForVersion(fallback,3,(void*)(uintptr_t)11,DDSCL_NORMAL)&&fallback_queries==1);MUST(!ddraw_SetDisplayModeForVersion(fallback,3,640,480,32,0,0)&&fallback_queries==2);MUST(!ddraw_RestoreDisplayModeForVersion(fallback,3)&&native.version==7);native_no_draw3=FALSE;release_public();proxy_allocation_mode();native_primary_contract();native_marker_lifetimes();native_global_loss();proxy_exclusive_exit_allocations();MUST(!native.refs&&!native_surface.refs&&!g_objects&&primary_creates==primary_destroys);printf("PASS actual native primary-control C: %u checks; all five interfaces, physical/virtual modes, native validation rollback, nested callbacks, last-public-release lifetime, absolute native load, self-load and factory failures\n",checks);return 0;}
`.replace('NATIVE_LIFETIMES',native.releases.map(r=>'{'+[r.version,r.refs].join(',')+'}').join(',')).replace('NEW_TESTS',newTests).replace('NATIVE_SURFACES',primaryNative.surfaces.map(r=>'{\"'+r.name+'\",'+[r.desc,r.lost,r.cooperative,r.width,r.height,r.bpp,r.pitch].join(',')+'}').join(',')).replace('PLATFORM',platform).replace('SURFACE_CONTROL',surfaceControl).replace('METHODS',['ddraw_AddRef','ddraw_Release','ddraw_child_attach','ddraw_child_detach','surface_IsLost','surface_Restore','surface_GetSurfaceDesc','surface_GetPixelFormat','ddraw_RestoreAllSurfaces','ddraw_TestCooperativeLevel','ddraw_RestoreDisplayModeForVersion','ddraw_RestoreDisplayMode','ddraw_SetCooperativeLevelForVersion','ddraw_SetCooperativeLevel','ddraw_SetDisplayModeForVersion','ddraw_SetDisplayMode'].map(extract).join('\n'));
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
