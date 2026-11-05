"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-indexed-span-"));
try {
    const source=fs.readFileSync(root+"/ddrawproxy/d3d7_proxy.inc","utf8");
    const start=source.indexOf("static BOOL d3d7_emit_draw_indexed_up_raw("),end=source.indexOf("static BOOL d3d7_emit_draw_up(",start);assert(start>0&&end>start);
    fs.writeFileSync(dir+"/test.c",`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef int BOOL;typedef uint32_t UINT,DWORD,D3DPRIMITIVETYPE;typedef uint16_t WORD;typedef uint8_t BYTE;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
typedef struct {uint32_t status;} D9WGClipLegacyStatus;
typedef struct {uint32_t device_handle;} Owner;
typedef struct {int gpu_dirty;} Memory;
typedef struct {Memory *memory;} Surface;
typedef struct {Owner *owner;Surface *render_target;D9WGClipLegacyStatus clip_status;} D3D7Device;
static uint8_t wire[8192];static int g_transport_lock;static unsigned submissions,classified;
static int allow_submission=1,allow_classification=1;
static uint8_t *batch_base(void){return wire;}
static BOOL d3d7_primitive_counts(UINT type,UINT count,UINT *primitives){if(type!=4||!count||count%3)return 0;*primitives=count/3;return 1;}
static BOOL d3d7_emit_fvf(D3D7Device*d,DWORD fvf,UINT*stride){(void)d;if(fvf!=0x144)return 0;*stride=28;return 1;}
static BOOL d3d7_multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return 0;*out=a*b;return 1;}
static BOOL d3d7_draw_clip_status(D3D7Device*d,DWORD f,const void*v,UINT count,const WORD*i,UINT ic,BOOL screen,D9WGClipLegacyStatus*s){(void)d;(void)f;(void)v;(void)i;(void)ic;(void)screen;classified=count;s->status=123;return allow_classification;}
static BOOL reserve_command_locked(UINT op,UINT payload,UINT extra,uint8_t **p,uint8_t **blob){if(op!=D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP||payload+extra>sizeof(wire)||!allow_submission)return 0;*p=wire;*blob=wire+payload;++submissions;return 1;}
${source.slice(start,end)}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"indexed span assertion line %d\\n",__LINE__);return 1;}}while(0)
int main(void){
 Owner owner={17};Memory memory={0};Surface surface={&memory};D3D7Device d={&owner,&surface,{9}};
 uint8_t vertices[128][28];for(unsigned i=0;i<sizeof(vertices);++i)((uint8_t*)vertices)[i]=(uint8_t)(i*37u+i/28u);
 for(unsigned first=0;first<120;++first){WORD indices[6]={(WORD)(first+4),(WORD)first,(WORD)(first+2),(WORD)(first+2),(WORD)first,(WORD)(first+4)};
  submissions=classified=0;CHECK(d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,128,indices,6));
  D9WGDrawIndexedPrimitiveUP command;memcpy(&command,wire,sizeof(command));
  CHECK(submissions==1&&classified==128&&command.vertex_count==5&&command.vertex_bytes==140&&command.index_bytes==12&&command.primitive_count==2&&command.device_handle==17);
  for(unsigned i=0;i<6;++i){WORD index;memcpy(&index,wire+command.index_data_offset+i*2u,2);CHECK(index<5);CHECK(!memcmp(wire+command.vertex_data_offset+index*28u,vertices[indices[i]],28));}
  CHECK(d.clip_status.status==123&&memory.gpu_dirty);
 }
 WORD bad[3]={1,128,2};submissions=0;d.clip_status.status=77;memory.gpu_dirty=0;
 CHECK(!d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,128,bad,3)&&!submissions&&d.clip_status.status==77&&!memory.gpu_dirty);
 bad[1]=1;CHECK(!d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,128,bad,0));
 CHECK(!d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,UINT32_MAX,bad,3));
 allow_classification=0;CHECK(!d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,128,bad,3)&&!submissions);
 allow_classification=1;allow_submission=0;CHECK(!d3d7_emit_draw_indexed_up_raw(&d,4,0x144,vertices,128,bad,3)&&d.clip_status.status==77&&!memory.gpu_dirty);
 puts("PASS actual DDraw indexed upload: 120 rebased ranges preserve every addressed vertex, full ClipStatus input, invalid bounds and failure atomicity");return 0;
}
`);
    const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),"-I",root,dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);
    const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
