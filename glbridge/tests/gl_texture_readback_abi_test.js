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
typedef unsigned int GLenum; typedef int GLint; typedef unsigned int GLuint; typedef int BOOL;
typedef void GLvoid; typedef intptr_t GLintptrARB; typedef intptr_t GLsizeiptrARB;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static unsigned int chunks,metadata,maximum;
#define GL_CPU_ABI_BEFORE_SEND do { if(*(uint16_t*)wire==GLFN_QUERY_TEXTURE_IMAGE){uint32_t head=*(uint16_t*)(wire+2)==0xFFFF?8:4;uint32_t mode=*(uint32_t*)(wire+head+20),size=*(uint32_t*)(wire+head+32);if(mode==0)metadata++;else if(mode==1||mode==2){chunks++;if(size>maximum)maximum=size;}} } while(0)
${transport}
static uint32_t g_dma_size, g_pixel_pack_buffer_binding;
static BOOL wait_for_host_status(const uint8_t* p,const char* name) { (void)name;return read_u32le(p)==1; }
static GLvoid* pack_pixel_pointer(GLvoid* pixels,uint32_t span) { assert(span);return pixels; }
static void emit_buffer_sub_data(GLenum target,GLintptrARB offset,GLsizeiptrARB span,const void* pixels) { (void)target;(void)offset;(void)span;(void)pixels;assert(0); }
${body(source, "emit_texture_image_query", "static void ")}
${body(source, "glGetTexImage", "void APIENTRY ")}
${body(source, "glGetCompressedTexImageARB", "void APIENTRY ")}
int main(void) {
    uint32_t bind[]={GL_TEXTURE_2D,1}; emit_gl_call(GLFN_BIND_TEXTURE,bind,sizeof(bind));
    uint32_t image[]={GL_TEXTURE_2D,0,GL_RGBA8,512,256,0,GL_RGBA,GL_UNSIGNED_BYTE,0}; emit_gl_call(GLFN_TEX_IMAGE_2D,image,sizeof(image));
    uint8_t* bytes=malloc(512*256*4+16);memset(bytes,0xA5,512*256*4+16);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes);
    assert(!error && chunks==2 && maximum==262144 && metadata==1);
    for(unsigned int i=0;i<512*256*4;i++)assert(bytes[i]==((i%4)==3?255:64));
    for(unsigned int i=512*256*4;i<512*256*4+16;i++)assert(bytes[i]==0xA5);
    glGetTexImage(GL_TEXTURE_2D,99,GL_RGBA,GL_UNSIGNED_BYTE,bytes);assert(error==GL_INVALID_VALUE);error=0;
    glGetCompressedTexImageARB(GL_TEXTURE_2D,0,bytes);assert(error==GL_INVALID_OPERATION);error=0;
    glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,bytes);assert(!error && chunks==2 && metadata==4);
    free(bytes);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.textureRead, null); assert.equal(gpu.current.textureRead, null);
}, (shadow, gpu) => {
    gpu.readRectangle = (source, kind, rect) => () => Promise.resolve(Float32Array.from({ length: rect.width * rect.height * 4 },
        (_, i) => i % 4 === 3 ? 1 : 64 / 255));
}).then(() => console.log("gl_texture_readback_abi_test: actual C texture readers validate metadata and split a large image into bounded chunks in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
