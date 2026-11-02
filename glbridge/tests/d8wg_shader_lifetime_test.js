"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_SHADER_LIFETIME_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-shader-life-"));
function extract(name){const m=new RegExp("static (?:HRESULT(?: WINAPI)?|BOOL|void|D8Shader \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const names=["find_shader","emit_destroy_shader","shader_collect","shader_assign","emit_set_fvf","device_bind_vertex_shader","device_set_vertex_shader","device_get_vertex_shader","device_bind_pixel_shader","device_set_pixel_shader","device_get_pixel_shader","device_delete_vertex_shader","device_delete_pixel_shader","device_get_vs_function","device_get_ps_function"];
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef uint32_t DWORD,UINT;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D8WG_TRACE_ERROR(x) (x)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define D8WG_MAX_VERTEX_ELEMENTS 32
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
static int g_transport_lock;
#define GetProcessHeap() 0
static unsigned allocations,frees;static uint32_t destroyed[64],destroyed_kind[64];static unsigned destroyed_count,emissions;static int transport_ok=1;
static void release(void *p){if(p){frees++;free(p);}}
#define HeapFree(heap,flags,pointer) release(pointer)
typedef struct D8Device D8Device;typedef struct D8Shader D8Shader;
struct D8Shader {D8Shader *next;D8Device *device;UINT state_refs;BOOL deleted;uint32_t handle,declaration_handle;BOOL is_pixel_shader,declaration_only;DWORD *declaration_tokens,*code_tokens;UINT declaration_token_count,code_token_count;};
typedef struct D8StateSnapshot {BOOL vertex_shader_mask,pixel_shader_mask;DWORD vertex_shader,pixel_shader;D8Shader *vertex_shader_ref,*pixel_shader_ref;} D8StateSnapshot;
typedef struct D8StateBlock {D8StateSnapshot state;} D8StateBlock;
struct D8Device {uint32_t handle;D8Shader *vertex_shaders,*pixel_shaders,*vertex_shader_ref,*pixel_shader_ref;DWORD vertex_shader,pixel_shader;D8StateBlock *recording_state_block;};
typedef D8Device IDirect3DDevice8;
static D8Device *device_from_iface(D8Device *p){return p;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t size){if(op!=D9WG_OP_DESTROY_RESOURCE||size!=sizeof(D9WGDestroyResource)||destroyed_count==64)abort();const D9WGDestroyResource *v=p;destroyed[destroyed_count]=v->resource_handle;destroyed_kind[destroyed_count++]=v->resource_kind;return transport_ok;}
static uint32_t last_shader_op,last_shader_handle;static uint8_t wire_buffer[1024];
static BOOL emit_set_shader(uint32_t op,D8Device *device,uint32_t shader){(void)device;last_shader_op=op;last_shader_handle=shader;emissions++;return transport_ok;}
static BOOL fvf_to_elements(DWORD fvf,D9WGVertexElement *elements,UINT capacity,UINT *count){(void)capacity;if(!fvf)return FALSE;memset(elements,0,sizeof(*elements));*count=1;return TRUE;}
static BOOL reserve_command_locked(uint32_t op,uint32_t size,uint32_t blob,void *unused,uint8_t **payload,uint8_t **data){(void)unused;if(op!=D9WG_OP_SET_FVF||size!=sizeof(D9WGSetFVF)||blob>512||last_shader_op!=D9WG_OP_SET_VERTEX_SHADER||last_shader_handle)abort();emissions++;*payload=wire_buffer;*data=wire_buffer+size;return transport_ok;}
static BOOL emit_bind_vertex_shader(D8Device *device,D8Shader *shader){(void)device;if(!shader)abort();emissions++;return transport_ok;}
${names.map(extract).join('\n')}
static D8Shader *make(D8Device *device,int pixel,uint32_t handle){D8Shader *p=calloc(1,sizeof(*p));allocations++;p->device=device;p->handle=handle;p->is_pixel_shader=pixel;p->code_tokens=malloc(8);allocations++;p->code_tokens[0]=pixel?0xffff0101u:0xfffe0101u;p->code_tokens[1]=1;p->code_token_count=2;if(!pixel){p->declaration_handle=handle+1000;p->declaration_tokens=malloc(4);allocations++;}D8Shader **head=pixel?&device->pixel_shaders:&device->vertex_shaders;p->next=*head;*head=p;return p;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"shader lifetime line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){D8Device device={0};D8StateBlock block={0};DWORD handle,size,data[3];
 for(unsigned pixel=0;pixel<2;++pixel){
  HRESULT (*set)(IDirect3DDevice8*,DWORD)=pixel?device_set_pixel_shader:device_set_vertex_shader;
  HRESULT (*get)(IDirect3DDevice8*,DWORD*)=pixel?device_get_pixel_shader:device_get_vertex_shader;
  HRESULT (*remove_shader)(IDirect3DDevice8*,DWORD)=pixel?device_delete_pixel_shader:device_delete_vertex_shader;
  HRESULT (*function)(IDirect3DDevice8*,DWORD,void*,DWORD*)=pixel?device_get_ps_function:device_get_vs_function;
  D8Shader **live=pixel?&device.pixel_shader_ref:&device.vertex_shader_ref,**saved=pixel?&block.state.pixel_shader_ref:&block.state.vertex_shader_ref;
  unsigned base=pixel?4:0,unit=pixel?1:2;uint32_t first=1+pixel*20,second=first+2;D8Shader *a=make(&device,pixel,first),*b=make(&device,pixel,second);CHECK(set(&device,first)==0&&a->state_refs==1);
  unsigned before=emissions;device.recording_state_block=&block;CHECK(set(&device,first)==0&&a->state_refs==2);CHECK(set(&device,first)==0&&a->state_refs==2);CHECK(set(&device,second)==0&&a->state_refs==1&&b->state_refs==1);CHECK(get(&device,&handle)==0&&handle==first&&*saved==b);CHECK(emissions==before);device.recording_state_block=NULL;
  CHECK(remove_shader(&device,second)==0&&b->deleted&&b->state_refs==1);CHECK(!find_shader(pixel?device.pixel_shaders:device.vertex_shaders,second));CHECK(set(&device,second)==-1&&remove_shader(&device,second)==-1);size=0xcccccccc;CHECK(function(&device,second,NULL,&size)==-1&&size==0xcccccccc);CHECK(destroyed_count==base);
  CHECK((pixel?device_bind_pixel_shader(&device,second,*saved):device_bind_vertex_shader(&device,second,*saved))==0);CHECK(b->state_refs==2&&a->state_refs==0);CHECK(get(&device,&handle)==0&&handle==second);
  shader_assign(saved,*live);CHECK(b->state_refs==2);shader_assign(saved,NULL);CHECK(b->state_refs==1&&destroyed_count==base);CHECK(set(&device,0)==0);if(!pixel){CHECK(((D9WGSetFVF*)wire_buffer)->fvf==0&&((D9WGSetFVF*)wire_buffer)->element_count==0);}CHECK(destroyed_count==base+(pixel?1:2));CHECK(destroyed[base]==second);CHECK(destroyed_kind[base]==(pixel?D9WG_RESOURCE_PIXEL_SHADER:D9WG_RESOURCE_VERTEX_SHADER));if(!pixel)CHECK(destroyed[base+1]==second+1000&&destroyed_kind[base+1]==D9WG_RESOURCE_VERTEX_DECLARATION);
  /* Deleting a live object records an unbind while preserving live queries. */
  CHECK(set(&device,first)==0);device.recording_state_block=&block;before=emissions;CHECK(remove_shader(&device,first)==0);CHECK(get(&device,&handle)==0&&handle==first&&*live==a&&a->deleted&&a->state_refs==1);CHECK(!*saved&&emissions==before);CHECK(set(&device,first)==-1);device.recording_state_block=NULL;CHECK(set(&device,0)==0);if(!pixel){CHECK(((D9WGSetFVF*)wire_buffer)->fvf==0&&((D9WGSetFVF*)wire_buffer)->element_count==0);}CHECK(destroyed_count==base+unit*2);
  (void)data;
 }
 /* Failed binding must preserve the previous reference and live value. */
 D8Shader *a=make(&device,0,51),*b=make(&device,0,53);CHECK(device_set_vertex_shader(&device,51)==0);transport_ok=0;CHECK(device_set_vertex_shader(&device,53)==-2);CHECK(device.vertex_shader_ref==a&&device.vertex_shader==51&&a->state_refs==1&&b->state_refs==0);transport_ok=1;CHECK(device_delete_vertex_shader(&device,53)==0);CHECK(device_delete_vertex_shader(&device,51)==0);CHECK(!device.vertex_shaders&&!device.pixel_shaders&&allocations==frees);return 0;}
`;
try{const input=path.join(directory,"lifetime.c"),binary=path.join(directory,"lifetime");fs.writeFileSync(input,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",input,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D8 shader C: expired public handles, retained VS/PS snapshots and bindings, same-reference Capture, recorded deletion, complete shader/declaration destruction and failed-bind atomicity");}finally{fs.rmSync(directory,{recursive:true,force:true});}
