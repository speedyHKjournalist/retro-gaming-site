#!/usr/bin/env node
"use strict";
const fs = require("node:fs"), os = require("node:os"), path = require("node:path"), assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function extract(prefix) {
    const start = source.indexOf(prefix), brace = source.indexOf("{", start); assert.ok(start >= 0);
    let n = 1, end = brace + 1;
    while (n && end < source.length) { if (source[end] === "{") ++n; if (source[end++] === "}") --n; }
    return source.slice(start, end);
}
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gl_protocol.h"
#define APIENTRY
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_OPERATION 0x502
#define GL_PIXEL_MAP_I_TO_I 0xC70
#define GL_PIXEL_MAP_I_TO_R 0xC72
#define GL_PIXEL_MAP_A_TO_A 0xC79
#define GL_PIXEL_MAP_I_TO_I_SIZE 0xCB0
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned short GLushort;
typedef float GLfloat;
typedef double GLdouble;
typedef int GLint;
typedef int GLsizei;
typedef void GLvoid;
typedef intptr_t GLintptrARB;
typedef intptr_t GLsizeiptrARB;
typedef struct { int mapped; } BufferObjectState;
typedef struct { GLsizei size; GLdouble* values; } PixelMapState;
static PixelMapState g_pixel_maps[10];
static GLuint g_pixel_pack_buffer_binding, g_pixel_unpack_buffer_binding;
static unsigned int error, writes, last_bytes, last_offset;
static int supported = 1, fail;
static BufferObjectState buffer;
static union { GLdouble alignment; unsigned char bytes[4096]; } memory;
#define storage memory.bytes
static void v86gl_set_error(GLenum e) { error = e; }
static const void* buffer_data_at(GLuint name, const void* offset, uint32_t bytes) {
    uintptr_t at = (uintptr_t)offset;
    if (!name || at > sizeof(storage) || bytes > sizeof(storage)-at) { v86gl_set_error(GL_INVALID_OPERATION); return NULL; }
    return storage+at;
}
static BufferObjectState* bound_buffer_state(GLenum target) {
    assert(target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER); return &buffer;
}
${extract("static const GLvoid* unpack_pixel_pointer(")}
${extract("static GLvoid* pack_pixel_pointer(")}
static int emit_query_state(GLenum pname, unsigned int kind, void* out) {
    assert(pname >= 0xCB0 && pname <= 0xCB9 && kind == V86GL_STATE_INTEGER); *(GLint*)out = 4; return supported;
}
static int emit_query_parameter_count(unsigned int family, GLenum target, GLenum pname, unsigned int kind, void* out, unsigned int capacity) {
    assert(family == V86GL_PARAMETER_PIXEL_MAP && target == 0xC76 && pname == 0 && capacity == V86GL_MAX_TYPED_QUERY_VALUES);
    if (fail) { v86gl_set_error(GL_INVALID_OPERATION); return -1; }
    if (!supported) return 0;
    if (!out) return -1;
    for (unsigned int i=0; i<4; ++i) {
        const double v[] = {0,.5,1,1};
        if (kind == V86GL_STATE_FLOAT) ((GLfloat*)out)[i] = (GLfloat)v[i];
        if (kind == V86GL_STATE_UNSIGNED_INT) ((GLuint*)out)[i] = (GLuint)(v[i]*4294967295.0);
        if (kind == V86GL_STATE_UNSIGNED_SHORT) ((GLushort*)out)[i] = (GLushort)(v[i]*65535.0+.5);
    }
    return 1;
}
static void emit_buffer_sub_data(GLenum target, GLintptrARB offset, GLsizeiptrARB bytes, const GLvoid* data) {
    assert(target == GL_PIXEL_PACK_BUFFER && data == storage+offset); ++writes; last_bytes=(unsigned int)bytes; last_offset=(unsigned int)offset;
}
${extract("static int pixel_map_index(")}
${extract("static const void* pixel_map_source(")}
${extract("static int emit_query_pixel_map(")}
${extract("void APIENTRY glGetPixelMapfv(")}
${extract("void APIENTRY glGetPixelMapuiv(")}
${extract("void APIENTRY glGetPixelMapusv(")}
int main(void) {
    g_pixel_unpack_buffer_binding=1;
    assert(pixel_map_source(0xC76,4,NULL,4) == storage);
    assert(!pixel_map_source(0xC76,4,(void*)2,4) && error == GL_INVALID_OPERATION);
    buffer.mapped=1; assert(!pixel_map_source(0xC76,4,NULL,2)); buffer.mapped=0;
    assert(!pixel_map_source(0xC76,4,(void*)4094,2));
    assert(!pixel_map_source(0xC72,3,NULL,4) && error == GL_INVALID_VALUE);
    assert(!pixel_map_source(0xC76,0,NULL,4) && error == GL_INVALID_VALUE);
    assert(!pixel_map_source(0xDEAD,1,NULL,4) && error == GL_INVALID_ENUM);
    g_pixel_pack_buffer_binding=1; error=0; memset(storage,0xA5,sizeof(storage));
    glGetPixelMapfv(0xC76,NULL); assert(!error && writes==1 && last_bytes==16 && last_offset==0);
    assert(((GLfloat*)storage)[1] == .5f && storage[16] == 0xA5);
    memset(storage,0xA5,sizeof(storage)); glGetPixelMapusv(0xC76,(GLushort*)4);
    assert(writes==2 && last_bytes==8 && last_offset==4); assert(((GLushort*)(storage+4))[1] == 32768 && storage[3] == 0xA5 && storage[12] == 0xA5);
    memset(storage,0xA5,sizeof(storage)); glGetPixelMapuiv(0xC76,(GLuint*)8);
    assert(writes==3 && last_bytes==16 && last_offset==8); assert(((GLuint*)(storage+8))[2] == 0xFFFFFFFFu && storage[7] == 0xA5 && storage[24] == 0xA5);
    glGetPixelMapusv(0xC76,(GLushort*)1); assert(error == GL_INVALID_OPERATION && writes==3);
    buffer.mapped=1; glGetPixelMapfv(0xC76,NULL); assert(writes==3); buffer.mapped=0;
    g_pixel_pack_buffer_binding=0; fail=1; GLfloat out[4]; memset(out,0xA5,sizeof(out)); glGetPixelMapfv(0xC76,out);
    for(unsigned int i=0; i<sizeof(out); ++i) assert(((unsigned char*)out)[i] == 0xA5);
    fail=0; supported=0; GLdouble saved[]={0,.5,1,1}; g_pixel_maps[6].size=4; g_pixel_maps[6].values=saved;
    GLuint u[4]; GLushort us[4]; glGetPixelMapuiv(0xC76,u); glGetPixelMapusv(0xC76,us);
    assert(u[1] == 2147483647u && u[2] == 0xFFFFFFFFu && us[1] == 32768 && us[2] == 65535);
    return 0;
}
`;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-pixelmap-abi-"));
try {
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"),
        path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_pixelmap_abi_test: actual C source/getters honor PBO offset zero, bounds, mapping/alignment, output sizes and failed-query guards (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
