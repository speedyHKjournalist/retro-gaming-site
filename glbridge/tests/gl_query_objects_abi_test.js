"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const typed = source.slice(source.indexOf("static int emit_query_typed("), source.indexOf("static int emit_query_error"));
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef int32_t GLint; typedef uint32_t GLuint; typedef uint16_t GLushort; typedef int32_t GLsizei;
typedef float GLfloat; typedef double GLdouble; typedef unsigned char GLboolean; typedef int BOOL;
typedef uint32_t DWORD;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define V86GL_MAX_QUERY_OBJECTS 64
#define V86GL_DIAG_ENABLED 0
#define V86GL_QUERY_RESULT_WAIT_MS 1000u
#define GL_CPU_ABI_BEFORE_SEND do { if((read_u32le(wire)&0xFFFFu)==GLFN_QUERY_OBJECT_BATCH)assert(read_u32le(wire+4)==1); } while(0)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define GetProcessHeap() 0
#define HeapFree(h,f,p) free(p)
${Object.entries(GL).filter(([, value]) => Number.isInteger(value) && value >= 0).map(([name, value]) => "#define GL_" + name + " 0x" + value.toString(16) + "u").join("\n")}
${transport}
${typed}
static uint32_t g_dma_size;
static uint8_t* alloc_payload(uint32_t size){return malloc(size);}
${body(source, "emit_name_array_call", "static void ")}
${body(source, "emit_query_names", "static int ")}
static BOOL wait_for_host_status(const uint8_t* status,const char* label) { (void)label; return read_u32le(status)==V86GL_SYNC_QUERY_STATUS_OK; }
${body(source, "emit_query_object_iv", "static int ")}
${["glGenQueries", "glDeleteQueries", "glBeginQuery", "glEndQuery", "glGetQueryiv"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
${body(source, "glIsQuery", "GLboolean APIENTRY ")}
${body(source, "glGetQueryObjectiv", "void APIENTRY ")}
${body(source, "glGetQueryObjectuiv", "void APIENTRY ")}
static void context(GLuint id,GLuint active) {
    uint32_t args[]={id,0,0,64,64,id,1,0}; emit_gl_call(V86GL_CTRL_MAKE_CURRENT,args,sizeof(args));
    (void)active;
}
int main(void) {
    GLuint ids[2], extra, other; GLint current;
    glGenQueries(2,ids); assert(ids[0]==1 && ids[1]==2 && !error);
    assert(!glIsQuery(ids[0])); glBeginQuery(GL_SAMPLES_PASSED,7); assert(!error && glIsQuery(7));
    glGetQueryiv(GL_SAMPLES_PASSED,GL_CURRENT_QUERY,&current); assert(current==7);
    glGenQueries(0,NULL);assert(error==GL_INVALID_OPERATION);error=0;
    glDeleteQueries(1,ids);
    context(2,0); assert(!glIsQuery(7)); glBeginQuery(GL_SAMPLES_PASSED,7); assert(!error);
    glEndQuery(GL_SAMPLES_PASSED); assert(!error);
    glGenQueries(1,&other); assert(other==1 && !glIsQuery(other));
    context(1,7); glGetQueryiv(GL_SAMPLES_PASSED,GL_CURRENT_QUERY,&current); assert(current==7);
    glEndQuery(GL_SAMPLES_PASSED); assert(!error);
    GLint result=-1; GLuint available=0; glGetQueryObjectiv(7,GL_QUERY_RESULT,&result); assert(result==0 && !error);
    glGetQueryObjectuiv(7,GL_QUERY_RESULT_AVAILABLE,&available); assert(available==1);
    glGenQueries(1,&extra); assert(extra==3); extra=7; glDeleteQueries(1,&extra); assert(!glIsQuery(7));
    context(2,0); assert(glIsQuery(7));
    result=-1; glGetQueryObjectiv(7,GL_QUERY_RESULT,&result); assert(result==0 && !error);
    glGetQueryiv(GL_ANY_SAMPLES_PASSED,GL_QUERY_COUNTER_BITS,&current); assert(current==1);
    glGetQueryiv(GL_SAMPLES_PASSED,GL_QUERY_COUNTER_BITS,&current); assert(current==31);
    assert(!error);
    uint32_t destroyed=2; emit_gl_call(V86GL_CTRL_DESTROY_CONTEXT,&destroyed,4); return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.contexts.get("context:1").queryObjects.has(7), false);
    assert.equal(shadow.contexts.has("context:2"), false);
    assert.equal(gpu.contexts.get("context:1").queryObjects.has(7), false);
    assert.equal(gpu.contexts.has("context:2"), false);
}).then(() => console.log("gl_query_objects_abi_test: actual C query generation, implicit creation and metadata stay private across shared contexts in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
