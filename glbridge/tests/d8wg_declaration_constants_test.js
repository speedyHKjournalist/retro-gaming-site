"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_DECL_CONSTANT_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-decl-constants-"));
function extract(name){const match=new RegExp("static (?:HRESULT(?: WINAPI)?|BOOL|void|D8Shader \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let at=source.indexOf("{",match.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")++depth;if(source[at]==="}")--depth;}return source.slice(match.index,at);}
const struct=/struct D8Shader \{[\s\S]*?\n\};/.exec(source);assert(struct);
const names=["shader_bytecode_hash","validate_vertex_declaration","declaration_to_elements","find_shader","emit_create_vertex_shader","emit_destroy_shader","shader_collect","shader_assign","emit_bind_vertex_shader","device_bind_vertex_shader","device_set_vertex_shader","device_get_vertex_shader","device_create_vertex_shader","device_delete_vertex_shader","device_get_vs_decl","device_get_vs_function","emit_set_shader_constant","device_set_vs_constant","device_get_vs_constant"];
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "${process.env.D8WG_DECL_ONLY_PROTOCOL||path.join(root,'d3d8proxy/d3d8_protocol.h')}"
#include "${process.env.D8WG_DECLARATION8_HEADER||path.join(root,'runtime/d9wg_declaration8.h')}"
#include "${process.env.D8WG_LOCAL_DEFS_HEADER||path.join(root,'runtime/d9wg_shader8_local_defs.h')}"
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
#define FillMemory(p,n,v) memset(p,v,n)
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
typedef struct D8StateSnapshot {float vs_constants[96][4];BYTE vs_constant_mask[96];BOOL vertex_shader_mask;DWORD vertex_shader;D8Shader *vertex_shader_ref;} D8StateSnapshot;
typedef struct D8StateBlock {D8StateSnapshot state;} D8StateBlock;
struct D8Device {float vs_constants[96][4];uint32_t handle;D8Shader *vertex_shaders,*pixel_shaders,*vertex_shader_ref;DWORD vertex_shader;D8StateBlock *recording_state_block;};
typedef D8Device IDirect3DDevice8;
static D8Device *device_from_iface(D8Device *p){return p;}
static uint8_t wire[65536];static uint32_t wire_at=8,ops[128],offsets[128],wire_count;static int g_transport_lock;
static uint8_t *batch_base(void){return wire;}
static BOOL reserve_command_locked(uint32_t op,uint32_t size,uint32_t bytes,void *unused,uint8_t **payload,uint8_t **data){(void)unused;if(op==D9WG_OP_CREATE_VERTEX_SHADER&&!bytes)return FALSE;if(wire_count==128||wire_at+size+bytes>65536)abort();ops[wire_count]=op;offsets[wire_count++]=wire_at;*payload=wire+wire_at;*data=*payload+size;memset(*payload,0,size+bytes);wire_at+=size+bytes;return TRUE;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t size){uint8_t *payload,*data;if(!reserve_command_locked(op,size,0,NULL,&payload,&data))return FALSE;memcpy(payload,p,size);return TRUE;}
static BOOL emit_set_shader(uint32_t op,D8Device *device,uint32_t handle){D9WGSetShader p={device->handle,handle};return emit_command(op,&p,sizeof(p));}
static BOOL emit_set_fvf(D8Device *device,uint32_t fvf){D9WGSetFVF p={device->handle,fvf,0,0};return emit_command(D9WG_OP_SET_FVF,&p,sizeof(p));}
static BOOL validate_shader_body(const DWORD*p,UINT maximum,BOOL pixel,UINT minor,UINT*count){(void)pixel;(void)minor;for(UINT i=0;i<maximum;i++)if(p[i]==0xffff){*count=i;return TRUE;}return FALSE;}
static DWORD allocate_shader_handle(void){static DWORD next=1;DWORD handle=next;next+=2;return handle;}
static uint32_t allocate_handle(void){static uint32_t next=100;return next++;}
${names.map(extract).join('\n')}
static UINT checks,decode_cases,bounds,constructors;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"declaration constants line %u: %s\\n",__LINE__,#x);return 1;}checks++;}while(0)
static uint64_t refhash(const DWORD*c,UINT n){uint64_t h=UINT64_C(0xcbf29ce484222325);for(UINT i=0;i<n;i++)for(UINT j=0;j<4;j++){h^=(c[i]>>(j*8))&255;h*=UINT64_C(0x1000001b3);}return h;}
static void reset_wire(void){wire_at=8;wire_count=0;}
int main(void){
 for(UINT start=0;start<98;start++)for(UINT count=0;count<16;count++)for(UINT variant=0;variant<8;variant++){
  DWORD tokens[160]={0x20000000,0x40030000};UINT n=2;tokens[n++]=0x80000000u|(count<<25)|start;DWORD expected[384];BYTE mask[96]={0};for(UINT i=0;i<384;i++)expected[i]=0xabababab;const DWORD bits[]={0xffffffff,0x80000000,0x7fc00001,0x3f800000,0x20000000,0xfffe0101,0x0000ffff,0x40000000};
  for(UINT v=0;v<count;v++)for(UINT c=0;c<4;c++){DWORD value=bits[(variant+v+c)%8];tokens[n++]=value;if(start+v<96){expected[(start+v)*4+c]=value;mask[start+v]=1;}}
  UINT second=variant==0?95:variant==1?start:variant*9;tokens[n++]=0x82000000u|second;for(UINT c=0;c<4;c++){DWORD value=bits[(variant+c+3)%8];tokens[n++]=value;if(second<96){expected[second*4+c]=value;mask[second]=1;}}tokens[n]=0xffffffff;
  UINT length=999;CHECK(validate_vertex_declaration(tokens,n+1,&length)&&length==n);for(UINT limit=0;limit<=n;limit++){length=999;CHECK(!validate_vertex_declaration(tokens,limit,&length)&&length==999);bounds++;}
  struct{DWORD pre;DWORD data[384];DWORD post;}out={.pre=0x1234abcd,.post=0xdcba4321};struct{DWORD pre;BYTE values[96];DWORD post;}m={.pre=0x1234abcd,.post=0xdcba4321};for(UINT i=0;i<384;i++)out.data[i]=0xabababab;D9WGVertexElement elements[32];UINT elements_n=999,vectors=999;
  BOOL valid=count&&start+count<=96&&second<96;CHECK(declaration_to_elements(tokens,n,elements,32,&elements_n,out.data,96,m.values,&vectors)==valid);CHECK(out.pre==0x1234abcd&&out.post==0xdcba4321&&m.pre==0x1234abcd&&m.post==0xdcba4321);
  if(valid){UINT total=0;for(UINT reg=0;reg<96;reg++){total+=mask[reg];CHECK(m.values[reg]==mask[reg]);if(mask[reg])CHECK(!memcmp(out.data+reg*4,expected+reg*4,16));}CHECK(vectors==total&&elements_n==1&&elements[0].type==3);}
  decode_cases++;
 }
 D8Device device={.handle=7};const DWORD function[]={0xfffe0101u,1,0xc00f0000,0x90e40000,1,0xd00f0000,0xa0e40002,0xffff};
 const DWORD declaration[]={0x20000000,0x40030000,0x82000002,0x3f800000,0,0,0x3f800000,0x82000008,0,0x3f800000,0,0x3f800000,0x8200005f,0x7fc00001,0xffffffff,0x80000000,0x3f800000,0xffffffff};
 for(UINT fixed=0;fixed<2;fixed++)for(UINT repeat=0;repeat<64;repeat++){
  reset_wire();DWORD handle=0,size=0,got=0;CHECK(!device_create_vertex_shader(&device,declaration,fixed?NULL:function,&handle,0)&&handle);D8Shader*s=find_shader(device.vertex_shaders,handle);CHECK(s&&s->const_vector_count==3&&s->const_mask[2]&&s->const_mask[8]&&s->const_mask[95]&&!s->const_mask[3]);CHECK(wire_count==(fixed?1u:2u));
  if(!fixed){D9WGCreateVertexShader*q=(void*)(wire+offsets[1]);DWORD*c=(void*)(wire+q->code_offset);CHECK(q->instruction_token_count==25&&c[0]==function[0]);const UINT regs[]={2,8,95};for(UINT v=0;v<3;v++){UINT at=1+v*6;CHECK(c[at]==81&&c[at+1]==(0xa00f0000u|regs[v])&&!memcmp(c+at+2,s->const_data+regs[v]*4,16));}CHECK(!memcmp(c+19,function+1,24));uint64_t hash=refhash(c,25);CHECK(q->bytecode_hash_low==(UINT)hash&&q->bytecode_hash_high==(UINT)(hash>>32)&&s->hash_low==(UINT)hash&&s->hash_high==(UINT)(hash>>32));}
  CHECK(!device_get_vs_decl(&device,handle,NULL,&size)&&size==sizeof(declaration));DWORD original[32];CHECK(!device_get_vs_decl(&device,handle,original,&size)&&!memcmp(original,declaration,size));size=sizeof(original);CHECK(!device_get_vs_function(&device,handle,original,&size)&&size==(fixed?0:sizeof(function))&&(fixed||!memcmp(original,function,size)));
  DWORD bank[384];for(UINT i=0;i<384;i++)bank[i]=0x3e800000+i;CHECK(!device_set_vs_constant(&device,0,bank,96));UINT before=wire_count;CHECK(!device_set_vertex_shader(&device,handle)&&wire_count==before+2&&!device_get_vertex_shader(&device,&got)&&got==handle);DWORD actual[384];CHECK(!device_get_vs_constant(&device,0,actual,96)&&!memcmp(actual,bank,sizeof(bank)));for(UINT i=0;i<384;i++)bank[i]^=0x00012345;CHECK(!device_set_vs_constant(&device,0,bank,96)&&!device_get_vs_constant(&device,0,actual,96)&&!memcmp(actual,bank,sizeof(bank)));CHECK(!device_set_vertex_shader(&device,0)&&!device_set_vertex_shader(&device,handle)&&!device_get_vs_constant(&device,0,actual,96)&&!memcmp(actual,bank,sizeof(bank)));
  before=wire_count;CHECK(emit_create_vertex_shader(&device,s)&&wire_count==before+(fixed?1u:2u));CHECK(!device_delete_vertex_shader(&device,handle)&&!device.vertex_shaders);constructors++;
 }
 DWORD h=0;reset_wire();D8StateBlock block={0};device.recording_state_block=&block;DWORD api[4]={1,2,3,4};CHECK(!device_set_vs_constant(&device,3,api,1)&&block.state.vs_constant_mask[3]&&!memcmp(block.state.vs_constants[3],api,16)&&wire_count==0);device.recording_state_block=NULL;
 const DWORD bad[]={0x20000000,0x40030000,0x82000060,1,2,3,4,0xffffffff};CHECK(device_create_vertex_shader(&device,bad,function,&h,0)==D3DERR_INVALIDCALL&&!h&&!device.vertex_shaders&&!wire_count);CHECK(allocations==frees);
 printf("PASS actual declaration constants: %u checks,%u decodes,%u bounded scans,%u public constructors; allblocks/holes/overlap,lastregister,literal END/NaN,private DEF bytes/independent hash,original queries,device bank preservation,rebinding,re-emission,fixed declarations,recorded constants andcleanup\\n",checks,decode_cases,bounds,constructors);return 0;
}
`;
try{const file=path.join(directory,"declaration.c"),binary=path.join(directory,"declaration");fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));process.stdout.write(run.stdout);}finally{fs.rmSync(directory,{recursive:true,force:true});}
