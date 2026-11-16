'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-software-'));
const source=fs.readFileSync(process.env.D9WG_SOFTWARE_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return source.slice(m.index,end);}
try {
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL,WINBOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define FAILED(x) ((x)<0)
#define TRACE_REFUSE(x) (x)
#define UNSUPPORTED(...)
#define PURE_DEVICE_REFUSE(...)
#define CopyMemory memcpy
#define HEAP_ZERO_MEMORY 8
#define D9_MAX_PS_CONST_F 224
#define D9_MAX_CONST_I 16
#define D9_MAX_CONST_B 16
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x20
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x80
typedef struct D9Device D9Device,IDirect3DDevice9;
typedef struct D9StateBlock D9StateBlock;
typedef struct {BOOL requires_software;}D9Shader;
struct D9Device {UINT vs_float_capacity,vs_int_capacity,vs_bool_capacity;float (*vs_const_f)[4],ps_const_f[224][4];int (*vs_const_i)[4],ps_const_i[16][4];BOOL *vs_const_b,ps_const_b[16];D9StateBlock*state_blocks,*recording_result;UINT pins;struct{DWORD BehaviorFlags;}creation;BOOL software_vertex_processing,vertex_shader_hw_disabled;D9Shader*vertex_shader;};
struct D9StateBlock {struct{void*lpVtbl;}iface;UINT refcount;D9Device*device;BOOL device_parent_ref;D9StateBlock*next_device_resource;float (*vs_const_f)[4],ps_const_f[224][4];int (*vs_const_i)[4],ps_const_i[16][4];BOOL *vs_const_b,ps_const_b[16],*has_vs_const_f,has_ps_const_f[224],*has_vs_const_i,has_ps_const_i[16],*has_vs_const_b,has_ps_const_b[16];};
static int g_state_block_vtbl;static BOOL alloc_fail,emit_fail;static UINT emitted,bytes_written,start_written,count_written,checks;static D9Shader*bound;
static unsigned char payload[8192*16];
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*p,UINT flags,size_t n){(void)p;assert(flags==HEAP_ZERO_MEMORY);return alloc_fail?NULL:calloc(1,n);}
static void device_child_add_ref(D9Device*d){d->pins++;}
static D9Device*device_from_iface(D9Device*d){return d;}
static BOOL emit_shader_constants(D9Device*d,UINT op,UINT start,UINT count,const void*p,UINT n){(void)d;(void)op;assert(n<=sizeof(payload));emitted++;bytes_written=n;start_written=start;count_written=count;memcpy(payload,p,n);return !emit_fail;}
static HRESULT emit_set_shader(D9Device*d,D9Shader*s,BOOL pixel){(void)d;assert(!pixel);emitted++;if(emit_fail)return -2;bound=s;return 0;}
${['constant_range_valid','constant_vector_equal','state_block_record_constant','set_constant_f','set_constant_i','set_constant_b','device_get_vertex_shader_constant_f','device_get_vertex_shader_constant_i','device_get_vertex_shader_constant_b','state_block_alloc','d9_software_vertex_processing','device_set_software_vertex_processing','device_get_software_vertex_processing','d9_validate_vertex_shader_mode'].map(extract).join('\n')}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"software constants line %u: %s\\n",__LINE__,#x);abort();}}while(0)
int main(void){
for(UINT extended=0;extended<2;extended++){
 D9Device d={.vs_float_capacity=extended?8192:256,.vs_int_capacity=extended?2048:16,.vs_bool_capacity=extended?2048:16};
 d.vs_const_f=calloc(d.vs_float_capacity,16);d.vs_const_i=calloc(d.vs_int_capacity,16);d.vs_const_b=calloc(d.vs_bool_capacity,4);
 alloc_fail=1;CHECK(!state_block_alloc(&d,TRUE)&&!d.state_blocks&&!d.pins);alloc_fail=0;
 D9StateBlock*b=state_block_alloc(&d,TRUE);CHECK(b&&b->device==&d&&d.state_blocks==b&&d.pins==1&&b->refcount==1);
 float *f=calloc(d.vs_float_capacity,16),*out=calloc(d.vs_float_capacity,16);int *iv=calloc(d.vs_int_capacity,16);BOOL *bv=calloc(d.vs_bool_capacity,4);
 for(UINT i=0;i<d.vs_float_capacity*4;i++)f[i]=(float)i/16;
 for(UINT i=0;i<d.vs_int_capacity*4;i++)iv[i]=(int)i-2048;
 for(UINT i=0;i<d.vs_bool_capacity;i++)bv[i]=i%2?-7:0;
 CHECK(!set_constant_f(&d,FALSE,0,f,d.vs_float_capacity)&&bytes_written==d.vs_float_capacity*16&&start_written==0&&count_written==d.vs_float_capacity);
 CHECK(!device_get_vertex_shader_constant_f(&d,0,out,d.vs_float_capacity)&&!memcmp(f,out,d.vs_float_capacity*16));
 CHECK(!set_constant_i(&d,FALSE,0,iv,d.vs_int_capacity)&&!device_get_vertex_shader_constant_i(&d,0,(int*)out,d.vs_int_capacity)&&!memcmp(iv,out,d.vs_int_capacity*16));
 CHECK(!set_constant_b(&d,FALSE,0,bv,d.vs_bool_capacity)&&!device_get_vertex_shader_constant_b(&d,0,(BOOL*)out,d.vs_bool_capacity));
 for(UINT i=0;i<d.vs_bool_capacity;i++)CHECK(((BOOL*)out)[i]==(BOOL)(i%2));
 UINT prior=emitted;CHECK(!set_constant_f(&d,FALSE,0,f,d.vs_float_capacity)&&emitted==prior);
 for(UINT n=0;n<4;n++){
  UINT start=n==0?d.vs_float_capacity:n==1?d.vs_float_capacity-1:UINT32_MAX,count=n==1?2:n==3?UINT32_MAX:1;
  CHECK(set_constant_f(&d,FALSE,start,f,count)==-1&&device_get_vertex_shader_constant_f(&d,start,out,count)==-1&&emitted==prior);
 }
 CHECK(set_constant_i(&d,FALSE,d.vs_int_capacity,iv,1)==-1&&set_constant_b(&d,FALSE,d.vs_bool_capacity,bv,1)==-1&&set_constant_f(&d,TRUE,224,f,1)==-1&&set_constant_i(&d,TRUE,16,iv,1)==-1&&set_constant_b(&d,TRUE,16,bv,1)==-1);
 d.recording_result=b;for(UINT i=0;i<d.vs_float_capacity*4;i++)f[i]+=1;
 CHECK(!set_constant_f(&d,FALSE,0,f,d.vs_float_capacity)&&!set_constant_i(&d,FALSE,0,iv,d.vs_int_capacity)&&!set_constant_b(&d,FALSE,0,bv,d.vs_bool_capacity)&&emitted==prior);
 for(UINT i=0;i<d.vs_float_capacity;i++)CHECK(b->has_vs_const_f[i]&&!memcmp(b->vs_const_f[i],f+i*4,16)&&d.vs_const_f[i][0]!=f[i*4]);
 for(UINT i=0;i<d.vs_int_capacity;i++)CHECK(b->has_vs_const_i[i]&&!memcmp(b->vs_const_i[i],iv+i*4,16));
 for(UINT i=0;i<d.vs_bool_capacity;i++)CHECK(b->has_vs_const_b[i]&&b->vs_const_b[i]==(BOOL)(i%2));
 free(f);free(out);free(iv);free(bv);free(b);free(d.vs_const_f);free(d.vs_const_i);free(d.vs_const_b);
}
D9Shader sw={TRUE},hw={FALSE};D9Device d={.creation={D3DCREATE_MIXED_VERTEXPROCESSING},.vertex_shader=&sw};
for(UINT round=0;round<32;round++){
 bound=&sw;d.vertex_shader_hw_disabled=FALSE;emitted=0;emit_fail=TRUE;
 CHECK(d9_validate_vertex_shader_mode(&d)==-2&&!d.vertex_shader_hw_disabled&&bound==&sw);emit_fail=FALSE;
 CHECK(d9_validate_vertex_shader_mode(&d)==-1&&d.vertex_shader_hw_disabled&&!bound&&d.vertex_shader==&sw);
 UINT prior=emitted;CHECK(!d9_validate_vertex_shader_mode(&d)&&emitted==prior);
 emit_fail=TRUE;CHECK(device_set_software_vertex_processing(&d,TRUE)==-2&&!d.software_vertex_processing&&d.vertex_shader_hw_disabled);emit_fail=FALSE;
 CHECK(!device_set_software_vertex_processing(&d,TRUE)&&device_get_software_vertex_processing(&d)&&bound==&sw&&!d.vertex_shader_hw_disabled);
 CHECK(!d9_validate_vertex_shader_mode(&d));CHECK(!device_set_software_vertex_processing(&d,FALSE)&&!device_get_software_vertex_processing(&d));
}
d.vertex_shader=&hw;CHECK(!d9_validate_vertex_shader_mode(&d));d.vertex_shader=NULL;CHECK(!d9_validate_vertex_shader_mode(&d));
for(UINT software=0;software<2;software++){d.creation.BehaviorFlags=software?0x20:0x40;CHECK(!device_set_software_vertex_processing(&d,software)&&device_set_software_vertex_processing(&d,!software)==-1);}
printf("PASS actual software constants/modes: %u checks; full banks, boundaries, Boolean normalization, dynamic state block allocation/recording, live isolation, hardware fallback, mode restoration and failed transport atomicity\\n",checks);
}
`;
fs.writeFileSync(dir+'/test.c',code);let r=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
} finally {fs.rmSync(dir,{recursive:true,force:true});}
