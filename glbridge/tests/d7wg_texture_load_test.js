"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), current = ["ddraw_proxy.c", "d3d7_proxy.inc", "d3d_legacy_proxy.inc"].map(n => fs.readFileSync(root + "/ddrawproxy/" + n, "utf8")).join("\n"), source = process.env.D7WG_TEXTURE_LOAD_SOURCE ? fs.readFileSync(process.env.D7WG_TEXTURE_LOAD_SOURCE, "utf8") : current, dir = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-texture-load-"));
function extract(name) {const re = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{"); let input = source, m = re.exec(input); if (!m) {input = current; m = re.exec(input);} assert(m, name); let end = input.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (input[end] === "{") depth++; if (input[end] === "}") depth--;} return input.slice(m.index, end);}
const definitions = fs.readFileSync(__dirname + "/ddraw_color_key_test.js", "utf8").split("typedef uint32_t DWORD,UINT;")[1].split("typedef struct DDSurface DDSurface;")[0];
const code = `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;${definitions}
typedef int32_t LONG;typedef struct{LONG x,y;}POINT;typedef struct{LONG left,top,right,bottom;}RECT;
#define DDSCAPS2_MIPMAPSUBLEVEL 0x10000u
#define DDSCAPS2_CUBEMAP_POSITIVEX 0x400u
#define DDSCAPS2_CUBEMAP_ALLFACES 0xfc00u
#define DDSCAPS_ALLOCONLOAD 0x4000000u
#define DDERR_NOPALETTEATTACHED (-8)
#define DDERR_SURFACEBUSY (-9)
#define DDERR_GENERIC (-10)
#define D3DERR_TEXTURE_LOAD_FAILED (-11)
#define D3DERR_TEXTURE_BADSIZE (-12)
#define DDBLT_WAIT 1u
#define FAILED(x) ((x)<0)
#define D3D_OK 0
#define CopyMemory(d,s,n) memcpy(d,s,n)
typedef struct {DWORD value;}PALETTEENTRY;
typedef struct {PALETTEENTRY entries[256];}DDPalette,IDirectDrawPalette;
typedef struct {unsigned id;}DDrawObject;
typedef struct DDSurface DDSurface;
struct DDSurface{void*vtbl7;DDrawObject*owner;unsigned id;UINT width,height,format,mip_level,cube_face;DDSURFACEDESC2 desc;DDSurface*mip_next,*cube_faces[6];DDPalette*palette;DDCOLORKEY color_key[4];DWORD color_key_present,lock_depth;BOOL external_memory;};
typedef DDSurface IDirectDrawSurface7;
#define lpVtbl vtbl7
typedef struct {DDrawObject*owner;}D3D7Device,IDirect3DDevice7;
static unsigned checks,copies,palettes,keys,readbacks,fail_copy,fail_palette,fail_key,fail_readback;static int g_surface_vtbl;static DDrawObject owner={1},foreign_owner={2};
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"texture load line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
struct Copy {DDSurface*dst,*src;RECT d,s;};static struct Copy records[128];
static HRESULT palette_SetEntries(IDirectDrawPalette*p,DWORD flags,DWORD start,DWORD count,PALETTEENTRY*e){MUST(!flags&&!start&&count==256);palettes++;memmove(p->entries,e,sizeof(p->entries));return fail_palette?DDERR_GENERIC:0;}
${extract("surface_color_key_kind")}
static HRESULT surface_SetColorKey(IDirectDrawSurface7*d,DWORD flag,LPDDCOLORKEY key){DWORD kind,description;MUST(surface_color_key_kind(flag,&kind,&description));keys++;if(fail_key&&keys==fail_key)return DDERR_GENERIC;d->color_key[kind]=*key;d->color_key_present|=1u<<kind;return 0;}
static BOOL emit_blt(DDSurface*d,const RECT*dr,DDSurface*s,const RECT*sr,DWORD flags,DWORD fill,float depth){MUST(!flags&&!fill&&depth==0&&copies<128);records[copies++]=(struct Copy){d,s,*dr,*sr};return !fail_copy||copies!=fail_copy;}
static BOOL surface_readback(DDSurface*d){MUST(d->external_memory);readbacks++;return !fail_readback;}
static DDSurface*surface_from_any(IDirectDrawSurface7*p,void*v){(void)v;return p;}static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static HRESULT surface_Blt(IDirectDrawSurface7*d,RECT*dr,IDirectDrawSurface7*s,RECT*sr,DWORD flags,void*fx){(void)flags;(void)fx;RECT r=sr?*sr:(RECT){0,0,(LONG)s->width,(LONG)s->height},out=dr?*dr:(RECT){0,0,(LONG)d->width,(LONG)d->height};return emit_blt(d,&out,s,&r,0,0,0)?0:DDERR_GENERIC;}
${["d3d_texture_copy_palette", "d3d_texture_copy_keys", "d3d_texture_copy_region", "d3d_texture_mip_subset", "d3d_texture_next_coordinates", "d3d_texture_load_mip_subset", "d3d_texture_load_face", "d3d_texture_load7", "d3d_texture_load_legacy", "d3d7_device_Load", "d3d_legacy_texture_load_surface"].map(extract).join("\n")}
static void reset(void){copies=palettes=keys=readbacks=fail_copy=fail_palette=fail_key=fail_readback=0;memset(records,0,sizeof(records));}
static void chain(DDSurface*s,unsigned width,unsigned height,unsigned levels,unsigned face,BOOL cube){memset(s,0,sizeof(*s)*9);for(unsigned i=0;i<levels;i++){s[i].vtbl7=&g_surface_vtbl;s[i].owner=&owner;s[i].width=width>>i?width>>i:1;s[i].height=height>>i?height>>i:1;s[i].format=21;s[i].id=face*10+i;s[i].mip_level=i;s[i].cube_face=face;s[i].desc.ddsCaps.dwCaps=DDSCAPS_ALLOCONLOAD;s[i].desc.ddsCaps.dwCaps2=(cube?DDSCAPS2_CUBEMAP:0)|(i?DDSCAPS2_MIPMAPSUBLEVEL:0);s[i].mip_next=i+1<levels?s+i+1:NULL;}}
static void cube(DDSurface s[6][9],unsigned width,unsigned height,unsigned levels,BOOL enabled){for(unsigned f=0;f<6;f++)chain(s[f],width,height,levels,f,enabled);if(enabled)for(unsigned f=0;f<6;f++)s[0][0].cube_faces[f]=s[f];}
/* Literal Windows/Wine test_device_load dimensions, coordinates and HRESULTs.
 * Mip coordinate oracle follows the Wine/Win98 RGB rule, not XP/Win8 broken variants. */
static void frozen_cases(void){const struct Case{unsigned sw,sh,sl;RECT rect;unsigned dw,dh,dl;POINT point;BOOL sc,dc,null_coords;HRESULT hr;}cases[]={
 {128,128,8,{0,0,0,0},128,128,8,{0,0},0,0,0,DDERR_INVALIDPARAMS},
 {128,128,8,{0,0,100,100},128,128,8,{50,50},0,0,0,DDERR_INVALIDPARAMS},
 {128,128,8,{30,20,93,52},128,128,8,{31,31},0,0,0,0},
 {128,128,1,{0,0,0,0},128,128,1,{0,0},0,0,1,0},
 {128,128,1,{0,0,0,0},256,128,1,{0,0},0,0,1,DDERR_INVALIDPARAMS},
 {256,128,1,{0,0,0,0},128,128,1,{0,0},0,0,1,DDERR_INVALIDPARAMS},
 {128,128,8,{30,20,93,52},128,128,8,{10,10},1,1,0,0},
 {128,128,1,{0,0,0,0},128,128,1,{0,0},0,1,1,DDERR_INVALIDPARAMS},
 {128,128,8,{30,20,93,52},128,128,4,{31,31},0,0,0,0},
 {128,128,4,{30,20,93,52},128,128,8,{31,31},0,0,0,DDERR_INVALIDPARAMS},
 {128,128,8,{32,32,96,96},32,32,1,{32,32},0,0,0,0},
 {128,128,8,{0,0,64,64},32,32,4,{0,0},0,0,0,0}};
 for(unsigned round=0;round<32;round++)for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){const struct Case*c=&cases[i];DDSurface src[6][9],dst[6][9];cube(src,c->sw,c->sh,c->sl,c->sc);cube(dst,c->dw,c->dh,c->dl,c->dc);D3D7Device d={&owner};POINT point=c->point;RECT rect=c->rect;reset();MUST(d3d7_device_Load(&d,dst[0],c->null_coords?NULL:&point,src[0],c->null_coords?NULL:&rect,c->dc?DDSCAPS2_CUBEMAP_ALLFACES:0)==c->hr);MUST(!memcmp(&point,&c->point,sizeof(point))&&!memcmp(&rect,&c->rect,sizeof(rect)));if(c->hr){MUST(!copies&&!palettes&&!keys);continue;}unsigned faces=c->sc?6:1;MUST(copies==faces*c->dl);unsigned offset=0;for(unsigned w=c->sw;w>c->dw;w>>=1)offset++;for(unsigned f=0;f<faces;f++){POINT p=c->null_coords?(POINT){0,0}:point;RECT r=c->null_coords?(RECT){0,0,(LONG)c->sw,(LONG)c->sh}:rect;for(unsigned l=0;l<offset;l++){p.x/=2;p.y/=2;r.left/=2;r.top/=2;r.right=(r.right+1)/2;r.bottom=(r.bottom+1)/2;}for(unsigned l=0;l<c->dl;l++){struct Copy*q=&records[f*c->dl+l];MUST(q->src==&src[f][l+offset]&&q->dst==&dst[f][l]);MUST(!memcmp(&q->s,&r,sizeof(r)));MUST(q->d.left==p.x&&q->d.top==p.y&&q->d.right==p.x+r.right-r.left&&q->d.bottom==p.y+r.bottom-r.top);p.x/=2;p.y/=2;r.left/=2;r.top/=2;r.right=(r.right+1)/2;r.bottom=(r.bottom+1)/2;}}}}
static void metadata_and_failures(void){DDSurface src[9],dst[9];DDPalette sp={0},dp={0};chain(src,8,8,4,0,0);chain(dst,8,8,2,0,0);for(unsigned i=0;i<256;i++){sp.entries[i].value=1000+i;dp.entries[i].value=2000+i;}src[0].palette=&sp;dst[0].palette=&dp;src[0].color_key_present=15;for(unsigned i=0;i<4;i++)src[0].color_key[i]=(DDCOLORKEY){30+i,30+i};reset();MUST(!d3d_texture_load7(dst,src,NULL,NULL,0xffffffffu)&&copies==2&&palettes==1&&keys==4&&dst[0].palette==&dp&&!memcmp(&sp,&dp,sizeof(sp)));for(unsigned i=0;i<4;i++)MUST(dst[0].color_key[i].dwColorSpaceLowValue==30+i);MUST(dst[0].desc.ddsCaps.dwCaps&DDSCAPS_ALLOCONLOAD);
 reset();dst[0].palette=NULL;MUST(!d3d_texture_load7(dst,src,NULL,NULL,0)&&copies==2&&!palettes&&!dst[0].palette);reset();MUST(d3d_legacy_texture_load_surface(dst,src)==DDERR_NOPALETTEATTACHED&&!copies&&!(dst[0].desc.ddsCaps.dwCaps&DDSCAPS_ALLOCONLOAD));dst[0].palette=&dp;dst[0].color_key_present=0;reset();MUST(!d3d_legacy_texture_load_surface(dst,src)&&copies==2&&palettes==1&&keys==1&&dst[0].color_key_present==(1u<<D9WG_DDCKEY_SOURCE_BLT));MUST(!(dst[1].desc.ddsCaps.dwCaps&DDSCAPS_ALLOCONLOAD));
 chain(src,8,8,2,0,0);chain(dst,8,8,4,0,0);dst[0].color_key_present=15;dst[0].color_key[0]=(DDCOLORKEY){99,99};reset();MUST(!d3d_legacy_texture_load_surface(dst,src)&&copies==2&&!keys&&dst[0].color_key[0].dwColorSpaceLowValue==99&&(dst[2].desc.ddsCaps.dwCaps&DDSCAPS_ALLOCONLOAD));reset();MUST(!d3d_legacy_texture_load_surface(dst,dst)&&!copies);dst[0].palette=&dp;src[0].palette=&sp;dst[0].width=4;reset();MUST(d3d_legacy_texture_load_surface(dst,src)==D3DERR_TEXTURE_LOAD_FAILED&&palettes==1&&!copies&&!keys);dst[0].width=8;
 reset();fail_copy=2;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_INVALIDPARAMS&&!copies&&!palettes);chain(dst,8,8,2,0,0);reset();fail_copy=2;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_GENERIC&&copies==2);reset();dst[0].lock_depth=1;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_SURFACEBUSY&&!copies);dst[0].lock_depth=0;reset();dst[0].external_memory=1;fail_readback=1;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_GENERIC&&copies==1&&readbacks==1);dst[0].external_memory=0;
 dst[0].palette=&dp;src[0].palette=&sp;reset();fail_palette=1;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_GENERIC&&palettes==1&&!copies);src[0].color_key_present=15;reset();fail_key=2;MUST(d3d_texture_load7(dst,src,NULL,NULL,0)==DDERR_GENERIC&&palettes==1&&keys==2&&!copies);D3D7Device foreign={&foreign_owner};reset();MUST(d3d7_device_Load(&foreign,dst,NULL,src,NULL,0)==DDERR_INVALIDPARAMS&&!copies);MUST(d3d7_device_Load(&foreign,NULL,NULL,src,NULL,0)==DDERR_INVALIDPARAMS);}
static void cube_and_bounds(void){DDSurface src[6][9],dst[6][9];cube(src,8,8,4,1);cube(dst,8,8,4,1);reset();MUST(!d3d_texture_load7(dst[0],src[0],NULL,NULL,0)&&!copies&&!palettes&&!keys);reset();MUST(!d3d_texture_load7(dst[0],src[0],NULL,NULL,(DDSCAPS2_CUBEMAP_POSITIVEX<<1)|(DDSCAPS2_CUBEMAP_POSITIVEX<<4))&&copies==8);MUST(records[0].src==src[1]&&records[4].src==src[4]);reset();dst[5][2].mip_next=NULL;MUST(d3d_texture_load7(dst[0],src[0],NULL,NULL,DDSCAPS2_CUBEMAP_POSITIVEX)==0&&copies==4);dst[5][0].width=7;reset();MUST(d3d_texture_load7(dst[0],src[0],NULL,NULL,DDSCAPS2_CUBEMAP_POSITIVEX)==DDERR_INVALIDPARAMS&&!copies&&!keys&&!palettes);dst[5][0].width=8;dst[5][2].mip_next=&dst[5][3];dst[0][0].cube_faces[5]=NULL;reset();MUST(d3d_texture_load7(dst[0],src[0],NULL,NULL,0)==DDERR_INVALIDPARAMS&&!copies);
 DDSurface s[9],d[9];chain(s,8,8,4,0,0);chain(d,8,8,4,0,0);reset();MUST(d3d_texture_load7(d+1,s,NULL,NULL,0)==DDERR_INVALIDPARAMS&&!copies);POINT point={0,0};RECT negative={-1,0,4,4};MUST(d3d_texture_load7(d,s,&point,&negative,0)==DDERR_INVALIDPARAMS);chain(s,INT_MAX,1,1,0,0);chain(d,INT_MAX,1,1,0,0);point=(POINT){INT_MAX-100,0};RECT rect={100,0,200,1};reset();MUST(!d3d_texture_load7(d,s,&point,&rect,0)&&copies==1&&records[0].d.right==INT_MAX);}
int main(void){(void)&surface_Blt;(void)&d3d_texture_load7;(void)&d3d_texture_load_legacy;frozen_cases();metadata_and_failures();cube_and_bounds();printf("PASS actual texture Load C: %u checks, 384 frozen Windows/Wine cases, mip subsets and ceil coordinates, cube selection/preflight, palette identity and key preservation, legacy unequal mip prefixes, ALLOCONLOAD, source immutability, external memory and accepted failure prefixes\\n",checks);return 0;}
`;
try {const file = dir + "/load.c", binary = dir + "/load"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
