"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.DDRAW_ROP_DC_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-rop-dc-"));
function extract(name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} return source.slice(m.index, end);}
const nativeClipping=JSON.parse(fs.readFileSync(path.resolve(root,"../../v86/tests/glbridge/fixtures/ddraw_native_clipping.json")));
const nativeClippingSource=path.resolve(root,"../",nativeClipping.source);
assert.equal(require("node:crypto").createHash("sha256").update(fs.readFileSync(nativeClippingSource)).digest("hex"),nativeClipping.sourceSha256);
const bltViews=[1,2,3,4].map(view=>{
 const a=source.indexOf("static HRESULT WINAPI tag##_Blt("),b=source.indexOf("static HRESULT WINAPI tag##_BltBatch(",a);assert(a>=0&&b>a);
 return source.slice(a,b).replace(/\\\r?\n/g,"\n").replaceAll("tag##","view"+view).replaceAll("IFACE","IDirectDrawSurface7").replace(/\bVERSION\b/g,String(view));
}).join("\n");
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "${root}/ddrawproxy/ddraw_protocol.h"
typedef uint32_t DWORD,UINT;typedef int32_t LONG,HRESULT;typedef uint16_t WORD;typedef unsigned char BYTE;typedef int BOOL;typedef void *HDC;typedef void *HBITMAP;
typedef struct{LONG left,top,right,bottom;}RECT,*LPRECT;
typedef struct{BYTE peRed,peGreen,peBlue,peFlags;}PALETTEENTRY;
typedef struct{BYTE rgbBlue,rgbGreen,rgbRed,rgbReserved;}RGBQUAD;
typedef struct{DWORD biSize;LONG biWidth,biHeight;WORD biPlanes,biBitCount;DWORD biCompression,biSizeImage;LONG biXPelsPerMeter,biYPelsPerMeter;DWORD biClrUsed,biClrImportant;}BITMAPINFOHEADER;
typedef struct{BITMAPINFOHEADER bmiHeader;RGBQUAD bmiColors[256];}BITMAPINFO;
typedef struct{DWORD dwColorSpaceLowValue,dwColorSpaceHighValue;}DDCOLORKEY;
typedef struct{DWORD dwROP,dwFillColor,dwFillDepth,dwDDFX;DDCOLORKEY ddckSrcColorkey,ddckDestColorkey;}DDBLTFX,*LPDDBLTFX;
typedef struct{DWORD dwFlags,dwRBitMask,dwGBitMask,dwBBitMask;}PF;
typedef struct{PALETTEENTRY entries[256];}DDPalette;
typedef struct DDSurface DDSurface;typedef struct{DDSurface*primary;}DDrawObject;
typedef struct{BOOL gpu_dirty;}Memory;
struct DDSurface{DWORD handle,width,height,pitch,bytes_per_pixel,format,lock_depth,color_key_present;DDCOLORKEY color_key[4];BOOL indexed,external_memory;DDPalette*palette;DDrawObject*owner;Memory*memory;void*clipper;BYTE*shadow,*dib_bits;HDC dc;void*gdi_dc;HBITMAP dib,old_bitmap;struct{PF ddpfPixelFormat;}desc;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
#define SURFACE7(p) (p)
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_GENERIC (-2)
#define DDERR_SURFACEBUSY (-3)
#define DDERR_NORASTEROPHW (-4)
#define DDERR_INVALIDPIXELFORMAT (-5)
#define DDERR_NOCOLORKEY (-6)
#define DDERR_UNSUPPORTED (-7)
#define DDERR_DCALREADYCREATED (-8)
#define DDERR_INVALIDRECT (-9)
#define E_NOTIMPL (-10)
#define DDERR_NOCLIPLIST (-13)
#define DDERR_BLTFASTCANTCLIP (-14)
#define FAILED(x) ((x)<0)
#define DDBLT_WAIT 0x1000u
#define DDBLT_DONOTWAIT 0x2000u
#define DDBLT_ASYNC 0x4000u
#define DDBLTFAST_WAIT 0x10u
#define DDBLTFAST_DONOTWAIT 0x20u
#define DDBLTFAST_SRCCOLORKEY 1u
#define DDBLTFAST_DESTCOLORKEY 2u
#define DDERR_NODC (-11)
#define DDERR_CANTCREATEDC (-12)
#define DDBLT_COLORFILL 1u
#define DDBLT_DEPTHFILL 2u
#define DDBLT_ROP 4u
#define DDBLT_KEYSRC 8u
#define DDBLT_KEYSRCOVERRIDE 16u
#define DDBLT_KEYDEST 32u
#define DDBLT_KEYDESTOVERRIDE 64u
#define DDBLT_DDFX 128u
#define DDBLTFX_MIRRORLEFTRIGHT 1u
#define DDBLTFX_MIRRORUPDOWN 2u
#define DDBLTFX_ROTATE90 4u
#define DDBLTFX_ROTATE180 8u
#define DDBLTFX_ROTATE270 16u
#define DDBLTFX_ARITHSTRETCHY 32u
#define BLACKNESS 0x42u
#define WHITENESS 0xff0062u
#define SRCCOPY 0xcc0020u
#define DDWG_MAX_CLIP_RECTS 64u
#define DDPF_RGB 0x40u
#define BI_RGB 0u
#define BI_BITFIELDS 3u
#define DIB_RGB_COLORS 0u
#define DDWG_TRACE_CHECKPOINT(...)
#define DDWG_TRACE(...)
#define HOSTLOG_REFUSED(...)
#define DDWG_TRACE_RESULT(x) (x)
#define DDERR_SURFACELOST ((HRESULT)0x887601c2u)
#define DDWG_TRACE_ERROR(x) (x)
#define UNSUPPORTED(s,x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
static unsigned checks,blits,presents,readbacks,bindings,fail_emit,fail_present,fail_read,fail_dc,fail_native,dc_creates;static RECT clip,last_rect;static BOOL clipped;static HRESULT clip_error;static UINT clip_override_count;static RECT clip_override[2];static BOOL capture;static UINT captured_count;static struct{RECT dst,src;DWORD flags,fill;BOOL source;}blit_captured[2];static DDSurface*last_source;static DWORD last_flags,last_fill;static float last_depth;static BYTE dib[256];static BITMAPINFO captured;
static HRESULT surface_IsLost(void*p){(void)p;return 0;}
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}
static BOOL format_is_yuv(DWORD f){return f==DDWG_FMT_YUY2||f==DDWG_FMT_UYVY;}
static BOOL format_is_compressed(DWORD f){return f==DDWG_FMT_DXT1;}
static BOOL format_is_depth(DWORD f){return f==70||f==71||f==73||f==75||f==77||f==79||f==80;}
static void emit_surface_palette_binding(DDSurface*s){(void)s;bindings++;}
static unsigned key_count;static struct{DDSurface*surface;DWORD kind,low,high;BOOL enabled;}keys[16];
static void emit_color_key(DDSurface*s,DWORD k,const DDCOLORKEY*c,BOOL enabled){if(key_count<16){keys[key_count].surface=s;keys[key_count].kind=k;keys[key_count].low=c?c->dwColorSpaceLowValue:0;keys[key_count].high=c?c->dwColorSpaceHighValue:0;keys[key_count].enabled=enabled;}key_count++;}
static HRESULT clipper_blt_rects(void*c,RECT*r,UINT n,UINT*count){(void)c;if(clip_override_count){*count=clip_override_count<n?clip_override_count:n;memcpy(r,clip_override,*count*sizeof(RECT));}else{if(clipped)*r=clip;*count=clipped;}return clip_error;}
static LONG MulDiv(LONG a,LONG b,LONG c){return (LONG)((int64_t)a*b/c);}
static BOOL emit_blt(DDSurface*d,const RECT*dr,DDSurface*s,const RECT*sr,DWORD flags,DWORD fill,float depth){(void)d;last_depth=depth;blits++;last_rect=*dr;last_source=s;last_flags=flags;last_fill=fill;if(capture&&captured_count<2){blit_captured[captured_count].dst=*dr;blit_captured[captured_count].src=sr?*sr:(RECT){0};blit_captured[captured_count].flags=flags;blit_captured[captured_count].fill=fill;blit_captured[captured_count].source=!!s;captured_count++;}return !fail_emit;}
static BOOL surface_readback(DDSurface*s){(void)s;readbacks++;return !fail_read;}
static BOOL present_changed_primary(DDSurface*s,const RECT*r){(void)s;(void)r;presents++;return !fail_present;}
static HDC GetDC(void*p){(void)p;return fail_dc==1?NULL:(void*)1;}
static HDC CreateCompatibleDC(HDC p){(void)p;return fail_dc==2?NULL:(void*)2;}
static void ReleaseDC(void*a,HDC b){(void)a;(void)b;}
static HBITMAP CreateDIBSection(HDC d,const BITMAPINFO*i,UINT usage,void**bits,void*section,DWORD offset){(void)d;(void)usage;(void)section;(void)offset;captured.bmiHeader=i->bmiHeader;memcpy(captured.bmiColors,i->bmiColors,i->bmiHeader.biBitCount==8?1024:12);dc_creates++;*bits=dib;return fail_dc==3?NULL:(void*)3;}
static HBITMAP SelectObject(HDC d,HBITMAP b){(void)d;(void)b;return (void*)4;}
static void DeleteDC(HDC d){(void)d;}
${extract("rect_intersect")}
${extract("surface_depth_mask")}
/* Canonical-format fixture; explicit client masks are covered by depth_transfer. */
static DWORD surface_client_depth_mask(DDSurface*s){return surface_depth_mask(s->format);}
${extract("surface_depth_fill_value")}
${extract("clipped_blt_source_rect")}
${extract("surface_BltForVersion")}
${extract("surface_Blt")}
${bltViews}
static HRESULT blit_view(unsigned v,IDirectDrawSurface7*d,RECT*dr,IDirectDrawSurface7*s,RECT*sr,DWORD f,DDBLTFX*fx){switch(v){case 1:return view1_Blt(d,dr,s,sr,f,fx);case 2:return view2_Blt(d,dr,s,sr,f,fx);case 3:return view3_Blt(d,dr,s,sr,f,fx);case 4:return view4_Blt(d,dr,s,sr,f,fx);default:return surface_Blt(d,dr,s,sr,f,fx);}}
${extract("surface_BltFast")}
typedef struct{HDC dc;}DDrawNativeDC;static DDrawNativeDC native_dc={(void*)2};
static DDrawNativeDC*ddraw_gdi_dc_create(DDSurface*s){(void)s;return fail_native?NULL:&native_dc;}
${extract("surface_GetDC")}
static HRESULT surface_finish_dc(DDSurface*s,BOOL present){(void)present;s->dc=NULL;return DD_OK;}
${extract("surface_ReleaseDC")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw ROP/DC line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static int key_versions(void){
 Memory m={0};DDrawObject owner={0};DDSurface d={.width=6,.height=1,.format=21,.memory=&m,.owner=&owner},s=d;DDBLTFX fx={.ddckSrcColorkey={0x110000,0x110000},.ddckDestColorkey={0x1100,0x1100}};clipped=FALSE;clip_error=0;
 d.color_key_present=(1u<<D9WG_DDCKEY_DESTINATION_BLT);d.color_key[D9WG_DDCKEY_DESTINATION_BLT]=(DDCOLORKEY){0xff0000,0xff0000};s.color_key[D9WG_DDCKEY_DESTINATION_BLT]=(DDCOLORKEY){0xaa,0xaa};
 unsigned versions[]={1,2,3,4,7};
 for(unsigned p=0;p<2;p++)for(unsigned v=0;v<5;v++){
  s.color_key_present=p?(1u<<D9WG_DDCKEY_DESTINATION_BLT):0;DDSurface before_d=d,before_s=s;DDBLTFX before_fx=fx;key_count=0;
  CHECK(!blit_view(versions[v],&d,NULL,&s,NULL,DDBLT_KEYDEST,&fx));CHECK(key_count==(versions[v]<4?2:1));CHECK(keys[0].surface==&d&&keys[0].kind==D9WG_DDCKEY_DESTINATION_BLT&&keys[0].low==(versions[v]<4?0xaa:0xff0000)&&keys[0].high==keys[0].low&&keys[0].enabled);
  CHECK(keys[key_count-1].low==0xff0000&&keys[key_count-1].enabled);CHECK(!memcmp(&d,&before_d,sizeof(d))&&!memcmp(&s,&before_s,sizeof(s))&&!memcmp(&fx,&before_fx,sizeof(fx)));
  fail_emit=TRUE;key_count=0;CHECK(blit_view(versions[v],&d,NULL,&s,NULL,DDBLT_KEYDEST,&fx)==DDERR_GENERIC);CHECK(key_count==(versions[v]<4?2:1)&&keys[key_count-1].low==0xff0000&&keys[key_count-1].enabled);CHECK(!memcmp(&d,&before_d,sizeof(d))&&!memcmp(&s,&before_s,sizeof(s))&&!memcmp(&fx,&before_fx,sizeof(fx)));fail_emit=FALSE;
 }
 DWORD conflicts[]={DDBLT_KEYSRC|DDBLT_KEYSRCOVERRIDE,DDBLT_KEYDEST|DDBLT_KEYDESTOVERRIDE};for(unsigned i=0;i<2;i++){unsigned b=blits;key_count=0;CHECK(blit_view(1,&d,NULL,&s,NULL,conflicts[i],&fx)==DDERR_INVALIDPARAMS&&blits==b&&!key_count);}
 s.color_key_present=0;unsigned b=blits;key_count=0;CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_KEYSRC,&fx)==DDERR_INVALIDPARAMS&&blits==b&&!key_count);s.color_key_present=1u<<D9WG_DDCKEY_SOURCE_BLT;d.color_key_present=0;key_count=0;CHECK(blit_view(1,&d,NULL,&s,NULL,DDBLT_KEYDEST,&fx)==DDERR_INVALIDPARAMS&&blits==b&&!key_count);CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_KEYSRC|DDBLT_KEYDEST,&fx)==DDERR_INVALIDPARAMS&&blits==b&&!key_count);return 0;
}
static int raster(void){
 Memory memory={0};BYTE shadow[256]={0};DDrawObject owner={0};DDSurface d={.width=8,.height=8,.pitch=32,.bytes_per_pixel=4,.format=21,.memory=&memory,.shadow=shadow,.owner=&owner},s=d;s.lock_depth=1;
 const DWORD masks[4][3]={{0,0,0},{0xf800,0x7e0,0x1f},{0x7c00,0x3e0,0x1f},{0xff0000,0xff00,0xff}},white[4]={255,65535,32767,0xffffff};
 for(unsigned round=0;round<64;round++)for(unsigned fmt=0;fmt<4;fmt++)for(unsigned rop=0;rop<2;rop++)for(unsigned supplied=0;supplied<2;supplied++){
  d.indexed=fmt==0;d.bytes_per_pixel=fmt==0?1:fmt==3?4:2;d.desc.ddpfPixelFormat=(PF){DDPF_RGB,masks[fmt][0],masks[fmt][1],masks[fmt][2]};DDBLTFX fx={.dwROP=rop?WHITENESS:BLACKNESS,.dwFillColor=0xdeadbeef};DDBLTFX before=fx;RECT area={1,1,7,7},input=area;clipped=round&1;clip=(RECT){2,3,6,5};unsigned b=blits,p=presents,r=readbacks,k=bindings;d.external_memory=!!(round&2);CHECK(!surface_Blt(&d,&area,supplied?&s:NULL,&area,DDBLT_ROP,&fx));CHECK(blits==b+1&&presents==p+1&&readbacks==r+d.external_memory&&bindings==k);CHECK(!last_source&&last_flags==D9WG_DDBLT_COLOR_FILL&&last_fill==(rop?white[fmt]:0));CHECK(!memcmp(&fx,&before,sizeof(fx))&&!memcmp(&area,&input,sizeof(area)));CHECK(!memcmp(&last_rect,clipped?&clip:&area,sizeof(area)));
 }
 clipped=0;d.external_memory=0;DDBLTFX fx={.dwROP=SRCCOPY};unsigned b=blits;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_INVALIDPARAMS&&blits==b);CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,&fx)==DDERR_SURFACEBUSY);s.lock_depth=0;CHECK(!surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,&fx)&&last_source==&s&&!last_flags);b=blits;CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,NULL)==DDERR_INVALIDPARAMS&&blits==b);for(unsigned i=0;i<32;i++){fx.dwROP=0x1000+i;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_NORASTEROPHW&&blits==b);}fx.dwROP=BLACKNESS;d.lock_depth=1;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_SURFACEBUSY);d.lock_depth=0;fail_emit=1;unsigned p=presents;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC&&presents==p);fail_emit=0;fail_present=1;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC);fail_present=0;d.external_memory=1;fail_read=1;p=presents;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC&&presents==p);fail_read=0;return 0;
}
static int fills(void){
 Memory m={0};DDrawObject owner={0};DDSurface d={.width=8,.height=8,.format=21,.memory=&m,.owner=&owner},s=d;DDBLTFX fx={.dwFillColor=0xdeadbeef,.dwFillDepth=0xdeadbeef,.dwROP=WHITENESS};clipped=0;
 const DWORD formats[]={70,71,73,75,77,79,80},mask[]={0xffff,0xffffffff,0xfffe,0xffffff00,0xffffff00,0xffffff00,0xffff};
 for(unsigned i=0;i<7;i++){
  d.format=formats[i];unsigned b=blits;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==DDERR_INVALIDPARAMS&&blits==b);CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_DEPTHFILL,&fx)==DDERR_INVALIDPARAMS&&blits==b);
  CHECK(!surface_Blt(&d,NULL,NULL,NULL,DDBLT_DEPTHFILL,&fx)&&last_flags==D9WG_DDBLT_DEPTH_FILL&&last_fill==0xdeadbeef&&!last_source);DWORD shift=i==2?1:(i>=3&&i<=5)?8:0;float expected=(float)((fx.dwFillDepth&mask[i])>>shift)/(float)(mask[i]>>shift);CHECK(last_depth==expected);
  CHECK(!surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)&&last_flags==D9WG_DDBLT_DEPTH_FILL&&last_fill==mask[i]&&last_depth==1.0f);fx.dwROP=BLACKNESS;CHECK(!surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)&&!last_fill&&last_depth==0.0f);fx.dwROP=WHITENESS;
 }
 for(unsigned kind=0;kind<2;kind++){
  d.format=kind?80:21;DWORD flags=kind?DDBLT_DEPTHFILL:DDBLT_COLORFILL;unsigned b=blits;CHECK(surface_Blt(&d,NULL,NULL,NULL,flags,NULL)==DDERR_INVALIDPARAMS&&blits==b);CHECK(surface_Blt(&d,NULL,&s,NULL,flags,&fx)==DDERR_INVALIDPARAMS&&blits==b);
  RECT invalid[]={{-1,0,8,8},{0,-1,8,8},{0,0,9,8},{0,0,8,9},{4,0,4,8},{0,5,8,4}};
  for(unsigned i=0;i<6;i++){CHECK(surface_Blt(&d,&invalid[i],NULL,NULL,flags,&fx)==DDERR_INVALIDRECT&&blits==b);CHECK(!surface_Blt(&d,NULL,NULL,&invalid[i],flags,&fx));b=blits;CHECK(surface_Blt(&d,NULL,&s,&invalid[i],DDBLT_ROP,&fx)==DDERR_INVALIDRECT&&blits==b);}
 }
 d.format=80;s.format=21;unsigned before_depth=blits;CHECK(surface_Blt(&d,NULL,&s,NULL,0,NULL)==DDERR_INVALIDPARAMS&&blits==before_depth);s.format=71;CHECK(surface_Blt(&d,NULL,&s,NULL,0,NULL)==DDERR_INVALIDPIXELFORMAT&&blits==before_depth);s.format=80;CHECK(!surface_Blt(&d,NULL,&s,NULL,0,NULL)&&last_source==&s);d.format=21;CHECK(surface_Blt(&d,NULL,&s,NULL,0,NULL)==DDERR_INVALIDPARAMS);s.format=21;
 for(unsigned i=0;i<3;i++){d.format=i==0?DDWG_FMT_YUY2:i==1?DDWG_FMT_UYVY:DDWG_FMT_DXT1;unsigned b=blits;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_NORASTEROPHW&&blits==b);CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==(i==2?E_NOTIMPL:DD_OK));}
 d.format=21;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_DEPTHFILL,&fx)==DDERR_INVALIDPARAMS);CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_ROP,&fx)==DDERR_INVALIDPARAMS);CHECK(!surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_DEPTHFILL,&fx));d.format=80;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_DEPTHFILL,&fx)==DDERR_INVALIDPARAMS);return 0;
}
static int transfers(void){
 Memory m={0};DDrawObject a={0},b={0};DDSurface d={.width=8,.height=8,.format=22,.memory=&m,.owner=&a},s=d;s.owner=&b;s.format=24;clipped=FALSE;clip_error=0;
 unsigned old=blits,keys=bindings,p=presents,r=readbacks;CHECK(surface_Blt(&d,NULL,&s,NULL,0,NULL)==E_NOTIMPL);CHECK(surface_BltFast(&d,0,0,&s,NULL,0)==E_NOTIMPL);CHECK(blits==old&&bindings==keys&&presents==p&&readbacks==r);s.format=22;CHECK(!surface_Blt(&d,NULL,&s,NULL,0,NULL)&&blits==old+1);CHECK(!surface_BltFast(&d,0,0,&s,NULL,0)&&blits==old+2);s.owner=&a;s.format=24;CHECK(!surface_Blt(&d,NULL,&s,NULL,0,NULL)&&blits==old+3);s.format=22;
 d.clipper=(void*)1;old=blits;CHECK(surface_BltFast(&d,0,0,&s,NULL,0)==DDERR_BLTFASTCANTCLIP&&blits==old);DDBLTFX fx={.dwFillColor=7};clip_error=DDERR_NOCLIPLIST;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==DDERR_NOCLIPLIST&&blits==old);clip_error=DDERR_GENERIC;CHECK(surface_Blt(&d,NULL,&s,NULL,0,NULL)==DDERR_GENERIC&&blits==old);clip_error=0;p=presents;CHECK(!surface_Blt(&d,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)&&blits==old&&presents==p+1);d.clipper=NULL;CHECK(!surface_BltFast(&d,0,0,&s,NULL,0)&&blits==old+1);d.format=s.format=DDWG_FMT_DXT1;old=blits;CHECK(surface_Blt(&d,NULL,&s,NULL,0x100000u,NULL)==DDERR_GENERIC&&blits==old);CHECK(surface_BltFast(&d,0,0,&s,NULL,0x100000u)==DDERR_GENERIC&&blits==old);CHECK(!surface_Blt(&d,NULL,&s,NULL,DDBLT_WAIT|DDBLT_ASYNC,NULL)&&blits==old+1);CHECK(!surface_BltFast(&d,0,0,&s,NULL,DDBLTFAST_WAIT)&&blits==old+2);return 0;
}
static int contexts(void){
 DDPalette own,primary;Memory memory={0};BYTE shadow[256];memset(shadow,0x41,sizeof(shadow));DDSurface front={.palette=&primary};DDrawObject owner={.primary=&front};DDSurface d={.width=8,.height=8,.pitch=8,.bytes_per_pixel=1,.indexed=1,.memory=&memory,.shadow=shadow,.owner=&owner};
 for(unsigned round=0;round<64;round++)for(unsigned mode=0;mode<4;mode++){
  for(unsigned i=0;i<256;i++){own.entries[i]=(PALETTEENTRY){(BYTE)(round+i),(BYTE)(round+2*i),(BYTE)(round+3*i),99};primary.entries[i]=(PALETTEENTRY){(BYTE)(round+4*i),(BYTE)(round+5*i),(BYTE)(round+6*i),77};}d.palette=mode==0?&own:NULL;d.owner=mode==3?NULL:&owner;front.palette=mode==2?NULL:&primary;d.dc=NULL;HDC dc=(void*)9;CHECK(!surface_GetDC(&d,&dc)&&dc==(void*)2&&d.dc==dc);CHECK(captured.bmiHeader.biWidth==8&&captured.bmiHeader.biHeight==-8&&captured.bmiHeader.biBitCount==8&&captured.bmiHeader.biClrUsed==256);for(unsigned i=0;i<256;i++){PALETTEENTRY want=mode==0?own.entries[i]:mode==1?primary.entries[i]:(PALETTEENTRY){0};RGBQUAD got=captured.bmiColors[i];CHECK(got.rgbRed==want.peRed&&got.rgbGreen==want.peGreen&&got.rgbBlue==want.peBlue&&!got.rgbReserved);}for(unsigned y=0;y<8;y++)CHECK(!memcmp(d.dib_bits+y*8,shadow+y*8,8));CHECK(d.palette==(mode==0?&own:NULL));
 }
 HDC out=(void*)9;d.dc=NULL;CHECK(surface_ReleaseDC(&d,NULL)==DDERR_NODC);d.dc=(void*)2;CHECK(surface_ReleaseDC(&d,(void*)1)==DDERR_INVALIDPARAMS&&d.dc==(void*)2);CHECK(!surface_ReleaseDC(&d,(void*)2)&&!d.dc);CHECK(surface_ReleaseDC(&d,(void*)2)==DDERR_NODC);d.owner=&owner;d.bytes_per_pixel=0;CHECK(surface_GetDC(&d,&out)==DDERR_UNSUPPORTED&&!out);out=(void*)9;d.bytes_per_pixel=1;d.indexed=1;CHECK(surface_GetDC(&d,NULL)==DDERR_INVALIDPARAMS);d.lock_depth=1;CHECK(!surface_GetDC(&d,&out)&&out==(void*)2&&d.lock_depth==1);d.dc=NULL;out=(void*)9;d.lock_depth=0;d.dc=(void*)1;CHECK(surface_GetDC(&d,&out)==DDERR_DCALREADYCREATED&&out==(void*)9);d.dc=NULL;for(fail_dc=1;fail_dc<=3;fail_dc++){CHECK(surface_GetDC(&d,&out)==DDERR_GENERIC&&!d.dc&&out==(void*)9);}fail_dc=0;memory.gpu_dirty=1;fail_read=1;unsigned r=readbacks;CHECK(surface_GetDC(&d,&out)==DDERR_GENERIC&&!d.dc&&readbacks==r+1);fail_read=0;d.dc=NULL;d.bytes_per_pixel=2;d.indexed=0;d.pitch=16;d.desc.ddpfPixelFormat=(PF){DDPF_RGB,0xf800,0x7e0,0x1f};CHECK(!surface_GetDC(&d,&out)&&out==(void*)2&&d.gdi_dc==&native_dc);d.dc=NULL;d.gdi_dc=NULL;fail_native=1;out=(void*)9;CHECK(surface_GetDC(&d,&out)==DDERR_CANTCREATEDC&&!out&&!d.dc&&!d.gdi_dc);fail_native=0;for(unsigned kind=0;kind<3;kind++){d.bytes_per_pixel=kind==2?2:1;d.desc.ddpfPixelFormat.dwFlags=kind==0?DDPF_RGB:0;out=(void*)9;CHECK(surface_GetDC(&d,&out)==DDERR_UNSUPPORTED&&!out&&!d.dc);}return 0;
}
static int fractional(void){
 RECT edge_source={1,1,4,4},edge_destination={0,0,21,21},edge_bits;float edge[4];
 clipped_blt_source_rect(&edge_source,&edge_destination,&edge_destination,D9WG_DDBLT_MIRROR_X|D9WG_DDBLT_MIRROR_Y,&edge_bits);memcpy(edge,&edge_bits,sizeof(edge));
 CHECK(edge[0]==1&&edge[1]==1&&edge[2]==4&&edge[3]==4);
 Memory m={0};DDrawObject owner={0};DDSurface d={.width=128,.height=96,.format=22,.memory=&m,.owner=&owner},src={.width=6,.height=3,.format=22,.memory=&m,.owner=&owner};
 for(UINT scene=0;scene<8;scene++){
  clipped=FALSE;clip_error=0;clip_override_count=scene==3?0:scene==2?1:2;clip_override[0]=scene>=4?(RECT){0,0,71,43}:scene==2?(RECT){32,48,64,64}:(RECT){0,0,128,32};clip_override[1]=scene>=4?(RECT){37,43,128,96}:(RECT){64,32,128,96};d.clipper=scene==3?NULL:(void*)1;src.width=scene>=4?8:6;src.height=scene>=4?8:3;
  RECT source={scene>=4?0:1,scene>=4?0:1,scene>=4?6:5,scene>=4?5:2},destination=scene>=4?(RECT){4,5,124,91}:(RECT){0,0,128,96},source_before=source,destination_before=destination;UINT mirror=scene==1?1:scene>=4?scene-4:0;DDBLTFX fx={.dwFillColor=0xff0000,.dwDDFX=((mirror&1)?DDBLTFX_MIRRORLEFTRIGHT:0)|((mirror&2)?DDBLTFX_MIRRORUPDOWN:0)};capture=TRUE;captured_count=0;
  CHECK(!surface_Blt(&d,&destination,scene==2?NULL:&src,&source,scene==2?DDBLT_COLORFILL:mirror?DDBLT_DDFX:0,&fx));capture=FALSE;CHECK(captured_count==(scene==2||scene==3?1:2));CHECK(!memcmp(&source,&source_before,sizeof(source))&&!memcmp(&destination,&destination_before,sizeof(destination)));
  for(UINT n=0;n<captured_count;n++){UINT bits[4];memcpy(bits,&blit_captured[n].src,16);if(blit_captured[n].source&&scene!=3){float rect[4];memcpy(rect,bits,16);CHECK((blit_captured[n].flags&D9WG_DDBLT_SOURCE_RECT_F32)&&rect[0]<rect[2]&&rect[1]<rect[3]);CHECK(rect[0]>=source.left&&rect[1]>=source.top&&rect[2]<=source.right&&rect[3]<=source.bottom);if(scene<2)CHECK(rect[1]>=1&&rect[3]<=2);UINT wanted=D9WG_DDBLT_SOURCE_RECT_F32|((mirror&1)?D9WG_DDBLT_MIRROR_X:0)|((mirror&2)?D9WG_DDBLT_MIRROR_Y:0);CHECK((blit_captured[n].flags&(D9WG_DDBLT_SOURCE_RECT_F32|D9WG_DDBLT_MIRROR_X|D9WG_DDBLT_MIRROR_Y))==wanted);double mx=(blit_captured[n].dst.left+blit_captured[n].dst.right)*0.5,my=(blit_captured[n].dst.top+blit_captured[n].dst.bottom)*0.5;double ex=source.left+(mx-destination.left)*(source.right-source.left)/(destination.right-destination.left),ey=source.top+(my-destination.top)*(source.bottom-source.top)/(destination.bottom-destination.top);if(mirror&1)ex=source.left+source.right-ex;if(mirror&2)ey=source.top+source.bottom-ey;double dx=(rect[0]+rect[2])*0.5-ex,dy=(rect[1]+rect[3])*0.5-ey;CHECK(dx>=-0.00002&&dx<=0.00002&&dy>=-0.00002&&dy<=0.00002);}else CHECK(!(blit_captured[n].flags&D9WG_DDBLT_SOURCE_RECT_F32));printf("CLIPPED %u %u %u %u %u %u %u %u %d %d %d %d %u %u\\n",scene,n,blit_captured[n].flags,bits[0],bits[1],bits[2],bits[3],blit_captured[n].fill,blit_captured[n].dst.left,blit_captured[n].dst.top,blit_captured[n].dst.right,blit_captured[n].dst.bottom,blit_captured[n].source,mirror);}
 }
 clip_override_count=0;d.clipper=NULL;return 0;
}
int main(void){CHECK(!key_versions());CHECK(!raster());CHECK(!fills());CHECK(!contexts());CHECK(!transfers());CHECK(!fractional());printf("PASS actual DDraw ROP/DC C: %u checks, 1024 source-independent raster fills, seven depth fill layouts, incompatible formats/sources/flags, invalid rectangle isolation, 256 full DC palette snapshots, own/primary/none precedence, clipping, caller guards and accepted failure prefixes\\n",checks);return 0;}
`;
try {const file = dir + "/rop.c", binary = dir + "/rop"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); if(process.argv.includes("--write-fixture")){const rows=[...run.stdout.matchAll(/^CLIPPED ([0-9 ]+)$/gm)].map(m=>m[1].split(" ").map(Number));assert.equal(rows.length,14);const scenes=Array.from({length:8},(_,index)=>({index,label:index<2?"native-stretch-"+index:index===2?"native-window-fill":index===3?"legacy-int-source":"fractional-mirror-"+(index-4),sourceWidth:index>=4?8:6,sourceHeight:index>=4?8:3,sourceRect:index>=4?[0,0,6,5]:[1,1,5,2],destinationRect:index>=4?[4,5,124,91]:[0,0,128,96],clips:index===3?null:index>=4?[[0,0,71,43],[37,43,128,96]]:index===2?[[32,48,64,64]]:[[0,0,128,32],[64,32,128,96]],mirror:index===1?1:index>=4?index-4:0,fill:index===2,commands:rows.filter(r=>r[0]===index).map(r=>({flags:r[2],sourceBits:r.slice(3,7).map(v=>v|0),fill:r[7],destinationRect:r.slice(8,12),source:!!r[12]})),nativePixels:nativeClipping.pixels.filter(p=>p.case===index)}));const sha=p=>require("node:crypto").createHash("sha256").update(fs.readFileSync(p)).digest("hex");fs.writeFileSync(path.resolve(root,"../../v86/tests/glbridge/fixtures/ddraw_clipped_packets.json"),JSON.stringify({description:"Actual public C Blt clipped packets and independent original XP pixel controls; synthetic full-frame oracle is evaluated by browser.",frontSourceSha256:sha(root+"/ddrawproxy/ddraw_proxy.c"),producerSha256:sha(__filename),protocolSha256:sha(root+"/d3d9proxy/d3d9_protocol.h"),nativeReferenceSha256:sha(path.resolve(root,"../../v86/tests/glbridge/fixtures/ddraw_native_clipping.json")),scenes},null,2)+"\n");}process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
