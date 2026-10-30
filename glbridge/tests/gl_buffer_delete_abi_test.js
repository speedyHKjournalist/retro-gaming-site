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
typedef unsigned int GLenum;typedef unsigned int GLuint;typedef int GLsizei;typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define HeapFree(h,f,p) free(p)
#define GetProcessHeap() NULL
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
typedef struct { GLuint buffer;const void* pointer; } ClientArrayState;
typedef struct { GLuint name;int used;void* data; } BufferObjectState;
#define V86GL_MAX_TEXTURE_UNITS 8
#define V86GL_MAX_VERTEX_ATTRIBS 16
static ClientArrayState g_vertex_array,g_color_array,g_index_array,g_normal_array,g_edge_flag_array,g_secondary_color_array,g_fog_coord_array;
static ClientArrayState g_texcoord_arrays[8],g_generic_attribs[16];
static GLuint g_array_buffer_binding=7,g_element_array_buffer_binding=7,g_pixel_pack_buffer_binding=7,g_pixel_unpack_buffer_binding=7;
static BufferObjectState cache;
static BufferObjectState* find_buffer_state(GLuint name,BOOL create) { (void)create;return cache.used&&cache.name==name?&cache:NULL; }
static void emit_name_array_call(uint16_t opcode,GLsizei count,const GLuint* names) { uint32_t words[3]={count,0,0};assert(count==1);words[1]=names[0];emit_gl_call(opcode,words,8); }
${body(source, "delete_buffer_state", "static void ")}
${body(source, "glDeleteBuffersARB", "void APIENTRY ")}
int main(void) {
    ClientArrayState* arrays[]={&g_vertex_array,&g_color_array,&g_index_array,&g_normal_array,&g_edge_flag_array,&g_secondary_color_array,&g_fog_coord_array};
    for(unsigned int i=0;i<7;i++){arrays[i]->buffer=7;arrays[i]->pointer=(void*)4;}
    for(unsigned int i=0;i<8;i++)g_texcoord_arrays[i].buffer=i==2?8:7;
    for(unsigned int i=0;i<16;i++)g_generic_attribs[i].buffer=i==3?8:7;
    cache.name=7;cache.used=1;cache.data=malloc(16);
    uint32_t targets[]={GL_ARRAY_BUFFER,GL_ELEMENT_ARRAY_BUFFER,GL_PIXEL_PACK_BUFFER,GL_PIXEL_UNPACK_BUFFER};
    for(unsigned int i=0;i<4;i++){uint32_t bind[]={targets[i],7};emit_gl_call(GLFN_BIND_BUFFER,bind,8);}
    uint32_t pointer[]={3,GL_FLOAT,0,4};emit_gl_call(GLFN_VERTEX_POINTER_VBO,pointer,16);
    GLuint name=7;glDeleteBuffersARB(1,&name);
    for(unsigned int i=0;i<7;i++)assert(!arrays[i]->buffer&&arrays[i]->pointer==(void*)4);
    for(unsigned int i=0;i<8;i++)assert(g_texcoord_arrays[i].buffer==(i==2?8u:0u));
    for(unsigned int i=0;i<16;i++)assert(g_generic_attribs[i].buffer==(i==3?8u:0u));
    assert(!g_array_buffer_binding&&!g_element_array_buffer_binding&&!g_pixel_pack_buffer_binding&&!g_pixel_unpack_buffer_binding&&!cache.used&&!error);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.bufferBindings.size, 0); assert.equal(shadow.current.state.arrays.vertex.bufferId, 0);
    assert.equal(shadow.current.state.arrays.vertex.pointer, 4); assert.equal(gpu.current.arrayBuffer, 0);
    assert.equal(gpu.current.arrays.vertex.bufferId, 0); assert.equal(gpu.current.arrays.vertex.offset, 4);
}).then(() => console.log("gl_buffer_delete_abi_test: actual C buffer deletion detaches current arrays/bindings and preserves pointers in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
