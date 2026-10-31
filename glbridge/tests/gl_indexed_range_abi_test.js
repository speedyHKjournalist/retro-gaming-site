"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),{body,transport,run,GL}=require("./gl_cpu_abi_harness.js");
const source=fs.readFileSync(path.join(__dirname,"../openglproxy/opengl32_proxy.c"),"utf8");
const type=name=>source.match(new RegExp("typedef struct \\{[^{}]*\\} "+name+";"))[0];
const code=`
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "gl_protocol.h"
typedef uint32_t GLenum,GLuint;typedef int32_t GLint,GLsizei;typedef float GLfloat;typedef unsigned char GLboolean,GLubyte;typedef void GLvoid;typedef int BOOL;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define V86GL_MAX_TEXTURE_UNITS 8
#define V86GL_MAX_VERTEX_ATTRIBS 16
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() 0
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
#define v86gl_trace(...) ((void)0)
static int v86gl_trace_call_allowed(void){return 0;}
${Object.entries(GL).filter(([,v])=>Number.isInteger(v)&&v>=0).map(([n,v])=>"#define GL_"+n+" 0x"+v.toString(16)+"u").join("\n")}
static uint32_t sent;
#define GL_CPU_ABI_BEFORE_SEND (++sent)
${transport}
${["ClientArrayState","ClientArrayCopy","GenericAttribState","GenericAttribCopy"].map(type).join("\n")}
typedef V86GLRecordClientArrayBlock ClientArrayBlockHeader;typedef V86GLRecordGenericAttribBlock GenericAttribBlockHeader;
static ClientArrayState g_vertex_array,g_color_array,g_normal_array,g_secondary_color_array,g_fog_coord_array,g_texcoord_arrays[8];static GenericAttribState g_generic_attribs[16];
static GLuint g_element_array_buffer_binding;static GLenum g_client_active_texture=GL_TEXTURE0;
static const uint8_t* client_buffer_data_at(const ClientArrayState* state,const GLvoid* offset,uint32_t bytes){(void)state;(void)offset;(void)bytes;abort();return NULL;}
static const uint8_t* buffer_selected_data_at(GLenum target,GLuint index,const GLvoid* offset,uint32_t bytes){(void)target;(void)index;(void)offset;(void)bytes;abort();return NULL;}
static const uint8_t* buffer_data_at(GLenum target,const GLvoid* offset,uint32_t bytes){(void)target;(void)offset;(void)bytes;abort();return NULL;}
static BOOL direct;
static BOOL draw_elements_can_use_vbo_direct(void){return direct;}
static BOOL draw_arrays_can_use_vbo_direct(void){return direct;}
static BOOL draw_needs_edge_flags(GLenum mode){(void)mode;return FALSE;}
static void draw_array_elements(GLenum mode,GLint first,GLsizei count,const uint8_t* data,GLenum type){(void)mode;(void)first;(void)count;(void)data;(void)type;abort();}
static uint8_t* alloc_payload(uint32_t size);
${body(source,"pointer_offset_u32","static uint32_t ")}
${body(source,"emit_draw_arrays_direct","static void ")}
${body(source,"emit_draw_elements_direct","static void ")}
${body(source,"emit_multi_draw_arrays_direct","static BOOL ")}
${body(source,"emit_multi_draw_elements_direct","static BOOL ")}

${body(source,"alloc_payload","static uint8_t* ")}
${["gl_type_bytes","client_array_element_size"].map(n=>body(source,n,"static uint32_t ")).join("\n")}
${body(source,"gl_index_bytes","static int ")}
${body(source,"client_array_copy","static int ")}
${body(source,"generic_attrib_copy","static int ")}
${["client_array_blocks_size","enabled_generic_attrib_count","generic_attrib_blocks_size"].map(n=>body(source,n,"static uint32_t ")).join("\n")}
${["write_client_array_block","write_generic_attrib_block"].map(n=>body(source,n,"static uint8_t* ")).join("\n")}
${body(source,"reserve_packed_draw_payload","static int ")}
${body(source,"commit_packed_draw_payload","static void ")}
${body(source,"draw_index_bounds","static int ")}
${body(source,"write_rebased_indices","static void ")}
${body(source,"glDrawArrays","void APIENTRY ")}
${body(source,"glDrawElements","void APIENTRY ")}
${["glDrawRangeElements","glMultiDrawArrays","glMultiDrawElements"].map(n=>body(source,n,"void APIENTRY ")).join("\n")}
int main(void){
 float* vertices=calloc(100003*4,sizeof(float));float* attributes=calloc(100003*4,sizeof(float));assert(vertices&&attributes);
 for(uint32_t i=0;i<100003;i++){vertices[4*i]=(float)i;vertices[4*i+3]=1;attributes[4*i]=(float)i*.5f;attributes[4*i+3]=1;}
 g_vertex_array=(ClientArrayState){TRUE,4,GL_FLOAT,0,vertices,0};g_generic_attribs[7]=(GenericAttribState){TRUE,4,GL_FLOAT,FALSE,0,attributes,0};
 GLubyte a[3]={8,7,9};uint16_t b[3]={50001,50000,50002};uint32_t c[3]={100001,100000,100002};
 glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,a);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,b);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,c);
 assert(!error);
 emit_gl_call((uint16_t)(V86GL_REJECTED_RECORD_BIT|GLFN_QUERY_ERROR),NULL,0);error=GL_INVALID_ENUM;
 GLint first[2]={7,50000};GLsizei counts[2]={3,3};const void* indices[2]={b,c};uint32_t d[3]={50001,50000,50002};indices[0]=d;
 glMultiDrawArrays(GL_TRIANGLES,first,counts,2);glMultiDrawElements(GL_TRIANGLES,counts,GL_UNSIGNED_INT,indices,2);assert(error==GL_INVALID_ENUM);
 uint32_t before=sent;counts[1]=-1;error=0;glMultiDrawArrays(GL_TRIANGLES,first,counts,2);assert(error==GL_INVALID_VALUE&&sent==before);error=0;glMultiDrawElements(GL_TRIANGLES,counts,GL_UNSIGNED_INT,indices,2);assert(error==GL_INVALID_VALUE&&sent==before);counts[1]=3;
 uint32_t bind[2]={GL_ARRAY_BUFFER,5};emit_gl_call(GLFN_BIND_BUFFER,bind,8);uint8_t vbo[160]={0};uint32_t vmeta[4]={GL_ARRAY_BUFFER,144,GL_STATIC_DRAW,144};memcpy(vbo,vmeta,16);emit_gl_call(GLFN_BUFFER_DATA,vbo,sizeof(vbo));uint32_t pointer[4]={4,GL_FLOAT,0,0};emit_gl_call(GLFN_VERTEX_POINTER_VBO,pointer,16);uint32_t enabled=GL_VERTEX_ARRAY;emit_gl_call(GLFN_ENABLE_CLIENT_STATE,&enabled,4);uint32_t seven=7;emit_gl_call(GLFN_DISABLE_VERTEX_ATTRIB_ARRAY,&seven,4);
 bind[0]=GL_ELEMENT_ARRAY_BUFFER;bind[1]=6;emit_gl_call(GLFN_BIND_BUFFER,bind,8);uint8_t ebo[22];uint32_t emeta[4]={GL_ELEMENT_ARRAY_BUFFER,6,GL_STATIC_DRAW,6};uint16_t outside[3]={6,7,8};memcpy(ebo,emeta,16);memcpy(ebo+16,outside,6);emit_gl_call(GLFN_BUFFER_DATA,ebo,sizeof(ebo));g_element_array_buffer_binding=6;direct=TRUE;g_vertex_array.buffer=5;g_vertex_array.pointer=NULL;g_generic_attribs[7].enabled=FALSE;
 glDrawRangeElements(GL_TRIANGLES,0,2,3,GL_UNSIGNED_SHORT,NULL);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
 first[0]=6;first[1]=0;glMultiDrawArrays(GL_TRIANGLES,first,counts,2);indices[0]=indices[1]=NULL;glMultiDrawElements(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,indices,2);
 free(vertices);free(attributes);return 0;
}`;
run(code,(cpu,gpu)=>{
 assert.equal(cpu.current.error,GL.INVALID_ENUM);assert.equal(gpu.ranges.length,7);
 const bases=[7,50000,100000,7,50000,50000,100000];
 for(const [i,base] of bases.entries()){
  const draw=gpu.ranges[i];if(draw.indices)assert.deepEqual(draw.indices,[1,0,2]);assert.equal(draw.bytes,48);assert.equal(draw.genericBytes,48);
  assert.deepEqual(draw.positions,[base,base+1,base+2]);assert.deepEqual(draw.generic,[base*.5,(base+1)*.5,(base+2)*.5]);
 }
 assert.equal(gpu.directDraws,6);assert.equal(gpu.stats.draws,13);assert.equal(gpu.hostErrors.length,0);
},(cpu,gpu)=>{
 gpu.ranges=[];gpu.directDraws=0;const directDraw=gpu.drawFromBuffers,packed=gpu.drawPacked;
 gpu.drawFromBuffers=function(...args){this.directDraws++;this.inDirect=true;try{return directDraw.apply(this,args);}finally{this.inDirect=false;}};
 gpu.drawPacked=function(mode,count,arrays,indices){
  if(!this.inDirect){let values=null;if(indices){const width=indices.type===GL.UNSIGNED_BYTE?1:indices.type===GL.UNSIGNED_SHORT?2:4,view=new DataView(indices.data.buffer,indices.data.byteOffset,indices.data.byteLength),read=[null,"getUint8","getUint16",null,"getUint32"][width];values=Array.from({length:count},(_,i)=>view[read](i*width,true));}
   gpu.ranges.push({bytes:arrays.vertex.data.length,genericBytes:arrays.generic7.data.length,indices:values,positions:[...new Float32Array(arrays.vertex.data.slice().buffer)].filter((_,i)=>i%4===0),generic:[...new Float32Array(arrays.generic7.data.slice().buffer)].filter((_,i)=>i%4===0)});
  }
  return packed.call(this,mode,count,arrays,indices);
 };
})
 .then(()=>console.log("PASS C indexed/multi encoders: min..max byte/short/int/generic copies, hint-exceeding VBO draws, no direct guest readbacks, latched errors and atomic invalid counts in both transports"))
 .catch(error=>{console.error(error);process.exitCode=1;});
