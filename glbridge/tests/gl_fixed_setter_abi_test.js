#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function body(name) {
    const start = source.indexOf("void APIENTRY " + name + "("), brace = source.indexOf("{", start); assert.ok(start >= 0);
    let n = 1, end = brace + 1;
    while (n && end < source.length) { if (source[end] === "{") ++n; if (source[end++] === "}") --n; }
    return source.slice(start, end);
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-fixed-setter-"));
try {
    fs.writeFileSync(path.join(dir, "test.c"), `
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "gl_protocol.h"
#define APIENTRY
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_ENUM 0x500
#define GL_FOG_COLOR 0xB66
#define GL_POINT_DISTANCE_ATTENUATION_ARB 0x8129
#define ZeroMemory(p,n) memset(p,0,n)
typedef uint32_t GLenum;
typedef float GLfloat;
typedef double GLdouble;
typedef int32_t GLint;
static uint8_t payload[32];
static unsigned int error, opcode;
static void v86gl_set_error(GLenum e) { error = e; }
static void emit_gl_call(uint16_t op, const void* data, uint32_t size) { assert(size == 16); opcode = op; memcpy(payload,data,size); }
static int fog_value_count(GLenum p) { return p == GL_FOG_COLOR ? 4 : p == 0xB62 ? 1 : 0; }
static GLfloat fog[4];
static void glFogfv(GLenum p, const GLfloat* values) { memcpy(fog,values,fog_value_count(p)*sizeof(GLfloat)); }
${body("glPointParameterfv")}
${body("glPointParameteriv")}
${body("glFogiv")}
int main(void) {
    long page = sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(memory != MAP_FAILED && mprotect(memory+page,(size_t)page,PROT_NONE) == 0);
    GLfloat* f = (GLfloat*)(memory+page-sizeof(GLfloat)); *f = 2.75f;
    glPointParameterfv(0x8126,f); assert(!error && opcode == GLFN_POINT_PARAMETERFV);
    GLfloat values[3]; memcpy(values,payload+4,sizeof(values)); assert(values[0] == 2.75f && values[1] == 0 && values[2] == 0);
    GLint* i = (GLint*)f; *i = 3; glPointParameteriv(0x8126,i);
    GLint ints[3]; memcpy(ints,payload+4,sizeof(ints)); assert(ints[0] == 3 && ints[1] == 0 && ints[2] == 0);
    GLfloat three[] = {1,2,3}; glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION_ARB,three);
    memcpy(values,payload+4,sizeof(values)); assert(values[0] == 1 && values[1] == 2 && values[2] == 3);
    GLint color[] = {2147483647,1073741824,0,(-2147483647-1)}; glFogiv(GL_FOG_COLOR,color);
    assert(fog[0] == 1 && fog[1] == .5f && fog[2] == 0 && fog[3] == -1);
    GLint density = 37; glFogiv(0xB62,&density); assert(fog[0] == 37);
    munmap(memory,(size_t)page*2); return 0;
}
`);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"),
        path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_fixed_setter_abi_test: actual C point vector calls respect a guarded one-value array; integer fog colors normalize (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
