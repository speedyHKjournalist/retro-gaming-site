"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),{body,transport,run,GL}=require("./gl_cpu_abi_harness.js");
const source=fs.readFileSync(path.join(__dirname,"../openglproxy/opengl32_proxy.c"),"utf8");
const code=`
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
typedef uint32_t GLenum,GLuint;typedef int32_t GLintptrARB,GLsizeiptrARB;typedef void GLvoid;typedef int BOOL;
#define FALSE 0
#define APIENTRY
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() 0
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
${Object.entries(GL).filter(([,v])=>Number.isInteger(v)&&v>=0).map(([n,v])=>"#define GL_"+n+" 0x"+v.toString(16)+"u").join("\n")}
${transport}
#undef release_encoding_buffer_reads
typedef struct EncodingBufferRead {struct EncodingBufferRead* next;} EncodingBufferRead;
static EncodingBufferRead* g_encoding_buffer_reads;
${["emit_query_buffer_data_for"].map(n=>body(source,n,"static int ")).join("\n")}
${body(source,"release_encoding_buffer_reads","static void ")}
${body(source,"buffer_selected_data_at","static const uint8_t* ")}
int main(void){
 uint32_t bind[2]={GL_ARRAY_BUFFER,7};emit_gl_call(GLFN_BIND_BUFFER,bind,8);
 uint32_t size=600008;uint8_t* payload=malloc(16+size);uint32_t header[4]={GL_ARRAY_BUFFER,size,GL_STATIC_DRAW,size};memcpy(payload,header,16);for(uint32_t i=0;i<size;i++)payload[16+i]=(uint8_t)(i%251);emit_gl_call(GLFN_BUFFER_DATA,payload,16+size);free(payload);
 uint32_t array[4]={3,GL_FLOAT,0,0};emit_gl_call(GLFN_VERTEX_POINTER_VBO,array,16);
 bind[1]=9;emit_gl_call(GLFN_BIND_BUFFER,bind,8);uint32_t other[5]={GL_ARRAY_BUFFER,4,GL_STATIC_DRAW,4,0xAABBCCDD};emit_gl_call(GLFN_BUFFER_DATA,other,20);
 const uint8_t* a=buffer_selected_data_at(GL_ARRAY_BUFFER,7,(void*)4,600000);assert(a&&!error);for(uint32_t i=0;i<600000;i++)assert(a[i]==(uint8_t)((i+4)%251));
 const uint8_t* b=buffer_selected_data_at(GL_ARRAY_BUFFER,7,(void*)21,8);assert(b&&b[0]==21&&a[0]==4&&a[599999]==(uint8_t)(600003%251));
 assert(!buffer_selected_data_at(GL_ARRAY_BUFFER,7,(void*)600004,8)&&error==GL_INVALID_VALUE);error=0;
 assert(!buffer_selected_data_at(GL_ARRAY_BUFFER,71,0,1)&&error==GL_INVALID_OPERATION);error=0;
 V86GLRecordQueryBufferMap map={0};map.target=GL_ARRAY_BUFFER;map.access=GL_READ_ONLY_ARB;map.pointer=7;uint8_t* args;reserve_pci_record(GLFN_QUERY_BUFFER_MAP,NULL,sizeof(map),&args);memcpy(args,&map,sizeof(map));assert(emit_pci_batch(FALSE));
 assert(!buffer_selected_data_at(GL_ARRAY_BUFFER,9,0,1)&&error==GL_INVALID_OPERATION);error=0;
 uint32_t ctx[8]={2,0,0,64,64,2,1,0};emit_gl_call(V86GL_CTRL_MAKE_CURRENT,ctx,32);
 uint32_t deleted[2]={1,7};emit_gl_call(GLFN_DELETE_BUFFERS,deleted,8);bind[1]=7;emit_gl_call(GLFN_BIND_BUFFER,bind,8);uint32_t fresh[5]={GL_ARRAY_BUFFER,4,GL_STATIC_DRAW,4,42};emit_gl_call(GLFN_BUFFER_DATA,fresh,20);
 ctx[0]=1;ctx[5]=1;emit_gl_call(V86GL_CTRL_MAKE_CURRENT,ctx,32);const uint8_t* retained=buffer_selected_data_at(GL_VERTEX_ARRAY,0,(void*)4,8);assert(retained&&retained[0]==4&&retained[7]==11);
emit_gl_call(GLFN_DELETE_BUFFERS,deleted,8);assert(!buffer_selected_data_at(GL_ARRAY_BUFFER,7,0,1)&&error==GL_INVALID_OPERATION);
 assert(a[0]==4&&b[0]==21);release_encoding_buffer_reads();assert(!g_encoding_buffer_reads);return 0;
}`;
run(code,cpu=>{assert.equal(cpu.boundBuffer(GL.ARRAY_BUFFER).name,9);assert.deepEqual([...cpu.boundBuffer(GL.ARRAY_BUFFER).data],[0xDD,0xCC,0xBB,0xAA]);assert.equal(cpu.current.group.buffers.has(7),false);})
 .then(()=>console.log("PASS actual C named VBO staging: bounded reads, stable gathered pointers, CPU ranges/mapping/deletion and unchanged bindings in both transports"))
 .catch(error=>{console.error(error);process.exitCode=1;});
