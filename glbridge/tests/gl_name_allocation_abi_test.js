"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const names = ["glGenTextures", "glGenBuffersARB", "glGenRenderbuffersEXT", "glGenFramebuffersEXT"];
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef int32_t GLint; typedef uint32_t GLuint; typedef int32_t GLsizei; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define GetProcessHeap() 0
#define HeapFree(h,f,p) free(p)
${Object.entries(GL).filter(([, value]) => Number.isInteger(value) && value >= 0).map(([name, value]) => "#define GL_" + name + " 0x" + value.toString(16) + "u").join("\n")}
${transport}
static uint32_t generated_records,largest_record;
static uint8_t* alloc_payload(uint32_t size) { if(size>largest_record)largest_record=size; return malloc(size); }
static void emit_name_array_call(uint16_t fn,GLsizei count,const GLuint* names);
${body(source, "emit_name_array_call", "static void ")}
${body(source, "emit_query_names", "static int ")}
static GLuint g_next_texture_id=1,g_next_buffer_id=1,g_next_renderbuffer_id=1,g_next_framebuffer_id=1;
static void* find_texture_state(GLuint name,GLenum target,BOOL create) { (void)name;(void)target;(void)create;abort(); }
static void* find_renderbuffer_state(GLuint name,BOOL create) { (void)name;(void)create;abort(); }
static void* find_framebuffer_state(GLuint name,BOOL create) { (void)name;(void)create;abort(); }
${names.map(name => body(source, name, "void APIENTRY ")).join("\n")}
int main(void) {
    void (*generate[])(GLsizei,GLuint*)={glGenTextures,glGenBuffersARB,glGenRenderbuffersEXT,glGenFramebuffersEXT};
    uint16_t binds[]={GLFN_BIND_TEXTURE,GLFN_BIND_BUFFER,GLFN_BIND_RENDERBUFFER,GLFN_BIND_FRAMEBUFFER};
    GLenum targets[]={GL_TEXTURE_2D,GL_ARRAY_BUFFER,GL_RENDERBUFFER_EXT,GL_FRAMEBUFFER_EXT};
    for(unsigned int i=0;i<4;i++) {
        uint32_t args[]={targets[i],1}; emit_gl_call(binds[i],args,8); args[1]=3; emit_gl_call(binds[i],args,8);
        GLuint out[3]={0}; generate[i](3,out); assert(!error && out[0]==2 && out[1]==4 && out[2]==5);
        generate[i](0,NULL); assert(!error); out[0]=0xA5A5A5A5;
        generate[i](-1,out); assert(error==GL_INVALID_VALUE && out[0]==0xA5A5A5A5); error=0;
        ++generated_records;
    }
    GLuint* many=malloc(70000*sizeof(GLuint)); assert(many); glGenBuffersARB(70000,many); assert(!error);
    for(unsigned int i=0;i<70000;i++)assert(many[i]==i+6); free(many);
    assert(largest_record<=4+65536*4 && generated_records==4);
    assert(g_next_texture_id==1 && g_next_buffer_id==1 && g_next_renderbuffer_id==1 && g_next_framebuffer_id==1);
    uint32_t mode=GL_POINTS; emit_gl_call(GLFN_BEGIN,&mode,4); glGenTextures(0,NULL);
    assert(error==GL_INVALID_OPERATION); error=0; emit_gl_call(GLFN_END,NULL,0); return 0;
}
`;
run(code, shadow => {
    const group = shadow.current.group;
    assert.equal(group.buffers.size, 70005); assert.equal(group.textures.size, 5); assert.equal(group.renderbuffers.size, 5);
    assert.equal(group.buffers.get(70005).created, false); assert.equal(group.textures.get(2).target, 0);
}).then(() => console.log("gl_name_allocation_abi_test: four actual C generators use CPU namespaces, skip bound names and chunk 70000 names in both stream modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
