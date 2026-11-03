"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-color-key-")),source=fs.readFileSync(process.env.DDRAW_COLOR_KEY_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8");
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+"/entry.c",`#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "${process.env.DDRAW_COLOR_KEY_POLICY||root+'/runtime/d9wg_ddraw_color_key.h'}"
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;typedef int32_t HRESULT;typedef int BOOL;typedef void IUnknown;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_NOCOLORKEY (-2)
#define DDERR_NOCOLORKEYHW (-3)
#define DDERR_UNSUPPORTED (-4)
#define DDERR_INVALIDPIXELFORMAT (-5)
#define DDERR_OUTOFVIDEOMEMORY (-6)
#define CLASS_E_NOAGGREGATION (-7)
#define DDWG_FMT_UNKNOWN 0
#define DDWG_TRACE_ERROR(x) (x)
#define DDWG_TRACE_RESULT(x) (x)
#define DDWG_TRACE(...)
#define DDWG_TRACE_CHECKPOINT(...)
#define HOSTLOG_REFUSED(...)
#define UNSUPPORTED(s,h) (h)
#define DDCKEY_COLORSPACE 1u
#define DDCKEY_DESTBLT 2u
#define DDCKEY_DESTOVERLAY 4u
#define DDCKEY_SRCBLT 8u
#define DDCKEY_SRCOVERLAY 16u
#define DDSD_CAPS 1u
#define DDSD_WIDTH 4u
#define DDSD_HEIGHT 2u
#define DDSD_BACKBUFFERCOUNT 0x20u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_CKDESTOVERLAY 0x2000u
#define DDSD_CKDESTBLT 0x4000u
#define DDSD_CKSRCOVERLAY 0x8000u
#define DDSD_CKSRCBLT 0x10000u
#define DDSD_MIPMAPCOUNT 0x20000u
#define DDSCAPS_TEXTURE 0x1000u
#define DDSCAPS_OFFSCREENPLAIN 0x40u
#define DDSCAPS_PRIMARYSURFACE 0x200u
#define DDSCAPS_VIDEOPORT 0x08000000u
#define DDSCAPS_MIPMAP 0x00400000u
#define DDSCAPS_BACKBUFFER 4u
#define DDSCAPS_FLIP 0x10u
#define DDSCAPS_VISIBLE 0x8000u
#define DDSCAPS2_CUBEMAP 0x200u
typedef struct{DWORD dwColorSpaceLowValue,dwColorSpaceHighValue;}DDCOLORKEY,*LPDDCOLORKEY;
typedef struct{DWORD dwSize,dwFlags,dwRGBBitCount,dwRBitMask,dwGBitMask,dwBBitMask;}DDPIXELFORMAT;
typedef struct{DWORD dwSize,dwFlags,dwWidth,dwHeight,dwBackBufferCount,dwMipMapCount;struct{DWORD dwCaps,dwCaps2;}ddsCaps;DDPIXELFORMAT ddpfPixelFormat;DDCOLORKEY ddckCKSrcBlt,ddckCKDestBlt,ddckCKSrcOverlay,ddckCKDestOverlay;}DDSURFACEDESC2,*LPDDSURFACEDESC2;
typedef struct DDSurface DDSurface;typedef struct{void*vtbl7;DWORD mode_bpp,ref;DDSurface*surfaces,*primary;}DDrawObject,IDirectDraw7;
struct DDSurface{void*vtbl7;DDrawObject*owner;DDSURFACEDESC2 desc;DDCOLORKEY color_key[4];DWORD color_key_present;BOOL overlay_visible,front_of_chain,implicit,owns_flip_chain,owner_public_ref;DDSurface*overlay_destination,*next,*flip_next;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;typedef struct{unsigned unused;}DDrawPixelFormatDesc;
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
static unsigned ddraw_AddRef(DDrawObject*d){return ++d->ref;}
static void surface_destroy(DDSurface*s){free(s);}
static unsigned checks,sets,overlays,creates;static DWORD emitted_kind;static BOOL emitted_present;static DDCOLORKEY emitted_key;
static DDSurface*surface_from_iface(DDSurface*s){return s;}static DDrawObject*object_from_iface(DDrawObject*d){return d;}
static void emit_color_key(DDSurface*s,DWORD kind,const DDCOLORKEY*key,BOOL present){assert(s&&kind<4);sets++;emitted_kind=kind;emitted_present=present;if(key)emitted_key=*key;}
static void emit_overlay_state(DDSurface*s){assert(s&&s->overlay_visible);overlays++;}
static void object_display_size(DDrawObject*d,UINT*w,UINT*h){(void)d;*w=*h=32;}
static void mode_pixel_format(UINT bits,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){.dwSize=sizeof(*p),.dwRGBBitCount=bits};}
static void describe_pixel_format(const DDPIXELFORMAT*p,DDrawPixelFormatDesc*d){assert(p->dwRGBBitCount==32);d->unused=0;}
static unsigned ddraw_pixel_format_to_d3dformat(const DDrawPixelFormatDesc*p){(void)p;return 22;}
static void watch_object_window(DDrawObject*d){assert(d);}static BOOL seed_windowed_primary_from_screen(DDSurface*s){assert(s);return 1;}
static DDSurface*surface_create(DDrawObject*d,const DDSURFACEDESC2*p,unsigned f,UINT w,UINT h,DWORD caps){assert(d&&f==22&&w==32&&h==32&&caps==p->ddsCaps.dwCaps);creates++;DDSurface*s=calloc(1,sizeof(*s));assert(s);s->owner=d;s->desc=*p;return s;}
static DDSurface*surface_create_complex_texture(DDrawObject*d,const DDSURFACEDESC2*p,unsigned f,UINT w,UINT h,DWORD caps,UINT levels,BOOL cube){assert(levels==2&&!cube);return surface_create(d,p,f,w,h,caps);}
${['surface_color_key_kind','surface_description_color_key','surface_GetColorKey','surface_SetColorKey','surface_full_mip_count',...(source.includes('static BOOL surface_request_color_keys_valid(')?['surface_request_color_keys_valid']:[]),...(source.includes('static HRESULT ddraw_CreateSurfaceForVersion(')?['ddraw_CreateSurfaceForVersion']:[]),'ddraw_CreateSurface'].map(extract).join('\n')}
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"DDraw color key line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
static void events(void){sets=overlays=0;emitted_kind=99;emitted_present=0;emitted_key=(DDCOLORKEY){0};}
int main(void){(void)&ddraw_AddRef;(void)&surface_destroy;(void)d9wg_ddraw_color_key_normalize;DDrawObject owner={.mode_bpp=32};DDSurface surface={.owner=&owner,.overlay_visible=1},other={.owner=&owner,.overlay_visible=1,.overlay_destination=&surface};owner.surfaces=&surface;surface.next=&other;
const DWORD selectors[]={DDCKEY_SRCBLT,DDCKEY_DESTBLT,DDCKEY_SRCOVERLAY,DDCKEY_DESTOVERLAY},descflags[]={DDSD_CKSRCBLT,DDSD_CKDESTBLT,DDSD_CKSRCOVERLAY,DDSD_CKDESTOVERLAY};
const DWORD values[]={0,1,0xff,0xffff,0x12345678,0xffffffff};unsigned set_cases=0,create_cases=0;
for(UINT kind=0;kind<4;kind++)for(UINT lo=0;lo<6;lo++)for(UINT hi=0;hi<6;hi++)for(UINT range=0;range<2;range++){
 DDCOLORKEY key={values[lo],values[hi]},input=key;DDSurface before=surface;events();HRESULT hr=surface_SetColorKey(&surface,selectors[kind]|(range?DDCKEY_COLORSPACE:0),&key);BOOL accepted=!range||lo==hi;
 CHECK(hr==(accepted?DD_OK:DDERR_NOCOLORKEYHW)&&!memcmp(&key,&input,sizeof(key)));
 if(accepted){CHECK(sets==1&&emitted_kind==kind&&emitted_present&&emitted_key.dwColorSpaceLowValue==values[lo]&&emitted_key.dwColorSpaceHighValue==values[lo]);CHECK(overlays==(kind>=2));DDCOLORKEY got={0};CHECK(!surface_GetColorKey(&surface,selectors[kind],&got)&&got.dwColorSpaceLowValue==values[lo]&&got.dwColorSpaceHighValue==values[lo]);CHECK(surface.desc.dwFlags&descflags[kind]);CHECK(!memcmp(&got,surface_description_color_key(&surface,kind),sizeof(got)));}
 else CHECK(!sets&&!overlays&&!memcmp(&surface,&before,sizeof(surface)));
 set_cases++;
}
for(UINT kind=0;kind<4;kind++){events();CHECK(!surface_SetColorKey(&surface,selectors[kind]|DDCKEY_COLORSPACE,NULL)&&sets==1&&!emitted_present&&overlays==(kind>=2)&&!(surface.desc.dwFlags&descflags[kind]));DDCOLORKEY absent={7,9};CHECK(surface_GetColorKey(&surface,selectors[kind],&absent)==DDERR_NOCOLORKEY&&absent.dwColorSpaceLowValue==7&&absent.dwColorSpaceHighValue==9);}
for(UINT mode=0;mode<6;mode++){DWORD flags=mode==0?0:mode==1?DDCKEY_COLORSPACE:mode==2?DDCKEY_SRCBLT|DDCKEY_DESTBLT:mode==3?DDCKEY_SRCBLT|0x80000000u:mode==4?0xffffffffu:0x20u;DDCOLORKEY key={1,2};DDSurface before=surface;events();CHECK(surface_SetColorKey(&surface,flags,&key)==DDERR_INVALIDPARAMS&&!sets&&!overlays&&!memcmp(&surface,&before,sizeof(surface)));CHECK(surface_GetColorKey(&surface,flags,&key)==DDERR_INVALIDPARAMS&&key.dwColorSpaceLowValue==1&&key.dwColorSpaceHighValue==2);}
CHECK(surface_GetColorKey(&surface,DDCKEY_SRCBLT,NULL)==DDERR_INVALIDPARAMS);
for(UINT profile=0;profile<4;profile++)for(UINT mask=1;mask<16;mask++)for(UINT bad=0;bad<5;bad++){
 DDSURFACEDESC2 request={.dwSize=sizeof(request),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT,.dwWidth=32,.dwHeight=32,.ddsCaps={.dwCaps=profile==0?DDSCAPS_OFFSCREENPLAIN:profile==1?DDSCAPS_TEXTURE:profile==2?DDSCAPS_PRIMARYSURFACE:DDSCAPS_TEXTURE|DDSCAPS_MIPMAP},.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwRGBBitCount=32}};
 if(profile==3){request.dwFlags|=DDSD_MIPMAPCOUNT;request.dwMipMapCount=2;}
 DDCOLORKEY*keys[]={&request.ddckCKSrcBlt,&request.ddckCKDestBlt,&request.ddckCKSrcOverlay,&request.ddckCKDestOverlay};BOOL rejected=0;
 for(UINT kind=0;kind<4;kind++){*keys[kind]=(DDCOLORKEY){0x11223344+kind,0x11223344+kind};if(mask&(1u<<kind))request.dwFlags|=descflags[kind];if(bad==kind+1){keys[kind]->dwColorSpaceHighValue^=1;rejected=(mask&(1u<<kind))!=0;}}
 DDSURFACEDESC2 before=request;DDSurface*out=(void*)(uintptr_t)1;unsigned old_creates=creates;HRESULT hr=ddraw_CreateSurface(&owner,&request,&out,NULL);
 CHECK(hr==(rejected?DDERR_NOCOLORKEYHW:DD_OK)&&!memcmp(&before,&request,sizeof(request)));
 if(rejected)CHECK(!out&&creates==old_creates);else{CHECK(out&&creates==old_creates+1&&out->desc.dwFlags==request.dwFlags);free(out);owner.primary=NULL;}
 create_cases++;
}
CHECK(set_cases==288&&create_cases==300);
printf("PASS actual DDraw surface color keys: %u checks; %u setter cases, %u full creation preflights with allocation stubs, four key kinds, canonical descriptions/wire values, explicit range refusal, overlay atomicity, ignored unflagged description values and unchanged inputs\\n",checks,set_cases,create_cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/entry.c","-o",dir+"/entry"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/entry",[],{encoding:"utf8",timeout:20000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
