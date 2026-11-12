"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_DEPTH_MEMORY_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),legacy=fs.readFileSync(root+"/ddrawproxy/d3d7_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-depth-memory-"));
function extract(input,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(input);assert(m,name);let at=input.indexOf("{",m.index)+1,depth=1;for(;depth;at++){if(input[at]==="{")depth++;if(input[at]==="}")depth--;}return input.slice(m.index,at);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ddrawproxy/ddraw_protocol.h"
typedef uint32_t DWORD,UINT,D3DCOLOR;typedef int32_t LONG,HRESULT;typedef uint8_t BYTE;typedef int BOOL;
typedef struct{LONG left,top,right,bottom;} RECT;typedef RECT D3DRECT;
typedef struct{BOOL gpu_dirty;BYTE bits[160];} Memory;
typedef struct{DWORD device_handle;} DDrawObject;
typedef struct DDSurface{DDrawObject*owner;Memory*memory;BYTE*shadow;UINT format,width,height,pitch,bytes_per_pixel,handle,mip_level,cube_face;BOOL upload_pending;struct DDSurface*attached_depth;} DDSurface;
typedef struct{DDrawObject*owner;DDSurface*render_target;DWORD render_states[256];} D3D7Device;typedef D3D7Device IDirect3DDevice7;typedef float D3DVALUE;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define D3DERR_ZBUFFER_NOTPRESENT (-2)
#define D3DERR_STENCILBUFFER_NOTPRESENT (-3)
#define D3DERR_INITFAILED (-4)
#define D3DRENDERSTATE_ZENABLE 7
#define D3DRENDERSTATE_ZWRITEENABLE 14
#define D3DRENDERSTATE_STENCILENABLE 52
#define D3DCLEAR_TARGET 1u
#define D3DCLEAR_ZBUFFER 2u
#define D3DCLEAR_STENCIL 4u
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(p,n) memset(p,0,n)
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
#define DDWG_TRACE_ERROR(x) (x)
static unsigned checks,emits,uploads;static BOOL fail_emit,fail_storage;static int g_transport_lock;static BYTE payload[256];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw depth memory line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static BOOL ensure_surface_storage(DDSurface*s){(void)s;return !fail_storage;}
static BOOL emit_surface_upload(DDSurface*s,const RECT*r){(void)r;uploads++;s->upload_pending=FALSE;return TRUE;}
static BOOL sync_external_surface_for_read(DDSurface*s){(void)s;return TRUE;}
static BOOL ensure_device(DDrawObject*d){return !!d;}
static BOOL emit_command(UINT op,const void*data,UINT bytes){(void)data;(void)bytes;if(op!=D9WG_OP_DD_BLT)return FALSE;emits++;return !fail_emit;}
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*d){return d;}
static BOOL d3d7_multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}
static BOOL reserve_command_locked(UINT op,UINT size,UINT extra,uint8_t**p,uint8_t**blob){(void)op;if(size+extra>sizeof(payload)||fail_emit)return FALSE;*p=payload;*blob=payload+size;return TRUE;}
${["surface_depth_mask","prepare_surface_for_host","surface_mirror_depth_fill","emit_blt"].map(n=>extract(source,n)).join("\n")}
${["d3d7_mark_draw_surfaces_dirty","d3d7_device_Clear"].map(n=>extract(legacy,n)).join("\n")}
static int fills(void){
 const UINT formats[]={70,71,73,75,77,79,80},masks[]={0xffff,0xffffffff,0xfffe,0xffffff00,0xffffff00,0xffffff00,0xffff};
 const DWORD values[]={0,1,0xffff,0xdeadbeef,0xffffffff};
 for(unsigned f=0;f<7;f++)for(unsigned dirty=0;dirty<2;dirty++)for(unsigned part=0;part<2;part++)for(unsigned v=0;v<5;v++){
  Memory memory={0};memset(memory.bits,0x5a,sizeof(memory.bits));memory.gpu_dirty=dirty;BYTE before[160];memcpy(before,memory.bits,160);DDrawObject owner={7};UINT bpp=(f==0||f==2||f==6)?2:4;DDSurface s={.owner=&owner,.memory=&memory,.shadow=memory.bits+8,.format=formats[f],.width=5,.height=4,.pitch=28,.bytes_per_pixel=bpp,.handle=17};
  RECT area=part?(RECT){1,1,4,3}:(RECT){0,0,5,4};BOOL stencil=f==2||f==3||f==5,known=!dirty||(!part&&!stencil);
  CHECK(emit_blt(&s,&area,NULL,NULL,D9WG_DDBLT_DEPTH_FILL,values[v],.5f));CHECK(memory.gpu_dirty==!known);
  for(unsigned i=0;i<160;i++){BYTE expected=before[i];if(known){for(LONG y=area.top;y<area.bottom;y++)for(LONG x=area.left;x<area.right;x++){UINT at=8+y*28+x*bpp;DWORD old=0;memcpy(&old,before+at,bpp);DWORD next=(old&~masks[f])|(values[v]&masks[f]);BYTE packed[4];memcpy(packed,&next,4);if(i>=at&&i<at+bpp)expected=packed[i-at];}}CHECK(memory.bits[i]==expected);}
  memcpy(memory.bits,before,160);memory.gpu_dirty=dirty;fail_emit=TRUE;CHECK(!emit_blt(&s,&area,NULL,NULL,D9WG_DDBLT_DEPTH_FILL,values[v],.5f));CHECK(memory.gpu_dirty==(BOOL)dirty&&!memcmp(memory.bits,before,160));fail_emit=FALSE;
  s.upload_pending=TRUE;unsigned old=uploads;CHECK(prepare_surface_for_host(&s)&&uploads==old+1&&!s.upload_pending);
 }
 return 0;
}
static int invalidation(void){Memory color={0},depth={0};DDrawObject owner={7};DDSurface ds={.memory=&depth,.format=75},rt={.memory=&color,.attached_depth=&ds};D3D7Device d={.owner=&owner,.render_target=&rt};
 for(DWORD flags=1;flags<8;flags++){color.gpu_dirty=depth.gpu_dirty=FALSE;CHECK(!d3d7_device_Clear(&d,0,NULL,flags,0,.5f,3));CHECK(color.gpu_dirty==!!(flags&1)&&depth.gpu_dirty==!!(flags&6));color.gpu_dirty=depth.gpu_dirty=FALSE;fail_emit=TRUE;CHECK(d3d7_device_Clear(&d,0,NULL,flags,0,.5f,3)==D3DERR_INITFAILED&&!color.gpu_dirty&&!depth.gpu_dirty);fail_emit=FALSE;}
 for(unsigned z=0;z<2;z++)for(unsigned write=0;write<2;write++)for(unsigned stencil=0;stencil<2;stencil++){d.render_states[7]=z;d.render_states[14]=write;d.render_states[52]=stencil;color.gpu_dirty=depth.gpu_dirty=FALSE;d3d7_mark_draw_surfaces_dirty(&d);CHECK(color.gpu_dirty&&depth.gpu_dirty==(BOOL)((z&&write)||stencil));}color.gpu_dirty=depth.gpu_dirty=FALSE;rt.attached_depth=NULL;d3d7_mark_draw_surfaces_dirty(&d);CHECK(color.gpu_dirty&&!depth.gpu_dirty);d.render_target=NULL;d3d7_mark_draw_surfaces_dirty(&d);return 0;}
int main(void){CHECK(!fills());CHECK(!invalidation());printf("PASS actual DDraw depth memory: %u checks; seven packed formats, exact D32 fill bits, immediate system-memory writes, partial/stencil dirty retention, transport failure atomicity, depth uploads and draw/clear invalidation\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+"/test.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
