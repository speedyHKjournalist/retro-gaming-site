"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_CREATION_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),d3d=fs.readFileSync(process.env.DDRAW_TARGET_SOURCE||root+"/ddrawproxy/d3d7_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-creation-"));
function extract(input,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(input);assert(m,name);let end=input.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(input[end]==="{")depth++;if(input[end]==="}")depth--;}return input.slice(m.index,end);}
const thunk=source.slice(source.indexOf('static HRESULT WINAPI tag##_CreateSurface('),source.indexOf('static HRESULT WINAPI tag##_DuplicateSurface(')).replace(/\\\n/g,'\n').replaceAll('tag##','legacy').replaceAll('SURFIFACE','IDirectDrawSurface7').replaceAll('IFACE','IDirectDraw7').replaceAll('DESC *request','DDSURFACEDESC2 *request').replaceAll('sizeof(DESC)','sizeof(DDSURFACEDESC2)').replaceAll('VERSION','DD_VERSION_4');
const definitions=fs.readFileSync(__dirname+"/ddraw_color_key_test.js","utf8").split("typedef uint32_t DWORD,UINT;")[1].split("typedef struct DDSurface DDSurface;")[0].replace("dwSize,dwFlags,dwRGBBitCount", "dwSize,dwFlags,dwFourCC,dwRGBBitCount");
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_ddraw_color_key.h"
typedef uint32_t DWORD,UINT;${definitions}
#define ZeroMemory(p,n) memset(p,0,n)
#define DDERR_INVALIDOBJECT (-9)
#define DDERR_NOCOOPERATIVELEVELSET (-25)
#define DDSCL_NORMAL 0x8u
#define D3D_OK 0
#define DDERR_NOPALETTEATTACHED (-21)
#define D3DERR_SURFACENOTINVIDMEM (-22)
#define E_FAIL (-23)
#define DDSCAPS_3DDEVICE 0x2000u
#define DD_VERSION_1 1u
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
typedef struct DDSurface DDSurface;
typedef struct DDrawObject{void*vtbl7;DWORD mode_bpp,ref,cooperative_flags;BOOL mode_set,guest_mode_changed;int32_t lifetime_ref;DDSurface*surfaces,*primary;struct DDrawObject*next;}DDrawObject,IDirectDraw7;
typedef struct{size_t bytes;}DDSurfaceMemory;
struct DDSurface{void*vtbl7,*storage;DDrawObject*owner;DDSURFACEDESC2 desc;DDSurface*next,*flip_next;BOOL front_of_chain,owns_flip_chain,implicit,owner_public_ref,indexed;UINT format,width,height;void*palette;DDSurfaceMemory*memory;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
typedef struct{UINT unused;}DDrawPixelFormatDesc;
typedef struct{UINT value;}GUID;typedef const GUID*REFCLSID;
static const GUID IID_IDirect3DRGBDevice={1},IID_IDirect3DHALDevice={2};
static DDrawObject*g_objects;static int g_object_lock;
static unsigned checks,creates,destroys,watches,pins,last_levels;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"surface creation line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static BOOL guid_equal(REFCLSID a,REFCLSID b){return a->value==b->value;}
static BOOL format_is_depth(UINT f){return f==75;}static BOOL format_is_yuv(UINT f){return f==100;}static BOOL format_is_compressed(UINT f){return f==101;}
static BOOL IsBadWritePtr(void*p,size_t n){(void)n;return !p;}
static DDrawObject*object_from_iface(DDrawObject*p){return p;}
static unsigned ddraw_AddRef(DDrawObject*d){pins++;return ++d->ref;}
static int32_t InterlockedIncrement(int32_t*p){return ++*p;}
static void ddraw_lifetime_release(DDrawObject*d){MUST(d->lifetime_ref>0);--d->lifetime_ref;}
static void ddraw_platform_poll_loss(DDrawObject*d){(void)d;}
static unsigned preparation_behavior,prepared_calls,disposed_calls;
typedef struct DDPlatformPrimary{DDSURFACEDESC2 desc;BOOL metadata;}DDPlatformPrimary;
static void ddraw_platform_primary_prepare(DDrawObject*d,unsigned v,BOOL flip,DWORD back,DDPlatformPrimary*p){MUST(d->lifetime_ref>0);(void)v;(void)flip;(void)back;prepared_calls++;memset(p,0,sizeof(*p));if(preparation_behavior==1){p->metadata=1;p->desc.dwWidth=352;p->desc.dwHeight=288;p->desc.ddpfPixelFormat.dwRGBBitCount=16;p->desc.ddpfPixelFormat.dwFourCC=23;}else if(preparation_behavior==2)d->ref=0;else if(preparation_behavior==3)d->primary=(void*)0xdeadbeef;}
static void ddraw_platform_primary_dispose(DDPlatformPrimary*p){(void)p;disposed_calls++;}
static void ddraw_platform_primary_attach(DDrawObject*d,DDSurface*s,DDPlatformPrimary*p){(void)d;(void)s;ddraw_platform_primary_dispose(p);}
#ifndef DDERR_PRIMARYSURFACEALREADYEXISTS
#define DDERR_PRIMARYSURFACEALREADYEXISTS ((HRESULT)0x88760234u)
#endif
static void ddraw_platform_sync(DDrawObject*d){(void)d;}
static void object_display_size(DDrawObject*d,UINT*w,UINT*h){MUST(d);*w=640;*h=480;}
static void mode_pixel_format(UINT bpp,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){.dwSize=sizeof(*p),.dwRGBBitCount=bpp};}
static void describe_pixel_format(DDPIXELFORMAT*p,DDrawPixelFormatDesc*d){d->unused=p->dwFourCC;}
static UINT ddraw_pixel_format_to_d3dformat(DDrawPixelFormatDesc*p){return p->unused?p->unused:22;}
static UINT format_compressed_block_bytes(UINT f){return f==0x31545844u?8:(f==0x32545844u||f==0x33545844u||f==0x34545844u||f==0x35545844u)?16:0;}static UINT format_bytes_per_pixel(UINT f){return format_compressed_block_bytes(f)?0:f==0x32595559u||f==0x59565955u?2:4;}
static void watch_object_window(DDrawObject*d){MUST(d);watches++;if(preparation_behavior==4)d->ref=0;}
static BOOL seed_windowed_primary_from_screen(DDSurface*s){MUST(s);return 1;}
static void surface_creation_profile(DDSurface*s,unsigned version){MUST(s&&version);}
static DDSurface*surface_create(DDrawObject*d,const DDSURFACEDESC2*r,unsigned f,UINT w,UINT h,DWORD caps){MUST(f&&w&&h);DDSurface*s=calloc(1,sizeof(*s));MUST(s);s->owner=d;s->desc=*r;s->desc.lpSurface=NULL;s->desc.dwFlags&=~DDSD_LPSURFACE;s->desc.ddsCaps.dwCaps=caps;s->format=f;s->width=w;s->height=h;s->next=d->surfaces;d->surfaces=s;creates++;return s;}
static DDSurface*surface_create_complex_texture(DDrawObject*d,const DDSURFACEDESC2*r,unsigned f,UINT w,UINT h,DWORD caps,UINT levels,BOOL cube){MUST(levels>=1&&levels<=8&&!cube);last_levels=levels;return surface_create(d,r,f,w,h,caps);}
static void surface_destroy(DDSurface*s){DDSurface*walk=s->flip_next;while(walk&&walk!=s){DDSurface*next=walk->flip_next;free(walk);destroys++;walk=next;}s->owner->surfaces=NULL;s->owner->primary=NULL;free(s);destroys++;}
${["surface_request_color_keys_valid","surface_full_mip_count","ddraw_CreateSurfaceForVersion","ddraw_video_memory_free"].map(n=>extract(source,n)).join("\n")}
${extract(d3d,"d3d7_validate_device_target")}
static DDSURFACEDESC2 desc(DWORD caps){return(DDSURFACEDESC2){.dwSize=sizeof(DDSURFACEDESC2),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT,.dwWidth=63,.dwHeight=63,.dwBackBufferCount=9,.lpSurface=(void*)0xdead,.ddsCaps={.dwCaps=caps}};}
static void request(DDrawObject*d,DDSURFACEDESC2*r,HRESULT expected,DWORD outcaps,unsigned version){DDSurface*out=(void*)1;DDSURFACEDESC2 before=*r;unsigned count=creates,p=pins,watch=watches;MUST(ddraw_CreateSurfaceForVersion(d,r,&out,NULL,version)==expected);MUST(!memcmp(r,&before,sizeof(*r)));if(expected){MUST(!out&&creates==count&&pins==p&&watches==watch&&!d->surfaces&&!d->primary);}else{MUST(out&&out->desc.ddsCaps.dwCaps==outcaps&&out->owner_public_ref==(version>=4));MUST(!out->desc.lpSurface&&!(out->desc.dwFlags&(DDSD_LPSURFACE|DDSD_LINEARSIZE)));MUST(out->desc.dwBackBufferCount==((r->dwFlags&DDSD_BACKBUFFERCOUNT)?r->dwBackBufferCount:0));if(r->dwFlags&DDSD_BACKBUFFERCOUNT){DDSurface*b=out->flip_next;MUST(out->front_of_chain);for(unsigned i=0;i<r->dwBackBufferCount;i++,b=b->flip_next){MUST(b&&b!=out&&b->implicit&&!b->desc.dwBackBufferCount&&!(b->desc.dwFlags&DDSD_BACKBUFFERCOUNT));MUST(b->desc.ddsCaps.dwCaps==((outcaps&~(DDSCAPS_PRIMARYSURFACE|DDSCAPS_VISIBLE|DDSCAPS_FRONTBUFFER|DDSCAPS_BACKBUFFER))|(i?0:DDSCAPS_BACKBUFFER)));}MUST(b==out);}surface_destroy(out);if(version>=4)d->ref--;}}
#define SUCCEEDED(h) ((h)>=0)
#define DDRAW7(p) (p)
static void legacy_widen(DDSURFACEDESC2*a,DDSURFACEDESC2*b){*b=*a;}
static DDSurface*surface_from_any(DDSurface*s,void*v){(void)v;return s;}
static void*surface_interface(DDSurface*s,unsigned v){(void)v;return s;}
static unsigned surface_factory_view(unsigned v){return v;}
${thunk}
static void primary_allocation_metadata(void){const unsigned versions[]={1,2,3,4,7};for(unsigned v=0;v<5;v++)for(unsigned virtual_mode=0;virtual_mode<2;virtual_mode++){DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL,.mode_set=virtual_mode};DDSURFACEDESC2 r=desc(DDSCAPS_PRIMARYSURFACE);DDSurface*out=NULL;preparation_behavior=1;unsigned prepared=prepared_calls,disposed=disposed_calls;MUST(!ddraw_CreateSurfaceForVersion(&o,&r,&out,NULL,versions[v]));MUST(out&&out->width==(virtual_mode?640:352)&&out->height==(virtual_mode?480:288)&&out->format==(virtual_mode?22:23)&&out->desc.ddpfPixelFormat.dwRGBBitCount==(virtual_mode?32:16)&&o.lifetime_ref==0&&prepared_calls==prepared+1&&disposed_calls==disposed+1);surface_destroy(out);if(versions[v]>=4)o.ref--;MUST(o.ref==1);}for(unsigned behavior=2;behavior<=4;behavior++){DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};DDSURFACEDESC2 r=desc(DDSCAPS_PRIMARYSURFACE);DDSurface*out=(void*)1;preparation_behavior=behavior;unsigned count=creates,disposed=disposed_calls;MUST(ddraw_CreateSurfaceForVersion(&o,&r,&out,NULL,7)==(behavior==3?DDERR_PRIMARYSURFACEALREADYEXISTS:DDERR_INVALIDOBJECT)&&!out&&o.lifetime_ref==0&&disposed_calls==disposed+1);MUST(creates==count+(behavior==4)&&!o.surfaces&&o.ref==(behavior==3));if(behavior==3)MUST(o.primary==(void*)0xdeadbeef);else MUST(!o.primary);}preparation_behavior=0;}
static void no_cooperative_level(void){for(unsigned version=1;version<=7;version++){DDrawObject o={.mode_bpp=32,.ref=1};const DWORD caps[]={DDSCAPS_PRIMARYSURFACE,DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY,DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY};for(unsigned i=0;i<4;i++){DDSURFACEDESC2 r=desc(i<3?caps[i]:0);DDSurface*out=(void*)0xdeadbeef;unsigned old_creates=creates;MUST(ddraw_CreateSurfaceForVersion(&o,i<3?&r:NULL,&out,NULL,version)==DDERR_NOCOOPERATIVELEVELSET&&out==(void*)0xdeadbeef&&creates==old_creates&&!o.surfaces);out=(void*)0xdeadbeef;MUST(legacy_CreateSurface(&o,i<3?&r:NULL,&out,NULL)==DDERR_NOCOOPERATIVELEVELSET&&out==(void*)0xdeadbeef&&creates==old_creates);}o.cooperative_flags=DDSCL_NORMAL;DDSurface*out=(void*)0xdeadbeef;MUST(legacy_CreateSurface(&o,NULL,&out,NULL)==DDERR_INVALIDPARAMS&&out==(void*)0xdeadbeef);}}
static void creations(void){const DWORD vid=DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM;unsigned versions[]={1,2,3,4,7};for(unsigned v=0;v<5;v++){DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};const DWORD roles[]={0,DDSCAPS_OFFSCREENPLAIN,DDSCAPS_TEXTURE,DDSCAPS_3DDEVICE,DDSCAPS_ZBUFFER};for(unsigned i=0;i<5;i++)for(unsigned place=0;place<3;place++){DWORD caps=roles[i]|(place==1?DDSCAPS_SYSTEMMEMORY:place==2?DDSCAPS_VIDEOMEMORY:0);DDSURFACEDESC2 r=desc(caps);request(&o,&r,0,caps|(place==1?0:vid),versions[v]);r.dwFlags|=DDSD_LPSURFACE;request(&o,&r,place==2?DDERR_INVALIDCAPS:DDERR_INVALIDPARAMS,0,versions[v]);}for(unsigned i=0;i<5;i++){DDSURFACEDESC2 r=desc(roles[i]|DDSCAPS_ALLOCONLOAD);request(&o,&r,i==2?0:DDERR_INVALIDCAPS,r.ddsCaps.dwCaps|vid,versions[v]);}for(unsigned managed=0;managed<2;managed++){DDSURFACEDESC2 r=desc(DDSCAPS_TEXTURE);r.ddsCaps.dwCaps2=managed?DDSCAPS2_D3DTEXTUREMANAGE:DDSCAPS2_TEXTUREMANAGE;request(&o,&r,0,DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,versions[v]);}DDSURFACEDESC2 r=desc(DDSCAPS_SYSTEMMEMORY|DDSCAPS_VIDEOMEMORY);request(&o,&r,DDERR_INVALIDCAPS,0,versions[v]);const DWORD base[]={DDSCAPS_PRIMARYSURFACE,DDSCAPS_OFFSCREENPLAIN,DDSCAPS_TEXTURE};for(unsigned i=0;i<3;i++)for(unsigned exclusive=0;exclusive<2;exclusive++){o.cooperative_flags=exclusive?DDSCL_EXCLUSIVE:DDSCL_NORMAL;for(unsigned complex=0;complex<2;complex++)for(unsigned flip=0;flip<2;flip++)for(unsigned count=0;count<3;count++){r=desc(base[i]|(complex?DDSCAPS_COMPLEX:0)|(flip?DDSCAPS_FLIP:0));if(count){r.dwFlags|=DDSD_BACKBUFFERCOUNT;r.dwBackBufferCount=count-1;}HRESULT hr=flip?(!complex||count!=2?DDERR_INVALIDCAPS:i==0&&!exclusive?DDERR_NOEXCLUSIVEMODE:0):count||complex?DDERR_INVALIDCAPS:0;request(&o,&r,hr,r.ddsCaps.dwCaps|vid|(!i?DDSCAPS_VISIBLE:0)|(flip?DDSCAPS_FRONTBUFFER:0),versions[v]);}}const DWORD invalid[]={DDSCAPS_TEXTURE,DDSCAPS_OFFSCREENPLAIN,DDSCAPS_ZBUFFER,DDSCAPS_OVERLAY,DDSCAPS_BACKBUFFER};for(unsigned i=0;i<5;i++){r=desc(DDSCAPS_PRIMARYSURFACE|invalid[i]);request(&o,&r,DDERR_INVALIDCAPS,0,versions[v]);}o.cooperative_flags=DDSCL_EXCLUSIVE;for(unsigned i=0;i<2;i++){r=desc(DDSCAPS_PRIMARYSURFACE|DDSCAPS_COMPLEX|DDSCAPS_FLIP|(i?DDSCAPS_BACKBUFFER:DDSCAPS_FRONTBUFFER));r.dwFlags|=DDSD_BACKBUFFERCOUNT;r.dwBackBufferCount=1;request(&o,&r,DDERR_INVALIDCAPS,0,versions[v]);}r=desc(DDSCAPS_PRIMARYSURFACE|DDSCAPS_FRONTBUFFER);request(&o,&r,versions[v]>1?DDERR_INVALIDCAPS:0,r.ddsCaps.dwCaps|vid|DDSCAPS_VISIBLE,versions[v]);MUST(o.ref==1&&!o.surfaces);}MUST(creates==destroys);}
static void mipmaps(void){unsigned versions[]={1,2,3,4,7};const DWORD vid=DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM;for(unsigned v=0;v<5;v++){DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};const DWORD roles[]={DDSCAPS_MIPMAP,DDSCAPS_COMPLEX,DDSCAPS_MIPMAP|DDSCAPS_COMPLEX,DDSCAPS_TEXTURE|DDSCAPS_MIPMAP,DDSCAPS_TEXTURE|DDSCAPS_COMPLEX,DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP};for(unsigned i=0;i<6;i++)for(unsigned count=0;count<4;count++){DDSURFACEDESC2 r=desc(roles[i]);r.dwWidth=128;r.dwHeight=32;if(count){r.dwFlags|=DDSD_MIPMAPCOUNT;r.dwMipMapCount=count-1;}HRESULT hr=i!=3&&i!=5?DDERR_INVALIDCAPS:i==3&&count?DDERR_INVALIDCAPS:i==5&&count==1?DDERR_INVALIDPARAMS:0;last_levels=0;request(&o,&r,hr,roles[i]|vid,versions[v]);if(!hr)MUST(last_levels==(i==3?1:count?count-1:versions[v]>=7?8:6));}}}
static void borrowed_inputs(void){for(unsigned version=1;version<=7;version++){DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};for(unsigned pitch=0;pitch<5;pitch++)for(unsigned flags=0;flags<4;flags++)for(unsigned ptr=0;ptr<2;ptr++){DDSURFACEDESC2 r=desc(DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN);r.dwFlags|=DDSD_LPSURFACE|(flags&1?DDSD_PITCH:0)|(flags&2?DDSD_LINEARSIZE:0);r.lpSurface=ptr?(void*)0xdead:NULL;const int32_t pitches[]={0,248,252,254,256};r.lPitch=pitches[pitch];HRESULT expected=version<4||!(flags&1)||!ptr||pitch<2||pitch==3?DDERR_INVALIDPARAMS:0;request(&o,&r,expected,r.ddsCaps.dwCaps,version);}}}
// Frozen Wine block-creation matrix: the root is checked even in system RAM;
// packed YUV keeps its native odd-size acceptance. No image data is touched here.
static void block_inputs(void){
 const unsigned formats[]={0x31545844u,0x32545844u,0x33545844u,0x34545844u,0x35545844u,0x32595559u,0x59565955u};
 const struct{DWORD flags;unsigned value;int rel;HRESULT hr;}cases[]={
 {DDSD_LINEARSIZE,0,0,0},{DDSD_LINEARSIZE,2,0,0},{DDSD_PITCH,0,0,0},{DDSD_PITCH,1,0,0},
 {DDSD_LPSURFACE,0,0,DDERR_INVALIDPARAMS},{DDSD_LPSURFACE|DDSD_LINEARSIZE,0,0,DDERR_INVALIDPARAMS},
 {DDSD_LPSURFACE|DDSD_LINEARSIZE,1,0,DDERR_INVALIDPARAMS},{DDSD_LPSURFACE|DDSD_LINEARSIZE,2,0,0},
 {DDSD_LPSURFACE|DDSD_LINEARSIZE,2,1,0},{DDSD_LPSURFACE|DDSD_LINEARSIZE,2,-1,DDERR_INVALIDPARAMS},
 {DDSD_LPSURFACE|DDSD_PITCH,0,0,0},{DDSD_LPSURFACE|DDSD_PITCH,1,0,0},
 {DDSD_LPSURFACE|DDSD_PITCH,2,0,0},{DDSD_LPSURFACE|DDSD_PITCH|DDSD_LINEARSIZE,2,0,0}};
 for(unsigned version=4;version<=7;version+=3)for(unsigned fmt=0;fmt<7;fmt++){
  DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};unsigned b=fmt<5?(fmt?16:8):0;
  for(unsigned place=0;place<3;place++)for(unsigned w=1;w<=8;w++)for(unsigned h=1;h<=8;h++){
   DDSURFACEDESC2 r=desc(DDSCAPS_TEXTURE|(place==1?DDSCAPS_SYSTEMMEMORY:place==0?DDSCAPS_VIDEOMEMORY:0));
   r.dwFlags|=DDSD_PIXELFORMAT;r.ddpfPixelFormat.dwFourCC=formats[fmt];r.dwWidth=w;r.dwHeight=h;
   if(place==2)r.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;
   request(&o,&r,b&&((w|h)&3u)?DDERR_INVALIDPARAMS:0,DDSCAPS_TEXTURE|(place==0?DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM:DDSCAPS_SYSTEMMEMORY),version);
  }
  if(!b)continue;
  for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){
   DDSURFACEDESC2 r=desc(DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE);r.dwFlags|=DDSD_PIXELFORMAT|cases[i].flags;
   r.ddpfPixelFormat.dwFourCC=formats[fmt];r.dwWidth=r.dwHeight=8;r.dwLinearSize=(cases[i].value==1?2:cases[i].value==2?4:0)*b+cases[i].rel;
   request(&o,&r,version==7&&i>=10?DDERR_INVALIDPARAMS:cases[i].hr,r.ddsCaps.dwCaps,version);
  }
  DDSURFACEDESC2 r=desc(DDSCAPS_TEXTURE|DDSCAPS_MIPMAP|DDSCAPS_COMPLEX);r.dwFlags|=DDSD_PIXELFORMAT;r.ddpfPixelFormat.dwFourCC=formats[fmt];r.dwWidth=r.dwHeight=8;
  request(&o,&r,0,r.ddsCaps.dwCaps|DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM,version);MUST(last_levels==4);
  r=desc(DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE);r.dwFlags|=DDSD_PIXELFORMAT|DDSD_LPSURFACE|DDSD_PITCH;r.ddpfPixelFormat.dwFourCC=formats[fmt];r.dwWidth=r.dwHeight=8;r.lPitch=-1;
  request(&o,&r,version==7?DDERR_INVALIDPARAMS:0,r.ddsCaps.dwCaps,version);r.lpSurface=NULL;request(&o,&r,DDERR_INVALIDPARAMS,0,version);
  r.lpSurface=(void*)0xdead;request(&o,&r,DDERR_INVALIDPARAMS,0,3);
  r.ddsCaps.dwCaps=DDSCAPS_VIDEOMEMORY|DDSCAPS_TEXTURE;request(&o,&r,DDERR_INVALIDCAPS,0,version);
 }
}
static void managed_roles(void){
 const DWORD roles[]={0,DDSCAPS_TEXTURE,DDSCAPS_OFFSCREENPLAIN};const DWORD places[]={0,DDSCAPS_SYSTEMMEMORY,DDSCAPS_VIDEOMEMORY};
 for(unsigned version=4;version<=7;version+=3)for(unsigned r=0;r<3;r++)for(unsigned p=0;p<3;p++)for(unsigned m=0;m<3;m++)for(unsigned write=0;write<2;write++){
  DDrawObject o={.mode_bpp=32,.ref=1,.cooperative_flags=DDSCL_NORMAL};DDSURFACEDESC2 d=desc(roles[r]|places[p]|(write?DDSCAPS_WRITEONLY:0));d.ddsCaps.dwCaps2=m==1?DDSCAPS2_TEXTUREMANAGE:m==2?DDSCAPS2_D3DTEXTUREMANAGE:0;
  HRESULT hr=m?(r!=1||p?DDERR_INVALIDCAPS:0):write?DDERR_INVALIDCAPS:0;
  DWORD caps=d.ddsCaps.dwCaps|(m?DDSCAPS_SYSTEMMEMORY:p==1?0:DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM);
  request(&o,&d,hr,caps,version);MUST(o.ref==1);
 }
}
static void targets(void){DDSurface s={0};for(unsigned version=1;version<=7;version++)for(unsigned soft=0;soft<2;soft++)for(unsigned role=0;role<4;role++)for(unsigned video=0;video<2;video++)for(unsigned indexed=0;indexed<2;indexed++)for(unsigned palette=0;palette<2;palette++){s=(DDSurface){.format=22,.indexed=indexed,.palette=palette?(void*)1:NULL,.desc={.ddsCaps={.dwCaps=(role&1?DDSCAPS_3DDEVICE:0)|(role&2?DDSCAPS_ZBUFFER:0)|(video?DDSCAPS_VIDEOMEMORY:DDSCAPS_SYSTEMMEMORY)}}};HRESULT expected=!(role&1)||(role&2)?DDERR_INVALIDCAPS:indexed&&!palette?DDERR_NOPALETTEATTACHED:!soft&&!video?D3DERR_SURFACENOTINVIDMEM:indexed&&(!soft||version>=7)?(soft&&!video?E_FAIL:DDERR_INVALIDPIXELFORMAT):0;MUST(d3d7_validate_device_target(&s,soft?&IID_IDirect3DRGBDevice:&IID_IDirect3DHALDevice,version)==expected);}s=(DDSurface){.desc={.ddsCaps={.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_VIDEOMEMORY}}};const unsigned formats[]={75,100,101};for(unsigned i=0;i<3;i++){s.format=formats[i];MUST(d3d7_validate_device_target(&s,&IID_IDirect3DHALDevice,2)==DDERR_INVALIDPIXELFORMAT);}}
static void memory(void){const DWORD budget=48u*1024u*1024u;DDSurfaceMemory memory[]={ {100},{200},{300},{(size_t)budget+1} };DDSurface a={.memory=&memory[0],.desc={.ddsCaps={.dwCaps=DDSCAPS_VIDEOMEMORY}}},alias=a,mip=a,sys=a,foreign=a;DDrawObject o={.surfaces=&a},other={.surfaces=&foreign};MUST(ddraw_video_memory_free()==budget);g_objects=&o;MUST(ddraw_video_memory_free()==budget-100);a.next=&alias;alias.next=&mip;mip.memory=&memory[1];mip.next=&sys;sys.memory=&memory[2];sys.desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY;MUST(ddraw_video_memory_free()==budget-300);o.next=&other;foreign.memory=&memory[2];MUST(ddraw_video_memory_free()==budget-600);foreign.memory=&memory[0];MUST(ddraw_video_memory_free()==budget-300);o.surfaces=&alias;MUST(ddraw_video_memory_free()==budget-300);other.surfaces=NULL;alias.next=&sys;MUST(ddraw_video_memory_free()==budget-100);sys.memory=&memory[3];MUST(ddraw_video_memory_free()==budget-100);sys.desc.ddsCaps.dwCaps=DDSCAPS_VIDEOMEMORY;MUST(ddraw_video_memory_free()==0);o.surfaces=NULL;MUST(ddraw_video_memory_free()==budget);g_objects=NULL;}
int main(void){primary_allocation_metadata();no_cooperative_level();creations();mipmaps();borrowed_inputs();block_inputs();managed_roles();targets();memory();MUST(!g_object_lock);printf("PASS actual surface creation C: %u checks, DD1/2/3/4/7 role/placement/flip/backbuffer contracts, rejection before allocation, input preservation, palette/memory/device validation priority, and live global video accounting with duplicate/mip/system-memory isolation and saturation\\n",checks);return 0;}
`;
try{const file=dir+"/creation.c",binary=dir+"/creation";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
