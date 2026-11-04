"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_DECL_ONLY_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-declaration-only-"));
function extract(name){const match=new RegExp("static (?:HRESULT(?: WINAPI)?|BOOL|void|D8Shader \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let at=source.indexOf("{",match.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")++depth;if(source[at]==="}")--depth;}return source.slice(match.index,at);}
const struct=/struct D8Shader \{[\s\S]*?\n\};/.exec(source);assert(struct);
const names=["validate_vertex_declaration","declaration_to_elements","find_shader","emit_create_vertex_shader","emit_destroy_shader","shader_collect","shader_assign","emit_bind_vertex_shader","device_bind_vertex_shader","device_set_vertex_shader","device_get_vertex_shader","device_create_vertex_shader","device_delete_vertex_shader","device_get_vs_decl","device_get_vs_function"];
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "${process.env.D8WG_DECL_ONLY_PROTOCOL||path.join(root,'d3d8proxy/d3d8_protocol.h')}"
#include "${process.env.D8WG_DECLARATION8_HEADER||path.join(root,'runtime/d9wg_declaration8.h')}"
typedef uint32_t DWORD,UINT;typedef unsigned char BYTE;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D8WG_TRACE_ERROR(x) (x)
#define D8WG_TRACE(...) ((void)0)
#define D8WG_MAX_VERTEX_ELEMENTS 32
#define D8WG_MAX_VS_CONSTANTS 96
#define D8WG_MAX_SHADER_TOKENS 8192
#define D8WG_MAX_STREAMS 16
#define D3DVSD_TOKENTYPESHIFT 29
#define D3DVSD_TOKEN_END 7
#define D3DVSD_TOKEN_CONSTMEM 4
#define D3DVSD_CONSTCOUNTMASK D3D8VSD_CONSTCOUNTMASK
#define D3DVSD_CONSTCOUNTSHIFT D3D8VSD_CONSTCOUNTSHIFT
#define D3DSHADER_VERSION_MINOR(v) ((v)&255)
#define D3DSHADER_VERSION_MAJOR(v) (((v)>>8)&255)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
static unsigned allocations,frees,attempts,fail_at;
static void *heap_alloc(void *heap,unsigned flags,size_t size){(void)heap;if(++attempts==fail_at)return NULL;void *p=flags?calloc(1,size):malloc(size);if(p)allocations++;return p;}
static void heap_free(void *heap,unsigned flags,void *p){(void)heap;(void)flags;if(p){frees++;free(p);}}
#define HeapAlloc heap_alloc
#define HeapFree heap_free
typedef struct D8Device D8Device;typedef struct D8Shader D8Shader;
${struct[0]}
typedef struct D8StateSnapshot {BOOL vertex_shader_mask;DWORD vertex_shader;D8Shader *vertex_shader_ref;} D8StateSnapshot;
typedef struct D8StateBlock {D8StateSnapshot state;} D8StateBlock;
struct D8Device {uint32_t handle;D8Shader *vertex_shaders,*pixel_shaders,*vertex_shader_ref;DWORD vertex_shader;D8StateBlock *recording_state_block;};
typedef D8Device IDirect3DDevice8;
static D8Device *device_from_iface(D8Device *p){return p;}
static uint8_t wire[65536];static uint32_t wire_at=8,ops[128],offsets[128],wire_count;static int g_transport_lock;
static uint8_t *batch_base(void){return wire;}
static BOOL reserve_command_locked(uint32_t op,uint32_t size,uint32_t bytes,void *unused,uint8_t **payload,uint8_t **data){(void)unused;if(op==D9WG_OP_CREATE_VERTEX_SHADER&&!bytes)return FALSE;if(wire_count==128||wire_at+size+bytes>65536)abort();ops[wire_count]=op;offsets[wire_count++]=wire_at;*payload=wire+wire_at;*data=*payload+size;memset(*payload,0,size+bytes);wire_at+=size+bytes;return TRUE;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t size){uint8_t *payload,*data;if(!reserve_command_locked(op,size,0,NULL,&payload,&data))return FALSE;memcpy(payload,p,size);return TRUE;}
static BOOL emit_set_shader(uint32_t op,D8Device *device,uint32_t handle){D9WGSetShader p={device->handle,handle};return emit_command(op,&p,sizeof(p));}
static BOOL emit_set_fvf(D8Device *device,uint32_t fvf){D9WGSetFVF p={device->handle,fvf,0,0};return emit_command(D9WG_OP_SET_FVF,&p,sizeof(p));}
static BOOL validate_shader_body(const DWORD *p,UINT maximum,BOOL pixel,UINT minor,UINT *count){(void)p;(void)maximum;(void)pixel;(void)minor;(void)count;abort();}
static void shader_bytecode_hash(const DWORD *p,UINT n,uint32_t *low,uint32_t *high){(void)p;(void)n;(void)low;(void)high;abort();}
static DWORD allocate_shader_handle(void){static DWORD next=1;DWORD handle=next;next+=2;return handle;}
static uint32_t allocate_handle(void){static uint32_t next=100;return next++;}
${names.map(extract).join('\n')}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"declaration-only line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 D8Device device={.handle=7};D8StateBlock recorded={0};DWORD a,b,size,got,words[12];
 const DWORD declaration[]={0x20000000,0x40020000,0x40020003,0x20000001,0x4002000f,0x40020010,0xffffffff};
 CHECK(device_create_vertex_shader(&device,NULL,NULL,&a,0)==D3DERR_INVALIDCALL&&a==0);CHECK(device_create_vertex_shader(&device,declaration,NULL,NULL,0)==D3DERR_INVALIDCALL);
 CHECK(device_create_vertex_shader(&device,declaration,NULL,&a,0)==0&&(a&1)&&wire_count==1&&ops[0]==D9WG_OP_CREATE_VERTEX_DECLARATION);
 D8Shader *first=find_shader(device.vertex_shaders,a);CHECK(first&&first->declaration_only&&!first->code_tokens&&!first->code_token_count&&first->element_count==4);
 const D9WGVertexElement expected[]={{0,0,2,0,0,0},{0,12,2,0,3,0},{1,0,2,0,0,1},{1,12,2,0,3,1}};
 D9WGCreateVertexDeclaration *created=(void*)(wire+offsets[0]);CHECK(created->element_count==4&&memcmp(created+1,expected,sizeof(expected))==0);
 size=0;CHECK(device_get_vs_decl(&device,a,NULL,&size)==0&&size==sizeof(declaration));CHECK(device_get_vs_decl(&device,a,words,&size)==0&&memcmp(words,declaration,size)==0);
 words[0]=0xcccccccc;size=99;CHECK(device_get_vs_function(&device,a,NULL,&size)==0&&size==0);size=sizeof(words);CHECK(device_get_vs_function(&device,a,words,&size)==0&&size==0&&words[0]==0xcccccccc);
 CHECK(device_set_vertex_shader(&device,a)==0&&device_get_vertex_shader(&device,&got)==0&&got==a&&first->state_refs==1);CHECK(ops[1]==D9WG_OP_SET_VERTEX_DECLARATION&&ops[2]==D9WG_OP_SET_VERTEX_SHADER&&((D9WGSetShader*)(wire+offsets[2]))->shader_handle==0);
 CHECK(device_create_vertex_shader(&device,declaration,NULL,&b,0)==0);D8Shader *second=find_shader(device.vertex_shaders,b);unsigned before=wire_count;device.recording_state_block=&recorded;CHECK(device_set_vertex_shader(&device,b)==0&&second->state_refs==1&&device.vertex_shader==a&&wire_count==before);device.recording_state_block=NULL;
 CHECK(device_delete_vertex_shader(&device,b)==0&&second->deleted&&second->state_refs==1&&device_set_vertex_shader(&device,b)==D3DERR_INVALIDCALL);size=0xcccccccc;CHECK(device_get_vs_decl(&device,b,NULL,&size)==D3DERR_INVALIDCALL&&size==0xcccccccc);
 CHECK(device_bind_vertex_shader(&device,b,recorded.state.vertex_shader_ref)==0&&second->state_refs==2);shader_assign(&recorded.state.vertex_shader_ref,NULL);CHECK(second->state_refs==1);CHECK(device_delete_vertex_shader(&device,a)==0&&device_set_vertex_shader(&device,0)==0&&!device.vertex_shaders);
 for(unsigned i=0;i<wire_count;++i){CHECK(ops[i]!=D9WG_OP_CREATE_VERTEX_SHADER);if(ops[i]==D9WG_OP_DESTROY_RESOURCE)CHECK(((D9WGDestroyResource*)(wire+offsets[i]))->resource_kind==D9WG_RESOURCE_VERTEX_DECLARATION);}
 const DWORD constants[]={0x20000000,0x40020000,0x82000003,0x3e800000,0x3f000000,0x3f400000,0x3f800000,0xffffffff};
 CHECK(device_create_vertex_shader(&device,constants,NULL,&a,0)==0&&device_set_vertex_shader(&device,a)==0&&device.vertex_shader_ref->const_vector_count==1&&device.vertex_shader_ref->const_mask[3]&&device.vertex_shader_ref->const_data[12]==0x3e800000&&device.vertex_shader_ref->const_data[15]==0x3f800000&&device_delete_vertex_shader(&device,a)==0);
 const DWORD bad_normal[]={0x20000000,0x40020000,0x40010003,0xffffffff},bad_constants[]={0x20000000,0x40020000,0x82000060,0,0,0,0,0xffffffff};
 CHECK(device_create_vertex_shader(&device,bad_normal,NULL,&a,0)==D3DERR_INVALIDCALL&&a==0&&!device.vertex_shaders);CHECK(device_create_vertex_shader(&device,bad_constants,NULL,&a,0)==D3DERR_INVALIDCALL&&a==0);
 for(unsigned failure=1;failure<=2;++failure){fail_at=attempts+failure;before=wire_count;CHECK(device_create_vertex_shader(&device,declaration,NULL,&a,0)==E_OUTOFMEMORY&&a==0&&!device.vertex_shaders&&wire_count==before);fail_at=0;}
 CHECK(allocations==frees);return 0;
}
`;
try{const file=path.join(directory,"declaration.c"),binary=path.join(directory,"declaration");fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D8 declaration-only C: public creation/query/bind, SDK secondary semantics, no shader resource, deferred retained lifetime, constants, validation and atomic OOM");}finally{fs.rmSync(directory,{recursive:true,force:true});}
