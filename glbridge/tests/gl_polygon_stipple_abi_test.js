"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const { testPath } = require("./host_paths.js");
const reference = require(testPath("gl_conformance/polygon_stipple_reference.json")).cases;
const expected = reference.find(c => c.name === "unpack_rows").bytes;
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum;typedef unsigned char GLubyte;typedef void GLvoid;typedef int BOOL;
typedef intptr_t GLintptrARB;typedef intptr_t GLsizeiptrARB;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static unsigned int maximum;
#define GL_CPU_ABI_BEFORE_SEND do {if(wire_size>maximum)maximum=wire_size;} while(0)
${transport}
static uint32_t g_dma_size,pack_buffer;
static uint8_t cache[128];
static BOOL wait_for_host_status(const uint8_t* p,const char* name) {(void)name;return read_u32le(p)==1;}
static GLvoid* pack_pixel_pointer(GLvoid* p,uint32_t size) {if(pack_buffer){assert(size==128);return cache;}return p;}
static void emit_buffer_sub_data(GLenum target,GLintptrARB offset,GLsizeiptrARB size,const GLvoid* data) {
    uint32_t words[4]={target,(uint32_t)offset,(uint32_t)size,(uint32_t)size};uint8_t payload[176];
    assert(size<=160);memcpy(payload,words,16);memcpy(payload+16,data,size);emit_gl_call(GLFN_BUFFER_SUB_DATA,payload,16+size);
}
${body(source, "query_polygon_stipple", "static BOOL ")}
${body(source, "glPolygonStipple", "void APIENTRY ")}
${body(source, "glGetPolygonStipple", "void APIENTRY ")}
static void store(uint32_t pname,uint32_t value){uint32_t words[]={pname,value};emit_gl_call(GLFN_PIXEL_STOREI,words,8);}
static void bind(uint32_t target,uint32_t name){uint32_t words[]={target,name};emit_gl_call(GLFN_BIND_BUFFER,words,8);}
static void buffer(uint32_t target,const uint8_t* bytes,uint32_t size) {
    uint32_t words[]={target,size,GL_STATIC_DRAW,size};uint8_t payload[144];memcpy(payload,words,16);memcpy(payload+16,bytes,size);emit_gl_call(GLFN_BUFFER_DATA,payload,16+size);
}
int main(void) {
    uint8_t source[512],out[320],expected[128]={${expected.join(",")}};
    for(unsigned int i=0;i<512;i++)source[i]=(i*73)^0xA6;
    store(GL_UNPACK_ALIGNMENT,8);store(GL_UNPACK_ROW_LENGTH,40);store(GL_UNPACK_SKIP_ROWS,1);store(GL_UNPACK_SKIP_PIXELS,5);store(GL_UNPACK_LSB_FIRST,1);
    glPolygonStipple(source);memset(out,0xA5,sizeof(out));glGetPolygonStipple(out);
    assert(!error&&!memcmp(out,expected,128));for(unsigned int i=128;i<320;i++)assert(out[i]==0xA5);
    store(GL_PACK_SKIP_PIXELS,3);store(GL_PACK_LSB_FIRST,1);memset(out,0xA5,sizeof(out));glGetPolygonStipple(out);
    assert((out[0]&7)==5 && (out[128]&0xF8)==0xA0 && out[129]==0xA5);
    store(GL_PACK_SKIP_PIXELS,0);store(GL_PACK_LSB_FIRST,0);
    uint8_t* padded=malloc(32*131072);memset(padded,0xA5,32*131072);store(GL_PACK_ROW_LENGTH,1048576);
    glGetPolygonStipple(padded);for(unsigned int y=0;y<32;y++){assert(!memcmp(padded+y*131072,expected+y*4,4));assert(padded[y*131072+4]==0xA5);}free(padded);store(GL_PACK_ROW_LENGTH,0);
    store(GL_UNPACK_ALIGNMENT,4);store(GL_UNPACK_ROW_LENGTH,0);store(GL_UNPACK_SKIP_ROWS,0);store(GL_UNPACK_SKIP_PIXELS,0);store(GL_UNPACK_LSB_FIRST,0);
    bind(GL_PIXEL_UNPACK_BUFFER,7);buffer(GL_PIXEL_UNPACK_BUFFER,expected,128);glPolygonStipple(NULL);bind(GL_PIXEL_UNPACK_BUFFER,0);
    bind(GL_PIXEL_PACK_BUFFER,8);memset(cache,0xA5,sizeof(cache));buffer(GL_PIXEL_PACK_BUFFER,cache,128);pack_buffer=8;glGetPolygonStipple(NULL);
    assert(!error&&!memcmp(cache,expected,128));pack_buffer=0;bind(GL_PIXEL_PACK_BUFFER,0);
    uint32_t primitive=GL_TRIANGLES;emit_gl_call(GLFN_BEGIN,&primitive,4);memset(out,0xA5,sizeof(out));glGetPolygonStipple(out);emit_gl_call(GLFN_END,NULL,0);
    assert(error==GL_INVALID_OPERATION);for(unsigned int i=0;i<320;i++)assert(out[i]==0xA5);
    assert(maximum<=372);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.deepEqual(Array.from(shadow.current.group.buffers.get(8).data), expected);
    assert.deepEqual(Array.from(gpu.current.polygonStipple), expected);
}).then(() => console.log("gl_polygon_stipple_abi_test: actual C row capture, masked scatter, sparse packing and PBO NULL offsets passed in both modes"))
    .catch(error => { console.error(error); process.exitCode = 1; });
