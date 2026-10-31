"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),{body,transport,run,GL}=require("./gl_cpu_abi_harness.js");
const native=JSON.parse(require("node:zlib").gunzipSync(fs.readFileSync(path.join(__dirname,"../../../v86/tests/glbridge/gl_conformance/mesa_reference.json.gz")))).fixtures.array_element.cases;
// GL 2.1 table 2.9. Mesa's reciprocal multiplication differs by one float ULP.
const signed=(v,bits)=>Math.fround((2*v+1)/(2**bits-1));
const reference={normal:[-128,0,64].map(v=>signed(v,8)),generic:[-128,-64,0,127].map(v=>signed(v,8)),normal3b:[-128,-64,127].map(v=>signed(v,8)),normal3s:[-32768,-16384,32767].map(v=>signed(v,16)),normal3i:[-2147483648,-1073741824,2147483647].map(v=>signed(v,32))};
for(const [key,values] of Object.entries(reference))values.forEach((v,i)=>assert.ok(Math.abs(v-native[key][i])<=2**-24,key+" matches the GL 2.1 Mesa oracle within one float ULP"));
const apple=JSON.parse(fs.readFileSync(path.join(__dirname,"../../../v86/tests/glbridge/gl_conformance/array_element_reference.json"),"utf8")).cases;
assert.equal(apple.normal[1],0);assert.equal(Math.fround(native.normal[1]),Math.fround(1/255));

const source=fs.readFileSync(path.join(__dirname,"../openglproxy/opengl32_proxy.c"),"utf8");
function type(name){const m=source.match(new RegExp("typedef struct \\{[^{}]*\\} "+name+";"));assert.ok(m,name);return m[0];}
const code=`
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "gl_protocol.h"
typedef uint32_t GLenum,GLuint;typedef int32_t GLint,GLsizei,GLintptrARB,GLsizeiptrARB;typedef int BOOL;typedef unsigned char GLboolean,GLubyte;typedef signed char GLbyte;typedef short GLshort;typedef unsigned short GLushort;typedef float GLfloat;typedef double GLdouble;typedef void GLvoid;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define V86GL_MAX_TEXTURE_UNITS 8
#define V86GL_MAX_VERTEX_ATTRIBS 16
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() 0
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
${Object.entries(GL).filter(([,v])=>Number.isInteger(v)&&v>=0).map(([n,v])=>"#define GL_"+n+" 0x"+v.toString(16)+"u").join("\n")}
${transport}
#undef release_encoding_buffer_reads
${type("ClientArrayState")}
${type("GenericAttribState")}
static ClientArrayState g_vertex_array,g_normal_array,g_color_array,g_index_array,g_edge_flag_array,g_secondary_color_array,g_fog_coord_array,g_texcoord_arrays[8];
static GenericAttribState g_generic_attribs[16];
typedef struct EncodingBufferRead {struct EncodingBufferRead* next;} EncodingBufferRead;
static EncodingBufferRead* g_encoding_buffer_reads;
${body(source,"emit_query_buffer_data_for","static int ")}
${body(source,"release_encoding_buffer_reads","static void ")}
${body(source,"buffer_selected_data_at","static const uint8_t* ")}
${body(source,"client_buffer_data_at","static const uint8_t* ")}
${body(source,"gl_type_bytes","static uint32_t ")}
${body(source,"client_array_element_size","static uint32_t ")}
${body(source,"normalize_signed_value","static GLfloat ")}
${["byte","short","int","ubyte","ushort","uint","double"].map(n=>body(source,"color_from_"+n,"static GLfloat ")).join("\n")}
${body(source,"client_array_element_ptr","static const uint8_t* ")}
${body(source,"client_array_scalar","static GLfloat ")}
${body(source,"client_array_color_scalar","static GLfloat ")}
${body(source,"client_array_normalized_scalar","static GLfloat ")}
${body(source,"read_client_array_values","static BOOL ")}
${body(source,"read_generic_array_values","static BOOL ")}
static void glColor4f(float x,float y,float z,float w){float p[4]={x,y,z,w};emit_gl_call(GLFN_COLOR4F,p,16);}
static void glNormal3f(float x,float y,float z){float p[3]={x,y,z};emit_gl_call(GLFN_NORMAL3F,p,12);}
${["normal_from_byte","normal_from_short","normal_from_int"].map(n=>body(source,n,"static GLfloat ")).join("\n")}
#define __declspec(value)
${source.slice(source.indexOf("#define DEFINE_NORMAL_VARIANT("),source.indexOf("#undef DEFINE_NORMAL_VARIANT")+"#undef DEFINE_NORMAL_VARIANT".length)}
static void check_normal(GLfloat x,GLfloat y,GLfloat z){V86GLRecordQueryState query={0};query.pname=GL_CURRENT_NORMAL;query.value_kind=V86GL_STATE_DOUBLE;query.data_size=24;uint8_t* args;reserve_pci_record(GLFN_QUERY_STATE,NULL,sizeof(query)+24,&args);memcpy(args,&query,sizeof(query));assert(emit_pci_batch(FALSE));assert(read_u32le(args+offsetof(V86GLRecordQueryState,status))==1);double out[3];memcpy(out,args+sizeof(query),24);assert(out[0]==x&&out[1]==y&&out[2]==z);}
static void glSecondaryColor3f(float x,float y,float z){float p[3]={x,y,z};emit_gl_call(GLFN_SECONDARY_COLOR3F,p,12);}
static void glFogCoordf(float x){emit_gl_call(GLFN_FOG_COORDF,&x,4);}
static void glIndexf(float x){V86GLRecordIndexState p={0};p.value=x;emit_gl_call(GLFN_INDEX_STATE,&p,sizeof(p));}
static void emit_multi_tex_coord4f(GLenum unit,float x,float y,float z,float w){struct{uint32_t unit;float v[4];}p={unit,{x,y,z,w}};emit_gl_call(GLFN_MULTI_TEX_COORD4F,&p,sizeof(p));}
static void glVertexAttrib4f(GLuint index,float x,float y,float z,float w){struct{uint32_t index;float v[4];}p={index,{x,y,z,w}};emit_gl_call(GLFN_VERTEX_ATTRIB4F,&p,sizeof(p));}
static void glEdgeFlag(GLboolean value){uint32_t v=value;emit_gl_call(GLFN_EDGE_FLAG,&v,4);}
static void glVertex4f(float x,float y,float z,float w){float p[4]={x,y,z,w};emit_gl_call(GLFN_VERTEX4F,p,16);}
${body(source,"glArrayElement","void APIENTRY ")}
static void pointer(uint16_t opcode,GLint size,GLenum type,uint32_t offset){uint32_t p[4]={size,type,0,offset};emit_gl_call(opcode,p,16);}
static void enable(GLenum array){emit_gl_call(GLFN_ENABLE_CLIENT_STATE,&array,4);}
int main(void){
 uint8_t storage[128]={0};float position[4]={1,1,0,2},fog=.375f,texcoord[3]={.25f,.5f,.75f};GLbyte normal[3]={-128,0,64},generic[4]={-128,-64,0,127};GLubyte secondary[3]={64,128,255};GLint index=7;
 memcpy(storage,position,16);memcpy(storage+16,normal,3);memcpy(storage+20,generic,4);memcpy(storage+24,secondary,3);memcpy(storage+28,&fog,4);memcpy(storage+32,&index,4);memcpy(storage+36,texcoord,12);
 uint32_t bind[2]={GL_ARRAY_BUFFER,1};emit_gl_call(GLFN_BIND_BUFFER,bind,8);uint8_t payload[144];uint32_t meta[4]={GL_ARRAY_BUFFER,128,GL_STATIC_DRAW,128};memcpy(payload,meta,16);memcpy(payload+16,storage,128);emit_gl_call(GLFN_BUFFER_DATA,payload,144);
 g_vertex_array=(ClientArrayState){TRUE,4,GL_FLOAT,0,0,1};g_normal_array=(ClientArrayState){TRUE,3,GL_BYTE,0,(void*)16,1};g_secondary_color_array=(ClientArrayState){TRUE,3,GL_UNSIGNED_BYTE,0,(void*)24,1};g_fog_coord_array=(ClientArrayState){TRUE,1,GL_FLOAT,0,(void*)28,1};g_index_array=(ClientArrayState){TRUE,1,GL_INT,0,(void*)32,1};g_texcoord_arrays[0]=(ClientArrayState){TRUE,3,GL_FLOAT,0,(void*)36,1};g_generic_attribs[7]=(GenericAttribState){TRUE,4,GL_BYTE,TRUE,0,(void*)20,1,{0}};
 pointer(GLFN_VERTEX_POINTER_VBO,4,GL_FLOAT,0);pointer(GLFN_NORMAL_POINTER_VBO,3,GL_BYTE,16);pointer(GLFN_SECONDARY_COLOR_POINTER_VBO,3,GL_UNSIGNED_BYTE,24);pointer(GLFN_FOG_COORD_POINTER_VBO,1,GL_FLOAT,28);pointer(GLFN_INDEX_POINTER_VBO,1,GL_INT,32);pointer(GLFN_TEX_COORD_POINTER_VBO,3,GL_FLOAT,36);uint32_t generic_pointer[6]={7,4,GL_BYTE,1,0,20};emit_gl_call(GLFN_VERTEX_ATTRIB_POINTER_VBO,generic_pointer,24);uint32_t seven=7;emit_gl_call(GLFN_ENABLE_VERTEX_ATTRIB_ARRAY,&seven,4);
 enable(GL_VERTEX_ARRAY);enable(GL_NORMAL_ARRAY);enable(GL_SECONDARY_COLOR_ARRAY_EXT);enable(GL_FOG_COORDINATE_ARRAY_EXT);enable(GL_INDEX_ARRAY);enable(GL_TEXTURE_COORD_ARRAY);
 glNormal3b(-128,-64,127);check_normal(${reference.normal3b.map(Math.fround).map(v=>"(GLfloat)("+v+")").join(",")});
 glNormal3s(-32768,-16384,32767);check_normal(${reference.normal3s.map(Math.fround).map(v=>"(GLfloat)("+v+")").join(",")});
 glNormal3i(INT32_MIN,-1073741824,INT32_MAX);check_normal(${reference.normal3i.map(Math.fround).map(v=>"(GLfloat)("+v+")").join(",")});
 glColor4f(.25,.5,.75,1);uint32_t mode=GL_POINTS;emit_gl_call(GLFN_BEGIN,&mode,4);glArrayElement(0);emit_gl_call(GLFN_END,NULL,0);assert(!error);
 bind[1]=0;emit_gl_call(GLFN_BIND_BUFFER,bind,8);float client_vertex[4]={.5f,-.5f,0,4};g_vertex_array.buffer=0;g_vertex_array.pointer=client_vertex;pointer(GLFN_VERTEX_POINTER_VBO,4,GL_FLOAT,(uint32_t)(uintptr_t)client_vertex);emit_gl_call(GLFN_BEGIN,&mode,4);glArrayElement(0);emit_gl_call(GLFN_END,NULL,0);assert(!error);
 bind[1]=1;emit_gl_call(GLFN_BIND_BUFFER,bind,8);V86GLRecordQueryBufferMap map={0};map.target=GL_ARRAY_BUFFER;map.access=GL_READ_ONLY_ARB;map.pointer=7;uint8_t* args;reserve_pci_record(GLFN_QUERY_BUFFER_MAP,NULL,sizeof(map),&args);memcpy(args,&map,sizeof(map));assert(emit_pci_batch(FALSE));
 emit_gl_call(GLFN_BEGIN,&mode,4);glArrayElement(0);emit_gl_call(GLFN_END,NULL,0);assert(error==GL_INVALID_OPERATION);release_encoding_buffer_reads();return 0;
}`;
run(code,(cpu,gpu)=>{
 assert.deepEqual(gpu.elementVertices,[[1,1,0,2],[.5,-.5,0,4]]);
 assert.deepEqual(cpu.stateValue(GL.CURRENT_NORMAL)[0],reference.normal.map(Math.fround));
 assert.deepEqual(cpu.current.state.arrays.generic[7].current,reference.generic.map(Math.fround));
 assert.deepEqual(cpu.stateValue(GL.CURRENT_TEXTURE_COORDS)[0],[.25,.5,.75,1]);assert.deepEqual(cpu.stateValue(GL.CURRENT_COLOR)[0],[.25,.5,.75,1]);
 assert.equal(cpu.stateValue(GL.CURRENT_INDEX)[0][0],7);assert.equal(cpu.stateValue(GL.CURRENT_FOG_COORDINATE_EXT)[0][0],.375);
},(cpu,gpu)=>{gpu.elementVertices=[];const original=gpu.immediateVertex;gpu.immediateVertex=function(...position){gpu.elementVertices.push(position);return original.apply(this,position);};})
 .then(()=>console.log("PASS C ArrayElement: VBO offset zero and client data, homogeneous w, normalized generic/normal values, secondary/fog/index/texcoords and mapped-buffer refusal in both transports"))
 .catch(error=>{console.error(error);process.exitCode=1;});
