"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.DDRAW_ROP_DC_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-rop-dc-"));
function extract(name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} return source.slice(m.index, end);}
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
struct DDSurface{DWORD handle,width,height,pitch,bytes_per_pixel,format,lock_depth,color_key_present;DDCOLORKEY color_key[4];BOOL indexed,external_memory;DDPalette*palette;DDrawObject*owner;Memory*memory;void*clipper;BYTE*shadow,*dib_bits;HDC dc;HBITMAP dib,old_bitmap;struct{PF ddpfPixelFormat;}desc;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
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
#define DDWG_TRACE_ERROR(x) (x)
#define UNSUPPORTED(s,x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
static unsigned checks,blits,presents,readbacks,bindings,fail_emit,fail_present,fail_read,fail_dc,dc_creates;static RECT clip,last_rect;static BOOL clipped;static DDSurface*last_source;static DWORD last_flags,last_fill;static BYTE dib[256];static BITMAPINFO captured;
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}
static BOOL format_is_depth(DWORD f){return f==DDWG_FMT_D16;}
static void emit_surface_palette_binding(DDSurface*s){(void)s;bindings++;}
static void emit_color_key(DDSurface*s,DWORD k,DDCOLORKEY*c,BOOL enabled){(void)s;(void)k;(void)c;(void)enabled;}
static UINT clipper_rects(void*c,RECT*r,UINT n){(void)c;(void)n;if(clipped)*r=clip;return clipped;}
static LONG MulDiv(LONG a,LONG b,LONG c){return (LONG)((int64_t)a*b/c);}
static BOOL emit_blt(DDSurface*d,const RECT*dr,DDSurface*s,const RECT*sr,DWORD flags,DWORD fill,float depth){(void)d;(void)sr;(void)depth;blits++;last_rect=*dr;last_source=s;last_flags=flags;last_fill=fill;return !fail_emit;}
static BOOL surface_readback(DDSurface*s){(void)s;readbacks++;return !fail_read;}
static BOOL present_changed_primary(DDSurface*s,const RECT*r){(void)s;(void)r;presents++;return !fail_present;}
static HDC GetDC(void*p){(void)p;return fail_dc==1?NULL:(void*)1;}
static HDC CreateCompatibleDC(HDC p){(void)p;return fail_dc==2?NULL:(void*)2;}
static void ReleaseDC(void*a,HDC b){(void)a;(void)b;}
static HBITMAP CreateDIBSection(HDC d,const BITMAPINFO*i,UINT usage,void**bits,void*section,DWORD offset){(void)d;(void)usage;(void)section;(void)offset;captured.bmiHeader=i->bmiHeader;memcpy(captured.bmiColors,i->bmiColors,i->bmiHeader.biBitCount==8?1024:12);dc_creates++;*bits=dib;return fail_dc==3?NULL:(void*)3;}
static HBITMAP SelectObject(HDC d,HBITMAP b){(void)d;(void)b;return (void*)4;}
static void DeleteDC(HDC d){(void)d;}
${extract("rect_intersect")}
${extract("surface_Blt")}
${extract("surface_GetDC")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw ROP/DC line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static int raster(void){
 Memory memory={0};BYTE shadow[256]={0};DDrawObject owner={0};DDSurface d={.width=8,.height=8,.pitch=32,.bytes_per_pixel=4,.format=21,.memory=&memory,.shadow=shadow,.owner=&owner},s=d;s.lock_depth=1;
 const DWORD masks[4][3]={{0,0,0},{0xf800,0x7e0,0x1f},{0x7c00,0x3e0,0x1f},{0xff0000,0xff00,0xff}},white[4]={255,65535,32767,0xffffff};
 for(unsigned round=0;round<64;round++)for(unsigned fmt=0;fmt<4;fmt++)for(unsigned rop=0;rop<2;rop++)for(unsigned supplied=0;supplied<2;supplied++){
  d.indexed=fmt==0;d.bytes_per_pixel=fmt==0?1:fmt==3?4:2;d.desc.ddpfPixelFormat=(PF){DDPF_RGB,masks[fmt][0],masks[fmt][1],masks[fmt][2]};DDBLTFX fx={.dwROP=rop?WHITENESS:BLACKNESS,.dwFillColor=0xdeadbeef};DDBLTFX before=fx;RECT area={1,1,7,7},input=area;clipped=round&1;clip=(RECT){2,3,6,5};unsigned b=blits,p=presents,r=readbacks,k=bindings;d.external_memory=!!(round&2);CHECK(!surface_Blt(&d,&area,supplied?&s:NULL,&area,DDBLT_ROP,&fx));CHECK(blits==b+1&&presents==p+1&&readbacks==r+d.external_memory&&bindings==k);CHECK(!last_source&&last_flags==D9WG_DDBLT_COLOR_FILL&&last_fill==(rop?white[fmt]:0));CHECK(!memcmp(&fx,&before,sizeof(fx))&&!memcmp(&area,&input,sizeof(area)));CHECK(!memcmp(&last_rect,clipped?&clip:&area,sizeof(area)));
 }
 clipped=0;d.external_memory=0;DDBLTFX fx={.dwROP=SRCCOPY};unsigned b=blits;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_INVALIDPARAMS&&blits==b);CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,&fx)==DDERR_SURFACEBUSY);s.lock_depth=0;CHECK(!surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,&fx)&&last_source==&s&&!last_flags);b=blits;CHECK(surface_Blt(&d,NULL,&s,NULL,DDBLT_ROP,NULL)==DDERR_INVALIDPARAMS&&blits==b);for(unsigned i=0;i<32;i++){fx.dwROP=0x1000+i;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_NORASTEROPHW&&blits==b);}fx.dwROP=BLACKNESS;d.lock_depth=1;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_SURFACEBUSY);d.lock_depth=0;fail_emit=1;unsigned p=presents;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC&&presents==p);fail_emit=0;fail_present=1;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC);fail_present=0;d.external_memory=1;fail_read=1;p=presents;CHECK(surface_Blt(&d,NULL,NULL,NULL,DDBLT_ROP,&fx)==DDERR_GENERIC&&presents==p);fail_read=0;return 0;
}
static int contexts(void){
 DDPalette own,primary;Memory memory={0};BYTE shadow[256];memset(shadow,0x41,sizeof(shadow));DDSurface front={.palette=&primary};DDrawObject owner={.primary=&front};DDSurface d={.width=8,.height=8,.pitch=8,.bytes_per_pixel=1,.indexed=1,.memory=&memory,.shadow=shadow,.owner=&owner};
 for(unsigned round=0;round<64;round++)for(unsigned mode=0;mode<4;mode++){
  for(unsigned i=0;i<256;i++){own.entries[i]=(PALETTEENTRY){(BYTE)(round+i),(BYTE)(round+2*i),(BYTE)(round+3*i),99};primary.entries[i]=(PALETTEENTRY){(BYTE)(round+4*i),(BYTE)(round+5*i),(BYTE)(round+6*i),77};}d.palette=mode==0?&own:NULL;d.owner=mode==3?NULL:&owner;front.palette=mode==2?NULL:&primary;d.dc=NULL;HDC dc=(void*)9;CHECK(!surface_GetDC(&d,&dc)&&dc==(void*)2&&d.dc==dc);CHECK(captured.bmiHeader.biWidth==8&&captured.bmiHeader.biHeight==-8&&captured.bmiHeader.biBitCount==8&&captured.bmiHeader.biClrUsed==256);for(unsigned i=0;i<256;i++){PALETTEENTRY want=mode==0?own.entries[i]:mode==1?primary.entries[i]:(PALETTEENTRY){0};RGBQUAD got=captured.bmiColors[i];CHECK(got.rgbRed==want.peRed&&got.rgbGreen==want.peGreen&&got.rgbBlue==want.peBlue&&!got.rgbReserved);}for(unsigned y=0;y<8;y++)CHECK(!memcmp(d.dib_bits+y*8,shadow+y*8,8));CHECK(d.palette==(mode==0?&own:NULL));
 }
 HDC out=(void*)9;d.dc=NULL;d.owner=&owner;d.bytes_per_pixel=0;CHECK(surface_GetDC(&d,&out)==DDERR_UNSUPPORTED&&out==(void*)9);d.bytes_per_pixel=1;CHECK(surface_GetDC(&d,NULL)==DDERR_INVALIDPARAMS);d.lock_depth=1;CHECK(surface_GetDC(&d,&out)==DDERR_SURFACEBUSY&&out==(void*)9);d.lock_depth=0;d.dc=(void*)1;CHECK(surface_GetDC(&d,&out)==DDERR_DCALREADYCREATED&&out==(void*)9);d.dc=NULL;for(fail_dc=1;fail_dc<=3;fail_dc++){CHECK(surface_GetDC(&d,&out)==DDERR_GENERIC&&!d.dc&&out==(void*)9);}fail_dc=0;memory.gpu_dirty=1;fail_read=1;unsigned r=readbacks;CHECK(!surface_GetDC(&d,&out)&&readbacks==r+1);fail_read=0;d.dc=NULL;d.bytes_per_pixel=2;d.pitch=16;d.desc.ddpfPixelFormat=(PF){DDPF_RGB,0xf800,0x7e0,0x1f};CHECK(!surface_GetDC(&d,&out)&&captured.bmiHeader.biCompression==BI_BITFIELDS);DWORD masks[3];memcpy(masks,captured.bmiColors,12);CHECK(masks[0]==0xf800&&masks[1]==0x7e0&&masks[2]==0x1f);return 0;
}
int main(void){CHECK(!raster());CHECK(!contexts());printf("PASS actual DDraw ROP/DC C: %u checks, 1024 source-independent raster fills, 256 full DC palette snapshots, own/primary/none precedence, clipping, caller guards and accepted failure prefixes\\n",checks);return 0;}
`;
try {const file = dir + "/rop.c", binary = dir + "/rop"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
