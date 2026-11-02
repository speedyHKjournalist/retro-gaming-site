"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_PROCESS_INPUT_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-process-input-"));
function extract(name){const m=new RegExp("static (?:BOOL|void|HRESULT) "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const input=/typedef struct D8ProcessInput \{[\s\S]*?\} D8ProcessInput;/.exec(source);assert(input);
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_fvf.h"
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT;
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D8WG_TRACE_ERROR(x) (x)
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D8WG_MAX_STREAMS 16
#define D8WG_MAX_VERTEX_ELEMENTS 32
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
#define CopyMemory memcpy
static unsigned allocations,frees,attempts,fail_at;
static void *HeapAlloc(void *heap,unsigned flags,size_t size){(void)heap;if(++attempts==fail_at)return NULL;void *p=flags?calloc(1,size):malloc(size);if(p)allocations++;return p;}
static int HeapFree(void *heap,unsigned flags,void *p){(void)heap;(void)flags;if(p){frees++;free(p);}return 1;}
typedef struct D8Device D8Device;typedef struct D8VertexBuffer {D8Device *device;uint8_t *shadow;UINT length;BOOL locked;} D8VertexBuffer;
typedef struct D8Shader {BOOL declaration_only,deleted;UINT element_count;D9WGVertexElement elements[32];} D8Shader;
struct D8Device {DWORD vertex_shader;D8Shader *vertex_shader_ref;struct {D8VertexBuffer *buffer;UINT stride;} streams[16];};
static BOOL fvf_to_elements(DWORD fvf,D9WGVertexElement *elements,UINT maximum,UINT *count){return d9wg_fvf_decode(8,fvf,elements,maximum,count,NULL);}
${input[0]}
${['d8_current_source_elements','d8_process_input_free','d8_process_input'].map(extract).join('\n')}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D8 process input line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 D8Device device={.vertex_shader=0x12},other={0};float fvfs[12]={-.5f,0,.25f,-1,0,0,-.25f,0,.25f,-1,0,0};D8VertexBuffer fvf={&device,(void*)fvfs,sizeof(fvfs),FALSE};device.streams[0].buffer=&fvf;device.streams[0].stride=24;D8ProcessInput *input;
 CHECK(d8_process_input(&device,NULL,0,2,&input)==0&&!input->gather&&input->fast_layout.position==0&&input->fast_layout.normal==12);d8_process_input_free(input);
 device.streams[0].stride=23;CHECK(d8_process_input(&device,NULL,0,2,&input)==D3DERR_INVALIDCALL&&!input);device.streams[0].stride=24;fvf.locked=TRUE;CHECK(d8_process_input(&device,NULL,0,1,&input)==D3DERR_INVALIDCALL&&!input);fvf.locked=FALSE;fvf.device=&other;CHECK(d8_process_input(&device,NULL,0,1,&input)==D3DERR_INVALIDCALL&&!input);fvf.device=&device;device.streams[0].stride=0;CHECK(d8_process_input(&device,NULL,0xffffffffu,2,&input)==0&&input->gather);d8_process_input_free(input);device.streams[0].stride=24;
 D8Shader declaration={.declaration_only=TRUE,.deleted=TRUE,.element_count=4,.elements={{15,0,2,0,0,0},{1,0,2,0,3,0},{7,0,2,0,0,1},{7,12,2,0,3,1}}};device.vertex_shader=1;device.vertex_shader_ref=&declaration;device.streams[0].buffer=NULL;
 float primary[6]={-.5f,0,.25f,-.25f,0,.25f},normal[3]={-1,0,0},second[6]={.5f,0,.75f,1,0,0};D8VertexBuffer p={&device,(void*)primary,sizeof(primary),FALSE},n={&device,(void*)normal,sizeof(normal),FALSE},s={&device,(void*)second,sizeof(second),FALSE};
 device.streams[15].buffer=&p;device.streams[15].stride=12;device.streams[1].buffer=&n;device.streams[1].stride=0;device.streams[7].buffer=&s;device.streams[7].stride=0;
 CHECK(d8_process_input(&device,NULL,0,2,&input)==0&&input->gather&&input->layout.stream_mask==((1u<<15)|(1u<<1)|(1u<<7)));
 float gathered[16];CHECK(d9wg_process_stream_gather(&input->layout,input->streams,1,gathered)&&gathered[0]==-.25f&&gathered[4]==-1&&gathered[8]==.5f&&gathered[12]==1);d8_process_input_free(input);
 unsigned before=attempts;declaration.declaration_only=FALSE;CHECK(d8_process_input(&device,NULL,0,1,&input)==D3DERR_INVALIDCALL&&!input&&attempts==before);declaration.declaration_only=TRUE;
 CHECK(d8_process_input(&device,NULL,0xffffffffu,2,&input)==D3DERR_INVALIDCALL&&!input);s.length=23;CHECK(d8_process_input(&device,NULL,0,2,&input)==D3DERR_INVALIDCALL&&!input);s.length=24;s.locked=TRUE;CHECK(d8_process_input(&device,NULL,0,2,&input)==D3DERR_INVALIDCALL&&!input);s.locked=FALSE;
 device.streams[15].stride=0;p.length=12;CHECK(d8_process_input(&device,NULL,0xffffffffu,2,&input)==0&&input->gather);CHECK(d9wg_process_stream_gather(&input->layout,input->streams,0xffffffffu,gathered)&&gathered[0]==-.5f);d8_process_input_free(input);
 uint8_t bytes[80];memset(bytes,0,sizeof(bytes));memcpy(bytes,primary,24);D8VertexBuffer alias={&device,bytes,sizeof(bytes),FALSE};device.streams[15].buffer=&alias;device.streams[15].stride=12;device.streams[7].buffer=&alias;
 before=allocations;CHECK(d8_process_input(&device,&alias,0,2,&input)==0&&input->snapshot&&allocations==before+2&&input->streams[15].vertices==input->snapshot&&input->streams[7].vertices==input->snapshot&&memcmp(input->snapshot,bytes,sizeof(bytes))==0);
 float changed=99;memcpy(bytes,&changed,4);CHECK(d9wg_process_stream_gather(&input->layout,input->streams,0,gathered)&&gathered[0]==-.5f&&gathered[8]==-.5f);d8_process_input_free(input);
 for(unsigned failure=1;failure<=2;++failure){fail_at=attempts+failure;CHECK(d8_process_input(&device,&alias,0,2,&input)==E_OUTOFMEMORY&&!input);fail_at=0;float value;memcpy(&value,bytes,4);CHECK(value==99);}
 CHECK(allocations==frees);return 0;
}
`;
try{const file=path.join(directory,"input.c"),binary=path.join(directory,"input");fs.writeFileSync(file,code);const sources=["clip","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>path.join(root,"runtime/d9wg_"+n+".c"));const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),file,...sources,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D8 process input C: FVF/retained declarations, arbitrary primary stream, multi/constant layouts, UINT_MAX, owner/lock/range checks, shared alias snapshot and atomic allocation failures");}finally{fs.rmSync(directory,{recursive:true,force:true});}
