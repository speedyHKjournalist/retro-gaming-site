#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const start = source.indexOf("void APIENTRY glPrioritizeTextures("), end = source.indexOf("__declspec(dllexport)", start);
assert.ok(start >= 0 && end > start);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-texture-priority-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
#define APIENTRY
#define GL_INVALID_VALUE 0x501
#define GL_OUT_OF_MEMORY 0x505
#define V86GL_MAX_TEXTURE_STATES 3
#define CopyMemory memcpy
typedef int GLsizei;
typedef uint32_t GLuint;
typedef float GLfloat;
typedef float GLclampf;
static struct { int used; GLuint name; GLfloat priority; } g_texture_states[3] = {{1,7,1},{1,9,1},{1,77,1}};
static unsigned int error, emitted;
static uint8_t captured[128];
static void v86gl_set_error(unsigned int value) { error = value; }
static uint8_t* alloc_payload(uint32_t size) { return malloc(size); }
static void* GetProcessHeap(void) { return NULL; }
static void HeapFree(void* heap, int flags, void* value) { (void)heap; (void)flags; free(value); }
static void emit_gl_call(uint16_t opcode, const void* data, uint32_t size) {
    assert(opcode == GLFN_PRIORITIZE_TEXTURES && size <= sizeof(captured));
    assert(size == 4u + ((const uint32_t*)data)[0] * 8u); memcpy(captured, data, size); ++emitted;
}
${source.slice(start, end)}
int main(void) {
    const GLuint names[3] = {7,9,77}; const GLfloat values[3] = {-1,.25f,2};
    glPrioritizeTextures(3, names, values); assert(!error && emitted == 1);
    for (unsigned int i = 0; i < 3; ++i) {
        GLuint name; GLfloat priority;
        memcpy(&name, captured + 4 + i * 8, 4); memcpy(&priority, captured + 8 + i * 8, 4);
        assert(name == names[i] && priority == values[i]);
    }
    assert(g_texture_states[0].priority == 0 && g_texture_states[1].priority == .25f && g_texture_states[2].priority == 1);
    glPrioritizeTextures(0, NULL, NULL); assert(emitted == 2);
    glPrioritizeTextures(-1, names, values); assert(error == GL_INVALID_VALUE && emitted == 2);
    error = 0; glPrioritizeTextures(1, NULL, values); assert(error == GL_INVALID_VALUE && emitted == 2);
    error = 0; glPrioritizeTextures(INT32_MAX, names, values); assert(error == GL_OUT_OF_MEMORY && emitted == 2);
    return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_texture_priority_abi_test: actual C function forwards name/float pairs, clamps cache values and validates empty, invalid and oversized requests (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
