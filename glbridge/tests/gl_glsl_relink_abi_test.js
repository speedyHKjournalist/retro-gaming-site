"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const {body,transport,run,GL}=require("./gl_cpu_abi_harness.js");
const {hostPath}=require("./host_paths.js"),{Glslang}=require(hostPath("gl-webgpu/gl_glslang.js"));
const source=fs.readFileSync(path.join(__dirname,"../openglproxy/opengl32_proxy.c"),"utf8");
const code=`
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef int GLint,GLsizei,BOOL;typedef double GLdouble;typedef float GLfloat;typedef unsigned char GLboolean;typedef unsigned short GLushort;typedef char GLchar;typedef size_t SIZE_T;
#define APIENTRY
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([,v])=>Number.isInteger(v)&&v>=0).map(([n,v])=>"#define GL_"+n+" 0x"+v.toString(16)+"u").join("\n")}
${transport}
static void* GetProcessHeap(void){return NULL;}
static void* HeapAlloc(void* heap,int flags,size_t size){(void)heap;(void)flags;return malloc(size);}
static void HeapFree(void* heap,int flags,void* pointer){(void)heap;(void)flags;free(pointer);}
static uint8_t* alloc_payload(uint32_t size){return malloc(size);}
static uint32_t gl_string_length(const GLchar* text){return (uint32_t)strlen(text);}
static uint32_t g_dma_size;
static BOOL wait_for_host_status(const uint8_t* status,const char* what){(void)what;return read_u32le(status)==1;}
${body(source,"emit_query_object_iv","static int ")}
${body(source,"emit_query_object_log","static int ")}
${["emit_query_typed","emit_query_parameter_index_count"].map(n=>body(source,n,"static int ")).join("\n")}
${["glShaderSource","glCompileShader","glLinkProgram","glUseProgram","glValidateProgram","glGetProgramiv","glGetShaderiv","glGetShaderSource","glGetAttachedShaders"].map(n=>body(source,n,"void APIENTRY ")).join("\n")}
int main(void){
 const char* vertex="void main(){gl_Position=gl_Vertex;}";const char* fragment="uniform float value;void main(){gl_FragColor=vec4(value);}";GLint status=-1;
 uint32_t create[2]={2,GL_VERTEX_SHADER};emit_gl_call(GLFN_CREATE_SHADER,create,8);create[0]=3;create[1]=GL_FRAGMENT_SHADER;emit_gl_call(GLFN_CREATE_SHADER,create,8);
 uint32_t program=1;emit_gl_call(GLFN_CREATE_PROGRAM,&program,4);uint32_t pair[2]={1,2};emit_gl_call(GLFN_ATTACH_SHADER,pair,8);pair[1]=3;emit_gl_call(GLFN_ATTACH_SHADER,pair,8);
 glShaderSource(2,1,&vertex,NULL);glShaderSource(3,1,&fragment,NULL);glCompileShader(2);glCompileShader(3);glLinkProgram(1);
 glGetProgramiv(1,GL_LINK_STATUS,&status);assert(status==1);glUseProgram(1);
 const char* invalid="void main(){unknown;}";glShaderSource(3,1,&invalid,NULL);glCompileShader(3);glLinkProgram(1);
 glGetProgramiv(1,GL_LINK_STATUS,&status);assert(status==0);glGetShaderiv(3,GL_COMPILE_STATUS,&status);assert(status==0);
 glShaderSource(3,1,&fragment,NULL);glCompileShader(3);glLinkProgram(1);glGetProgramiv(1,GL_LINK_STATUS,&status);assert(status==1);
 char text[256];GLsizei length=-1;glGetShaderSource(3,sizeof(text),&length,text);assert(length==(GLsizei)strlen(fragment)&&!strcmp(text,fragment));
 for(uint32_t i=4;i<304;i++){create[0]=i;create[1]=GL_VERTEX_SHADER;emit_gl_call(GLFN_CREATE_SHADER,create,8);pair[0]=1;pair[1]=i;emit_gl_call(GLFN_ATTACH_SHADER,pair,8);}
 GLuint attached[305];memset(attached,0xA5,sizeof(attached));GLsizei count=-1;glGetAttachedShaders(1,305,&count,attached);assert(count==302&&attached[0]==2&&attached[301]==303&&attached[302]==0xA5A5A5A5u);
 const char nul_source[]={'a',0,'b'};const char* nul=nul_source;GLint size=3;glShaderSource(2,1,&nul,&size);glGetShaderSource(2,sizeof(text),&length,text);assert(length==3&&!memcmp(text,nul_source,3)&&!text[3]);
 return 0;
}`;
run(code,(cpu,gpu)=>{assert.equal(cpu.current.group.programs.get(1).linked.ok,true);assert.equal(gpu.current.shareGroup.programs.get(1).linked,true);},(cpu,gpu)=>{cpu.setCapabilities(gpu.capabilities());cpu.setGlslang(new Glslang(fs.readFileSync(path.join(__dirname,"../../../v86/bios/glslang.wasm"))));})
 .then(()=>console.log("PASS actual C GLSL compiler/link commands and CPU status across failed relink and recovery in both transports"))
 .catch(error=>{console.error(error);process.exitCode=1;});
