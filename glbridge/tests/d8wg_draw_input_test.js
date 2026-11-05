"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=readComparisonSource(process.env.D8WG_DRAW_INPUT_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-draw-input-"));
function extract(name){const m=new RegExp("static (?:BOOL|void|int|HRESULT)(?: WINAPI)? "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const input=/typedef struct D8ProcessInput \{[\s\S]*?\} D8ProcessInput;/.exec(source);assert(input);
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_fvf.h"
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT,D3DPRIMITIVETYPE,D3DFORMAT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D8WG_TRACE_ERROR(x) (x)
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D3DERR_DRIVERINTERNALERROR (-3)
#define FAILED(x) ((x)<0)
#define D8WG_MAX_STREAMS 16
#define D8WG_MAX_VERTEX_ELEMENTS 32
#define D8WG_TRANSFORM_VIEW_SLOT 0
#define D8WG_TRANSFORM_PROJECTION_SLOT 1
#define D8WG_TRANSFORM_WORLD_SLOT 10
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x20
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x80
#define D3D8RS_SOFTWAREVERTEXPROCESSING 153
#define D3DRS_CLIPPING 136
#define D3DRS_CLIPPLANEENABLE 152
#define D3DRS_SHADEMODE 9
#define D3DSHADE_FLAT 1
#define D3DFVF_POSITION_MASK 14u
#define D3DFVF_XYZRHW 4u
#define D3DFMT_INDEX16 101
#define D3DFMT_INDEX32 102
#define D3DPT_POINTLIST 1
#define D3DPT_LINELIST 2
#define D3DPT_LINESTRIP 3
#define D3DPT_TRIANGLELIST 4
#define D3DPT_TRIANGLESTRIP 5
#define D3DPT_TRIANGLEFAN 6
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned uploads,upload_fail_at,sequence_count;static char sequence[128];
static unsigned allocations,frees,attempts,fail_at,emits,releases;static int transport_ok=1;
static void *HeapAlloc(void *heap,unsigned flags,size_t size){(void)heap;if(++attempts==fail_at)return NULL;void *p=flags?calloc(1,size):malloc(size);if(p)allocations++;return p;}
static int HeapFree(void *heap,unsigned flags,void *p){(void)heap;(void)flags;if(p){frees++;free(p);}return 1;}
#include "runtime/d9wg_vertex_windows.h"
typedef struct D8Device D8Device;typedef D8Device IDirect3DDevice8;
typedef struct D8VertexBuffer {int iface;D8Device *device;uint8_t *shadow;UINT length,handle,lock_offset,lock_size,lock_flags;BOOL locked;D9WGClipBuffer clips;} D8VertexBuffer;
typedef struct D8IndexBuffer {int iface;D8Device *device;uint8_t *shadow;UINT length,format,handle,lock_offset,lock_size,lock_flags;BOOL locked;} D8IndexBuffer;
typedef struct D8Shader {BOOL declaration_only,deleted;UINT element_count;D9WGVertexElement elements[32];} D8Shader;
struct D8Device {DWORD vertex_shader;D8Shader *vertex_shader_ref;struct {D8VertexBuffer *buffer;UINT stride;} streams[16];D8IndexBuffer *index_buffer;UINT base_vertex_index,handle;D9WGClipStatus clip_status;BOOL clip_status_valid;struct {DWORD BehaviorFlags;} creation;DWORD render_states[256];float transforms[266][16],clip_planes[6][4];struct {UINT X,Y,Width,Height;float MinZ,MaxZ;} viewport;};
#define IDirect3DVertexBuffer8_Release(p) ((void)(p),++releases)
#define IDirect3DIndexBuffer8_Release(p) ((void)(p),++releases)
static D8Device *device_from_iface(IDirect3DDevice8 *iface){return iface;}
static BOOL fvf_to_elements(DWORD fvf,D9WGVertexElement *elements,UINT maximum,UINT *count){return d9wg_fvf_decode(8,fvf,elements,maximum,count,NULL);}
static BOOL emit_command(UINT op,const void *data,UINT bytes){if(op==D9WG_OP_DRAW_PRIMITIVE||op==D9WG_OP_DRAW_INDEXED_PRIMITIVE)sequence[sequence_count++]='D';(void)data;(void)bytes;++emits;return transport_ok;}
static BOOL emit_draw_primitive_up(const D9WGDrawPrimitiveUP *command,const void *vertices){(void)command;(void)vertices;++emits;return transport_ok;}
static BOOL emit_draw_indexed_primitive_up(const D9WGDrawIndexedPrimitiveUP *command,const void *indices,const void *vertices){(void)command;(void)indices;(void)vertices;++emits;return transport_ok;}

static BOOL emit_buffer_update(uint32_t h,uint32_t off,const void *p,uint32_t n,uint32_t f){(void)h;(void)off;(void)p;(void)n;(void)f;sequence[sequence_count++]='U';uploads++;return transport_ok&&uploads!=upload_fail_at;}
#define D9WG_MAPPED_DEVICE D8Device
#define D9WG_MAPPED_VERTEX D8VertexBuffer
#define D9WG_MAPPED_INDEX D8IndexBuffer
#include "runtime/d9wg_mapped_draw_bridge.h"
${input[0]}
${['multiply_u32','primitive_element_count','d8_current_draw_elements','d8_current_source_elements','d8_process_input_free','d8_process_input','d8_clip_emit','d8_clip_mask','d8_clip_vb_draw','d8_software_vertex_processing','d8_prepare_clip_status','d8_draw_stream_range','d8_draw_index_bounds','device_draw_primitive','device_draw_indexed','clear_stream_zero_after_up','clear_indices_after_indexed_up','device_draw_up','device_draw_indexed_up'].map(extract).join('\n')}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D8 draw input line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static void reset_status(D8Device *device){device->clip_status=(D9WGClipStatus){0,0xffffffffu};device->clip_status_valid=TRUE;}
static int unchanged(D8Device *d,unsigned emitted,D9WGClipStatus before){return emits==emitted&&!memcmp(&d->clip_status,&before,sizeof(before));}
int main(void){
 static D8Device device,other;device.creation.BehaviorFlags=0x20;device.render_states[136]=1;device.render_states[151]=255;device.render_states[170]=0x3f400000;
 for(unsigned i=0;i<266;++i)for(unsigned j=0;j<16;++j)device.transforms[i][j]=(j%5)==0?1:0;
 D8Shader declaration={.declaration_only=TRUE,.deleted=TRUE,.element_count=4,.elements={{15,0,2,0,0,0},{15,12,2,0,3,0},{1,0,2,0,0,1},{1,12,2,0,3,1}}};device.vertex_shader=1;device.vertex_shader_ref=&declaration;
 float first[18]={-3,0,.25f,-1,0,0, -.25f,0,.25f,-1,0,0, 4,0,.25f,-1,0,0},second[6]={.5f,0,.75f,1,0,0};
 D8VertexBuffer p={.device=&device,.shadow=(void*)first,.length=sizeof(first)},s={.device=&device,.shadow=(void*)second,.length=sizeof(second)};
 device.streams[15].buffer=&p;device.streams[15].stride=24;device.streams[1].buffer=&s;device.streams[1].stride=0;
 reset_status(&device);CHECK(device_draw_primitive(&device,1,1,1)==0&&device.clip_status.clip_union==0&&emits==1);
 unsigned before=emits;D9WGClipStatus status=device.clip_status;
 device.creation.BehaviorFlags=0;
 s.length=23;CHECK(device_draw_primitive(&device,1,1,1)==-1&&unchanged(&device,before,status));s.length=24;
 s.handle=42;s.lock_offset=0;s.lock_size=24;s.lock_flags=8192;s.locked=TRUE;CHECK(device_draw_primitive(&device,1,1,1)==0&&s.locked&&!s.lock_flags&&uploads==1);s.locked=FALSE;before=emits;status=device.clip_status;
 s.device=&other;CHECK(device_draw_primitive(&device,1,1,1)==-1&&unchanged(&device,before,status));s.device=&device;
 CHECK(device_draw_primitive(&device,1,3,1)==-1&&unchanged(&device,before,status));
 CHECK(device_draw_primitive(&device,1,1,3)==-1&&unchanged(&device,before,status));device.creation.BehaviorFlags=0x20;
 device.clip_planes[0][0]=1;device.clip_planes[0][3]=-.25f;device.render_states[152]=1;
 device.render_states[170]=0x3e800000;reset_status(&device);CHECK(device_draw_primitive(&device,1,1,1)==0&&(device.clip_status.clip_union&64)&&(device.clip_status.clip_intersection&64));
 device.render_states[170]=0x3f400000;reset_status(&device);CHECK(device_draw_primitive(&device,1,1,1)==0&&device.clip_status.clip_union==0);
 device.render_states[152]=0;
 uint16_t indices[3]={0,0,0};D8IndexBuffer ib={.device=&device,.shadow=(void*)indices,.length=sizeof(indices),.format=101};device.index_buffer=&ib;device.base_vertex_index=1;
 reset_status(&device);CHECK(device_draw_indexed(&device,1,0,2,0,1)==0&&(device.clip_status.clip_union&2)); /* unreferenced physical vertex 2 is right of clip volume */
 status=device.clip_status;before=emits;indices[0]=2;CHECK(device_draw_indexed(&device,1,0,2,0,1)==0&&!device.clip_status_valid);indices[0]=0;
 CHECK(device_draw_indexed(&device,1,0xffffffffu,2,0,1)==0);before=emits;status=device.clip_status;
 CHECK(device_draw_indexed(&device,1,0xffffffffu,0,0,1)==0);before=emits;status=device.clip_status;
 CHECK(device_draw_primitive(&device,4,0xffffffffu,0)==0&&unchanged(&device,before,status));
 CHECK(device_draw_indexed(&device,5,0xffffffffu,0,0xffffffffu,0)==0&&unchanged(&device,before,status));
 device.base_vertex_index=0xffffffffu;indices[0]=2;CHECK(device_draw_indexed(&device,1,2,1,0,1)==0&&(device.clip_status.clip_union&2));
 before=emits;status=device.clip_status;indices[0]=0;CHECK(device_draw_indexed(&device,1,0,0,0,1)==0&&!device.clip_status_valid);before=emits;status=device.clip_status;
 device.base_vertex_index=1;indices[0]=0;
 CHECK(device_draw_indexed(&device,1,0,2,3,1)==-1&&unchanged(&device,before,status));
 ib.handle=43;ib.lock_size=6;ib.lock_flags=4096;ib.locked=TRUE;CHECK(device_draw_indexed(&device,1,0,2,0,1)==0&&ib.locked&&ib.lock_flags==4096&&uploads==2);ib.locked=FALSE;before=emits;status=device.clip_status;
 p.handle=41;p.lock_size=p.length;p.lock_flags=s.lock_flags=8192;p.locked=s.locked=TRUE;
 ib.locked=TRUE;ib.lock_flags=8192;ib.lock_offset=0xffffffffu;sequence_count=0;unsigned uploads_before=uploads;
 CHECK(device_draw_indexed(&device,4,0xffffffffu,0,0xffffffffu,0)==0&&uploads==uploads_before&&!sequence_count&&ib.locked&&ib.lock_flags==8192);ib.locked=FALSE;ib.lock_offset=0;
 device.clip_status=(D9WGClipStatus){0x80000000u,0x80000000u};status=device.clip_status;before=emits;upload_fail_at=uploads+2;
 CHECK(device_draw_primitive(&device,1,1,1)==-3&&emits==before&&unchanged(&device,before,status)&&sequence_count==2&&!memcmp(sequence,"UU",2)&&p.locked&&s.locked&&p.lock_flags==8192&&!s.lock_flags);
 upload_fail_at=0;sequence_count=0;CHECK(device_draw_primitive(&device,1,1,1)==0&&sequence_count==3&&!memcmp(sequence,"UUD",3)&&device.clip_status_valid&&p.locked&&s.locked&&!p.lock_flags&&!s.lock_flags);p.locked=s.locked=FALSE;
 reset_status(&device);before=emits;status=device.clip_status;transport_ok=0;CHECK(device_draw_primitive(&device,1,2,1)==-3&&emits==before+1&&!memcmp(&status,&device.clip_status,sizeof(status)));transport_ok=1;
 before=emits;fail_at=attempts+1;CHECK(device_draw_primitive(&device,1,1,1)==-2&&unchanged(&device,before,status));fail_at=attempts+2;CHECK(device_draw_primitive(&device,1,1,1)==-2&&unchanged(&device,before,status));fail_at=0;
 /* GPU programs need only their declaration's bytes, not a fixed POSITION. */
 declaration.declaration_only=FALSE;declaration.element_count=1;declaration.elements[0]=(D9WGVertexElement){15,0,0,0,5,0};
 CHECK(device_draw_primitive(&device,1,1,1)==0&&!device.clip_status_valid);
 device.render_states[136]=0;CHECK(device_draw_primitive(&device,1,1,1)==0&&device.clip_status_valid&&device.clip_status.clip_union==0);
 device.render_states[136]=1;declaration.declaration_only=TRUE;declaration.elements[0]=(D9WGVertexElement){15,0,2,0,0,0};device.render_states[151]=0;
 CHECK(device_draw_primitive(&device,1,1,1)==0&&device.clip_status_valid);
 /* Last vertex needs its declared extent, not unused stride padding. */
 p.length=36;CHECK(device_draw_primitive(&device,1,1,1)==0);p.length=35;CHECK(device_draw_primitive(&device,1,1,1)==-1);p.length=sizeof(first);
 device.streams[15].stride=0;p.length=12;CHECK(device_draw_primitive(&device,1,0xffffffffu,1)==0);device.streams[15].stride=24;p.length=sizeof(first);
 /* UP must reject any nonzero declaration stream without clearing bindings. */
 device.creation.BehaviorFlags=0;before=emits;status=device.clip_status;CHECK(device_draw_up(&device,1,1,first,24)==-1&&unchanged(&device,before,status)&&device.streams[15].buffer==&p);
 CHECK(device_draw_indexed_up(&device,1,0,1,1,indices,101,first,24)==-1&&device.index_buffer==&ib&&unchanged(&device,before,status));
 declaration.elements[0].stream=0;device.streams[0].buffer=&p;device.streams[0].stride=24;
 CHECK(device_draw_up(&device,1,1,first,11)==-1&&device.streams[0].buffer==&p);
 CHECK(device_draw_up(&device,1,1,first,24)==0&&!device.streams[0].buffer&&releases==1);
 device.streams[0].buffer=&p;device.streams[0].stride=24;CHECK(device_draw_indexed_up(&device,1,0,1,1,indices,101,first,24)==0&&!device.streams[0].buffer&&!device.index_buffer&&releases==3);
 /* FVF compatibility: ordinary transformed buffer draws retain ClipStatus. */
 device.creation.BehaviorFlags=0x20;device.vertex_shader=4;float screen[4]={32,32,.5f,1};p.shadow=(void*)screen;p.length=16;device.streams[0].buffer=&p;device.streams[0].stride=16;status=device.clip_status;
 CHECK(device_draw_primitive(&device,1,0,1)==0&&!memcmp(&status,&device.clip_status,sizeof(status)));
 void *guard=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(guard!=MAP_FAILED);
 device.index_buffer=&ib;device.base_vertex_index=0;ib.shadow=guard;ib.length=6;
 CHECK(device_draw_indexed(&device,4,0,1,0,1)==0);CHECK(munmap(guard,4096)==0);
 CHECK(allocations==frees);puts("PASS actual D3D8 draw C: multi/constant/deleted declarations, all stream extents, locks/owners, indexed binding bounds/ignored hints/signed base/zero no-ops, Tween/user-plane ClipStatus, shader availability, UP restrictions, atomic allocation/transport failure and FVF compatibility");return 0;
}
`;
try{fs.writeFileSync(path.join(directory,"windows.h"),"/* Win32 allocation is provided by the harness. */\n");const file=path.join(directory,"draw.c"),binary=path.join(directory,"draw");fs.writeFileSync(file,code);const sources=["clip","clip_draw","blend","vertex","fvf","math","lighting","tnl_math","buffer_lock"].map(n=>path.join(root,"runtime/d9wg_"+n+".c"));const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",directory,"-I",root,"-I",path.join(root,"runtime"),file,...sources,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||"native run signal "+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(directory,{recursive:true,force:true});}
