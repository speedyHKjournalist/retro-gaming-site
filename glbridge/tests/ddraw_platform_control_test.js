"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_PLATFORM_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),platform=fs.readFileSync(process.env.DDRAW_PLATFORM_INCLUDE||root+'/ddrawproxy/ddraw_platform_control.inc','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-platform-'));
const native=JSON.parse(fs.readFileSync(path.resolve(root,'../../v86/tests/glbridge/fixtures/ddraw_native_control_lifetime.json')));assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(root+'/../'+native.source)).digest('hex'),native.sourceSha256);
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HWND,*HMODULE;typedef struct{unsigned version;}GUID;typedef const GUID*REFIID;
typedef struct Native{unsigned refs,version;}IUnknown,IDirectDraw,IDirectDraw2,IDirectDraw3,IDirectDraw4;
typedef struct DDSurface{struct DDSurface*next,*flip_next;struct{struct{DWORD dwCaps,dwCaps2;}ddsCaps;}desc;BOOL cooperative_lost;}DDSurface,IDirectDrawSurface7;
typedef struct DDrawObject{const void*vtbl7;DDSurface*surfaces,*primary;IUnknown*platform_ddraw;BOOL initialized,extended_factory;LONG ref,lifetime_ref,control_serial;HWND window;DWORD cooperative_flags;UINT mode_width,mode_height,mode_bpp,mode_refresh;BOOL mode_set,guest_mode_changed;}DDrawObject,IDirectDraw7;
typedef struct{DWORD dwSize,dwWidth,dwHeight;struct{DWORD dwRGBBitCount;}ddpfPixelFormat;}DDSURFACEDESC2;
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
#define E_FAIL ((HRESULT)0x80004005u)
#define E_NOTIMPL ((HRESULT)0x80004001u)
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057u)
#define DDERR_INVALIDMODE ((HRESULT)0x88760078u)
#define DDERR_UNSUPPORTEDMODE ((HRESULT)0x8876024eu)
#define DDSCAPS_VIDEOMEMORY 0x4000u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS2_TEXTUREMANAGE 0x10u
#define DDSCAPS2_D3DTEXTUREMANAGE 0x20000u
#define DDERR_SURFACELOST ((HRESULT)0x887601c2u)
#define DDSCL_FULLSCREEN 1u
#define DDSCL_EXCLUSIVE 16u
#define DDSCL_NORMAL 8u
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
static int g_object_lock;static HMODULE g_instance=(void*)(uintptr_t)1;static struct Native native;static unsigned checks,hides,loads,frees,creates,ex_creates,queries,native_releases,emits,cds_calls,deaths,control_calls;static HRESULT native_hr,query_hr,create_hr;static unsigned actual_version;static BOOL missing_module,self_module,missing_create;static DDrawObject*active;static void(*callback)(void);static DWORD current_width=800,current_height=600,current_bpp=32;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"platform line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static LONG InterlockedDecrement(LONG*p){return --*p;}
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static UINT GetSystemDirectoryA(char*p,UINT n){MUST(n==MAX_PATH);strcpy(p,"C:\\WINDOWS\\system32");return strlen(p);}static void lstrcatA(char*p,const char*s){strcat(p,s);}
static HMODULE LoadLibraryA(const char*p){MUST(!strcmp(p,"C:\\WINDOWS\\system32\\ddraw.dll"));loads++;return missing_module?NULL:self_module?g_instance:(void*)(uintptr_t)2;}static void FreeLibrary(HMODULE m){MUST(m==g_instance);frees++;}
static ULONG IUnknown_Release(IUnknown*p){MUST(!g_object_lock&&p==&native&&p->refs);native_releases++;return --p->refs;}
static HRESULT IUnknown_QueryInterface(IUnknown*p,REFIID iid,void**out){MUST(!g_object_lock&&p==&native);queries++;if(FAILED(query_hr)){*out=NULL;return query_hr;}native.version=iid->version;native.refs++;*out=p;return 0;}
static HRESULT create_old(GUID*g,IDirectDraw**out,IUnknown*outer){MUST(!g_object_lock&&!g&&!outer);creates++;if(FAILED(create_hr))return create_hr;native.refs++;*out=&native;return 0;}
static HRESULT create_ex(GUID*g,void**out,REFIID iid,IUnknown*outer){MUST(iid==&IID_IDirectDraw7);ex_creates++;return create_old(g,(IDirectDraw**)out,outer);}
static void*GetProcAddress(HMODULE m,const char*n){MUST(m==(void*)(uintptr_t)2);if(missing_create)return NULL;return !strcmp(n,"DirectDrawCreateEx")?(void*)create_ex:(void*)create_old;}
static HRESULT cooperative(IUnknown*p,HWND w,DWORD f,unsigned v){MUST(!g_object_lock&&p==&native&&p->version==v&&active->lifetime_ref>=2&&active->window==w&&active->cooperative_flags==f);control_calls++;actual_version=v;if(callback){void(*cb)(void)=callback;callback=NULL;cb();}if(SUCCEEDED(native_hr)&&!(f&DDSCL_EXCLUSIVE)){current_width=800;current_height=600;current_bpp=32;}return native_hr;}
static HRESULT mode(IUnknown*p,DWORD w,DWORD h,DWORD b,DWORD r,DWORD f,unsigned v){MUST(!g_object_lock&&p==&native&&p->version==v&&active->lifetime_ref>=2&&active->mode_width==w&&active->mode_height==h&&active->mode_bpp==b&&active->mode_refresh==r);MUST(!f);control_calls++;actual_version=v;if(callback){void(*cb)(void)=callback;callback=NULL;cb();}if(SUCCEEDED(native_hr)){current_width=w;current_height=h;current_bpp=b;}return native_hr;}
static HRESULT restore(IUnknown*p,unsigned v){MUST(!g_object_lock&&p==&native&&p->version==v&&active->lifetime_ref>=2);control_calls++;actual_version=v;if(SUCCEEDED(native_hr)){current_width=800;current_height=600;current_bpp=32;}return native_hr;}
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
#define IDirectDraw7_GetDisplayMode(p,d) current(p,d)
#define IDirectDraw7_TestCooperativeLevel(p) test_coop(p)
static HRESULT test_coop(void*p){MUST(p==&native);return native_hr;}
static BOOL ddraw_platform_release(DDrawObject*);
static void ddraw_lifetime_release(DDrawObject*o){MUST(o->lifetime_ref>0);if(!--o->lifetime_ref){MUST(!o->ref);ddraw_platform_release(o);deaths++;free(o);}}
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}
static DDrawObject*object_from_any(IDirectDraw7*p,void*v){(void)v;return p;}
static void emit_window_state_not_showing(DDrawObject*o){MUST(o->lifetime_ref>0&&!o->primary&&!o->guest_mode_changed);hides++;}
static void emit_display_mode(DDrawObject*o){MUST(o->lifetime_ref>0);emits++;}
static void object_display_size(DDrawObject*o,UINT*w,UINT*h){(void)o;*w=current_width;*h=current_height;}
static LONG ChangeDisplaySettingsA(DEVMODEA*m,DWORD f){cds_calls++;MUST(!m?!f:(f==CDS_FULLSCREEN&&m->dmSize==sizeof(*m)));return 0;}
PLATFORM
METHODS
static DDrawObject*owner(BOOL ex){DDrawObject*o=calloc(1,sizeof(*o));MUST(o);o->ref=o->lifetime_ref=1;o->initialized=TRUE;o->extended_factory=ex;active=o;ddraw_platform_attach(o);return o;}
static void release_public(void){MUST(active->ref>0);ddraw_Release(active);}
static void nested_mode(void){MUST(!ddraw_SetDisplayModeForVersion(active,4,1024,768,32,0,0));}
static void nested_coop(void){MUST(!ddraw_SetCooperativeLevelForVersion(active,2,(void*)(uintptr_t)22,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));}
int main(void){(void)&ddraw_platform_load_mode;const unsigned versions[]={1,2,3,4,7};for(unsigned repeat=0;repeat<16;repeat++)for(unsigned i=0;i<5;i++){
 unsigned v=versions[i];native_hr=query_hr=create_hr=0;DDrawObject*o=owner(i&1);MUST(o->platform_ddraw==&native&&native.refs==1&&o->lifetime_ref==1);MUST(!ddraw_TestCooperativeLevel(o));native_hr=E_FAIL;MUST(ddraw_TestCooperativeLevel(o)==E_FAIL);native_hr=0;unsigned c=creates;ddraw_platform_attach(o);MUST(creates==c);MUST(!ddraw_SetCooperativeLevelForVersion(o,v,(void*)(uintptr_t)11,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)&&actual_version==v&&o->lifetime_ref==1&&native.refs==1);
 MUST(!ddraw_SetDisplayModeForVersion(o,v,640,480,16,0,0)&&actual_version==v&&o->guest_mode_changed&&o->mode_set);DDSURFACEDESC2 d;HRESULT hr;MUST(ddraw_platform_mode(o,&d,&hr)&&!hr&&d.dwWidth==640&&d.dwHeight==480&&d.ddpfPixelFormat.dwRGBBitCount==16);
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
 DDSurface video={.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},system={.desc={.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY}}},managed={.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY,.dwCaps2=DDSCAPS2_TEXTUREMANAGE}}};video.next=&system;system.next=&managed;DDrawObject local={.surfaces=&video};ddraw_platform_lose_video_surfaces(&local);MUST(surface_IsLost(&video)==DDERR_SURFACELOST&&!surface_IsLost(&system)&&!surface_IsLost(&managed));DDSurface back={.cooperative_lost=TRUE};video.flip_next=&back;back.flip_next=&video;MUST(!surface_Restore(&video)&&!surface_IsLost(&video)&&!surface_IsLost(&back));ddraw_platform_lose_video_surfaces(&local);MUST(!ddraw_RestoreAllSurfaces(&local)&&!surface_IsLost(&video));
 o=owner(FALSE);o->lifetime_ref++;o->guest_mode_changed=TRUE;o->primary=&video;unsigned hidden=hides;MUST(!ddraw_Release(o)&&o->lifetime_ref==1&&!o->platform_ddraw&&!o->primary&&!o->guest_mode_changed&&!native.refs&&hides==hidden+1);ddraw_lifetime_release(o);
 const struct{unsigned version,refs;}native_lifetimes[]={NATIVE_LIFETIMES};for(unsigned i=0;i<4;i++){o=owner(native_lifetimes[i].version==7);BOOL pin=native_lifetimes[i].version>=4;ddraw_child_attach(o,pin);MUST(ddraw_Release(o)==native_lifetimes[i].refs);MUST(!!o->platform_ddraw==!!native_lifetimes[i].refs&&native.refs==native_lifetimes[i].refs);unsigned died=deaths;ddraw_child_detach(o,pin);MUST(deaths==died+1&&!native.refs);}
 MUST(!g_object_lock&&!native.refs&&deaths==90);printf("PASS actual platform-control C: %u checks; all five interfaces, physical/virtual modes, native validation rollback, nested callbacks, last-public-release lifetime, absolute native load, self-load and factory failures\n",checks);return 0;}
`.replace('NATIVE_LIFETIMES',native.releases.map(r=>'{'+[r.version,r.refs].join(',')+'}').join(',')).replace('PLATFORM',platform).replace('METHODS',['ddraw_AddRef','ddraw_Release','ddraw_child_attach','ddraw_child_detach','surface_IsLost','surface_Restore','ddraw_RestoreAllSurfaces','ddraw_TestCooperativeLevel','ddraw_RestoreDisplayModeForVersion','ddraw_RestoreDisplayMode','ddraw_SetCooperativeLevelForVersion','ddraw_SetCooperativeLevel','ddraw_SetDisplayModeForVersion','ddraw_SetDisplayMode'].map(extract).join('\n'));
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
