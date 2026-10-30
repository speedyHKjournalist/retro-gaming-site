"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef int GLint,GLsizei,BOOL;typedef void GLvoid;
typedef unsigned char GLubyte,GLboolean;typedef signed char GLbyte;typedef short GLshort;typedef unsigned short GLushort;typedef float GLfloat;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define CopyMemory memcpy
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
#define GetProcessHeap() NULL
#define v86gl_trace(...) ((void)0)
static int v86gl_trace_call_allowed(void){return 0;}
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
static uint32_t g_dma_size,g_list_base;static GLfloat g_line_width=1;
static BOOL wait_for_host_status(const uint8_t* p,const char* name){(void)name;return read_u32le(p)==1;}
${body(source, "query_list", "static GLuint ")}
${body(source, "glGenLists", "GLuint APIENTRY ")}
${body(source, "glDeleteLists", "void APIENTRY ")}
${body(source, "glIsList", "GLboolean APIENTRY ")}
${body(source, "glNewList", "void APIENTRY ")}
${body(source, "glEndList", "void APIENTRY ")}
${body(source, "glCallList", "void APIENTRY ")}
${body(source, "glListBase", "void APIENTRY ")}
${body(source, "display_list_name_from_array", "static int ")}
${body(source, "glCallLists", "void APIENTRY ")}
${body(source, "glLineWidth", "void APIENTRY ")}
static void color(float red,float green){float values[]={red,green,0,1};emit_gl_call(GLFN_COLOR4F,values,16);}
static float red(void){uint32_t request[]={GL_CURRENT_COLOR,V86GL_STATE_DOUBLE,0,0,32,0};uint8_t* args;reserve_pci_record(GLFN_QUERY_STATE,NULL,sizeof(request)+32,&args);memcpy(args,request,sizeof(request));emit_pci_batch(0);assert(read_u32le(args+8)==1);double value;memcpy(&value,args+24,8);return (float)value;}
static uint32_t host_error(void){uint32_t request[4]={0};uint8_t* args;reserve_pci_record(GLFN_QUERY_ERROR,NULL,16,&args);memcpy(args,request,16);emit_pci_batch(0);assert(read_u32le(args)==1);return read_u32le(args+4);}
int main(void){
    GLuint first=glGenLists(2);assert(first&&glIsList(first)&&glIsList(first+1));glDeleteLists(first,2);assert(!glIsList(first));
    glNewList(10,GL_COMPILE);color(0.25f,0);assert(!glIsList(10)&&red()==1);glEndList();assert(glIsList(10));glCallList(10);assert(red()==0.25f);
    glNewList(11,GL_COMPILE);glCallList(10);glEndList();glNewList(10,GL_COMPILE);color(0.5f,0);glEndList();glCallList(11);assert(red()==0.5f);
    glListBase(11);signed char offset=-1;glCallLists(1,GL_BYTE,&offset);assert(red()==0.5f);
    glNewList(12,GL_COMPILE_AND_EXECUTE);color(0.75f,0);assert(red()==0.75f);glEndList();color(1,0);glCallList(12);assert(red()==0.75f);
    glNewList(13,GL_COMPILE);glLineWidth(-1);glEndList();assert(!error&&!host_error());glCallList(13);assert(host_error()==GL_INVALID_VALUE);
    glNewList(14,0);assert(host_error()==GL_INVALID_ENUM);glEndList();assert(host_error()==GL_INVALID_OPERATION);
    glDeleteLists(10,4);assert(!glIsList(10)&&!glIsList(11)&&!glIsList(12));assert(!error);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.listCompile, null); assert.equal(shadow.current.group.displayLists.size, 0);
    assert.equal(gpu.current.current.color[0], 0.75);
}).then(() => console.log("gl_display_lists_abi_test: actual C list allocation, compile, nested redefinition, signed offsets and deferred errors passed in both modes"))
    .catch(error => { console.error(error); process.exitCode = 1; });
