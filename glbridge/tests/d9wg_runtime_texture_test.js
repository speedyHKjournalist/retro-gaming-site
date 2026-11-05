"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { spawnSync, execFileSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-texture-"));
function definition(source, name) {
    let start = -1, at;
    do { start = source.indexOf("static BOOL " + name + "(", start + 1);assert(start >= 0, name);at = source.indexOf("{", start); }
    while (source.indexOf(";", start) < at);
    let depth = 1;
    for (at++; depth; at++) { if (source[at] === "{") depth++;else if (source[at] === "}") depth--; }
    return source.slice(start, at);
}
try {
    for (const api of ["d3d8", "d3d9", "ddraw"]) {
        if (process.env.D9WG_TEXTURE_BASELINE && api !== "d3d8") continue;
        const relative = api + "proxy/" + api + "_proxy.c";
        const source = process.env.D9WG_TEXTURE_BASELINE ? execFileSync("git", ["show", "833bc0e:glbridge/" + relative], { cwd: path.dirname(root), encoding: "utf8" })
            : readComparisonSource(path.join(root, relative), "utf8");
        const frontend = (api === "d3d9" ? definition(source, "emit_level_update_api") + "\n" : "") + definition(source, api === "ddraw" ? "emit_surface_upload" : "emit_level_update") +
            (api === "d3d9" ? "\n" + definition(source, "emit_cube_texture_update") + "\n" + definition(source, "emit_volume_texture_update") :
                api === "d3d8" ? "\n" + definition(source, "emit_volume_level_update") : "");
        const code = `
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_runtime.h"
#include "runtime/d9wg_format_layouts.h"
typedef uint32_t UINT,D3DFORMAT;typedef int32_t LONG;typedef int BOOL;
typedef struct {LONG left,top,right,bottom;} RECT;
typedef struct {uint8_t *shadow;UINT row_pitch,slice_pitch,depth,byte_count;} D8TextureLevel,D9TextureLevel;
typedef struct {UINT handle,format;D8TextureLevel levels[4];} D8VolumeTexture;
typedef struct D9VolumeTexture D9VolumeTexture;
typedef struct {UINT api;} Device;
struct D9VolumeTexture{UINT handle,format;D8TextureLevel levels[4];Device*device;};
typedef struct {UINT handle,format,level_count;D9TextureLevel levels[24];Device*device;} D9CubeTexture;
${api === "d3d9" ? "static Device cube_device={9};static UINT device_compatibility_api(Device*d){return d->api;}" : ""}
typedef struct {UINT Left,Top,Right,Bottom,Front,Back;} D3DBOX;
typedef int INT;
${api === "d3d9" ? "static void SetRect(RECT *rect,INT l,INT t,INT r,INT b){rect->left=l;rect->top=t;rect->right=r;rect->bottom=b;}" : ""}
typedef struct {uint8_t *shadow;UINT handle,width,height,mip_level,cube_face,format,bytes_per_pixel,pitch;BOOL upload_pending;} DDSurface;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
static uint8_t arena[16u<<20],*expected;
static UINT capacity,used,records,rows_done,row_bytes,row_count,block_height,source_pitch,origin_x,origin_y,logical_width,logical_height,level,z;
static unsigned g_transport_lock, lock_sessions;
static UINT source_slice_pitch;
static void EnterCriticalSection(unsigned *lock){if(!*lock)lock_sessions++;(*lock)++;}
static void LeaveCriticalSection(unsigned *lock){assert(*lock);(*lock)--;}
static BOOL open_transport_locked(void){assert(g_transport_lock);return TRUE;}
static uint8_t *batch_base(void){return arena;}
static UINT batch_capacity(void){return capacity;}
static void flush(void){
 for(UINT at=sizeof(D9WGBatchHeader);at<used;){
  D9WGCommandHeader *command=(D9WGCommandHeader*)(arena+at);D9WGUpdateTexture *update=(D9WGUpdateTexture*)(command+1);
  assert(command->opcode==D9WG_OP_UPDATE_TEXTURE&&!(command->size&7u));
  UINT plane=rows_done/row_count, local=rows_done%row_count;
  assert(update->resource_handle==77&&update->level==level&&update->z==z+plane&&update->depth==1);
  assert(update->x==origin_x&&update->y==origin_y+local*block_height&&update->width==logical_width);
  UINT rows=update->data_bytes/row_bytes, height=rows*block_height;
  if(height>logical_height-local*block_height)height=logical_height-local*block_height;
  assert(rows&&rows<=row_count-local&&update->height==height&&update->row_pitch==row_bytes);
  assert(update->data_bytes==rows*row_bytes&&update->data_offset+update->data_bytes<=used);
  for(UINT row=0;row<rows;row++)assert(!memcmp(arena+update->data_offset+row*row_bytes,expected+plane*source_slice_pitch+(local+row)*source_pitch,row_bytes));
  rows_done+=rows;records++;at+=command->size;
 }
 used=sizeof(D9WGBatchHeader);
}
static BOOL reserve_command_locked(uint16_t opcode,UINT payload_bytes,UINT extra_bytes,
 ${api === "ddraw" ? "" : "D9WGCommandHeader **command_out,"}uint8_t **payload,uint8_t **extra){
 assert(g_transport_lock);UINT size=D9WG_ALIGN8(sizeof(D9WGCommandHeader)+payload_bytes+extra_bytes);
 if(size>capacity-sizeof(D9WGBatchHeader))return FALSE;if(used+size>capacity)flush();
 D9WGCommandHeader *command=(D9WGCommandHeader*)(arena+used);memset(command,0,size);command->opcode=opcode;command->size=size;
 ${api === "ddraw" ? "" : "if(command_out)*command_out=command;"}
 *payload=(uint8_t*)(command+1);*extra=*payload+payload_bytes;used+=size;return TRUE;
}
#define D9WG_RESERVE_UPLOAD(op,size,extra,payload,blob) reserve_command_locked(op,size,extra,${api === "ddraw" ? "" : "NULL,"}payload,blob)
#define D9WG_DISCARD_FLAG 0x2000u
#include "runtime/d9wg_upload_bridge.h"
${api !== "ddraw" ? `static BOOL texture_format_layout(D3DFORMAT format,UINT *w,UINT *h,UINT *bytes){*w=*h=format==21?1:4;*bytes=format==21?4:8;return TRUE;}
static BOOL multiply_u32(UINT a,UINT b,UINT *out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}`
    : `static UINT format_compressed_block_bytes(UINT format){return format==21?0:8;}
static BOOL ensure_surface_storage(DDSurface *surface){(void)surface;return TRUE;}`}
${frontend}
static void run_case(UINT width,UINT height,BOOL bc,BOOL partial,UINT mip,UINT face,BOOL tiny){
 UINT bw=bc?4:1,bytes=bc?8:4;block_height=bw;row_count=(height+bw-1)/bw;row_bytes=((width+bw-1)/bw)*bytes;
 origin_x=partial?4:0;origin_y=partial?4:0;logical_width=width;logical_height=height;level=mip;z=face;
 source_pitch=((width+origin_x+bw-1)/bw)*bytes+16;
 UINT source_rows=(height+origin_y+bw-1)/bw;source_slice_pitch=source_rows*source_pitch;uint8_t *shadow=malloc(source_slice_pitch);assert(shadow);
 for(UINT i=0;i<source_rows*source_pitch;i++)shadow[i]=(uint8_t)(i*131u+(i>>8));
 expected=shadow+origin_y/bw*source_pitch+origin_x/bw*bytes;
 capacity=tiny?sizeof(D9WGBatchHeader)+D9WG_ALIGN8(sizeof(D9WGCommandHeader)+sizeof(D9WGUpdateTexture)+row_bytes):12u*1024u*1024u-36u;
 used=sizeof(D9WGBatchHeader);rows_done=records=0;
 RECT rect={(LONG)origin_x,(LONG)origin_y,(LONG)(origin_x+width),(LONG)(origin_y+height)};
 ${api === "ddraw" ? `DDSurface surface={shadow,77,width+origin_x,height+origin_y,mip,face,bc?0x31545844u:21,bc?0:4,source_pitch,partial?FALSE:TRUE};
 assert(emit_surface_upload(&surface,&rect));assert(!surface.upload_pending);`
    : `${api === "d3d9" ? "D9" : "D8"}TextureLevel data={.shadow=shadow,.row_pitch=source_pitch};
 ${api === "d3d9" ? `if(face){D9CubeTexture cube={.device=&cube_device,.handle=77,.format=bc?0x31545844u:21,.level_count=3};cube.levels[face*3+mip]=data;assert(emit_cube_texture_update(&cube,face,mip,&rect));}
 else assert(emit_level_update(77,bc?0x31545844u:21,&data,mip,face,&rect));` : "assert(emit_level_update(77,bc?0x31545844u:21,&data,mip,face,&rect));"}`}
 flush();assert(rows_done==row_count&&records==2&&!g_transport_lock);free(shadow);
}
${api === "ddraw" ? "" : `static void volume_case(void){
 logical_width=logical_height=7;origin_x=origin_y=4;level=2;z=1;block_height=4;row_count=2;row_bytes=16;source_pitch=40;source_slice_pitch=120;
 uint8_t shadow[360];for(UINT i=0;i<sizeof(shadow);i++)shadow[i]=(uint8_t)(i*31u+(i>>4));expected=shadow+source_slice_pitch+source_pitch+8;
 capacity=sizeof(D9WGBatchHeader)+D9WG_ALIGN8(sizeof(D9WGCommandHeader)+sizeof(D9WGUpdateTexture)+row_bytes);used=sizeof(D9WGBatchHeader);records=rows_done=lock_sessions=0;
 ${api === "d3d9" ? "D9" : "D8"}VolumeTexture volume={${api === "d3d9" ? ".device=&cube_device," : ""}.handle=77,.format=0x31545844u};volume.levels[level]=(D8TextureLevel){.shadow=shadow,.row_pitch=source_pitch,.slice_pitch=source_slice_pitch};
 ${api === "d3d9" ? "D3DBOX box={4,4,11,11,1,3};assert(emit_volume_texture_update(&volume,level,&box));" : "RECT rect={4,4,11,11};assert(emit_volume_level_update(&volume,level,&rect,1,2));"}
 flush();assert(rows_done==4&&records==4&&lock_sessions==1&&!g_transport_lock);
}`}
int main(void){
 ${api === "d3d9" ? "(void)&texture_format_layout;" : ""}
 (void)emit_buffer_update;(void)emit_texture_rows;
 run_case(2048,2048,FALSE,FALSE,0,0,FALSE);
 run_case(2048,2048,FALSE,FALSE,2,5,FALSE);
 run_case(7,7,TRUE,FALSE,2,5,TRUE);
 run_case(7,7,TRUE,TRUE,2,5,TRUE);
 ${api === "ddraw" ? "" : "volume_case();"}
 D9WGTextureRows invalid={0};invalid.width=1;invalid.height=1;
 assert(!d9wg_upload_texture_rows(&invalid,1024,emit_texture_band_locked,0));
 printf("PASS ${api} actual C texture uploads: 2048-square planes, cube/slice metadata and whole-volume locking, padded pitch and BC edge/partial bands\\n");
 return 0;
}
`;
        const input = path.join(directory, api + ".c"), binary = path.join(directory, api);fs.writeFileSync(input, code);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", root,
            input, path.join(root, "runtime/d9wg_runtime.c"), "-o", binary], { encoding: "utf8" });
        assert.equal(build.status, 0, build.stderr || String(build.error));
        const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr || String(run.error));process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
