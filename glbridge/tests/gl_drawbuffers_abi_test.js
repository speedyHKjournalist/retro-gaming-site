#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const start = source.indexOf("void APIENTRY glDrawBuffers("), end = source.indexOf("void APIENTRY glDrawBuffersARB(", start);
assert.ok(start >= 0 && end > start);
const capacity = source.match(/^#define V86GL_MAX_DRAW_BUFFERS \d+/m)[0];
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-drawbuffers-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
${capacity}
#define APIENTRY
#define GL_NONE 0
#define GL_INVALID_VALUE 0x501
#define CopyMemory memcpy
typedef int GLsizei;
typedef unsigned int GLenum;
static GLenum g_draw_buffers[V86GL_MAX_DRAW_BUFFERS], error;
static unsigned int emitted;
static void v86gl_set_error(GLenum value) { error = value; }
static uint8_t* alloc_payload(uint32_t size) { return malloc(size); }
static void* GetProcessHeap(void) { return NULL; }
static void HeapFree(void* heap, int flags, void* value) { (void)heap; (void)flags; free(value); }
static void emit_gl_call(uint16_t fn, const void* data, uint32_t size) {
    const uint32_t* words = data; assert(fn == GLFN_DRAW_BUFFERS && size == 4 + words[0] * 4);
    for (unsigned int i = 0; i < words[0]; ++i) assert(words[i+1] == 0x8CE0u + i);
    ++emitted;
}
${source.slice(start, end)}
int main(void) {
    const GLenum values[8] = {0x8CE0,0x8CE1,0x8CE2,0x8CE3,0x8CE4,0x8CE5,0x8CE6,0x8CE7};
    glDrawBuffers(8, values); assert(!error && emitted == 1);
    glDrawBuffers(9, values); assert(error == GL_INVALID_VALUE && emitted == 1);
    error = 0; glDrawBuffers(-1, values); assert(error == GL_INVALID_VALUE && emitted == 1);
    error = 0; glDrawBuffers(1, NULL); assert(error == GL_INVALID_VALUE && emitted == 1);
    glDrawBuffers(0, NULL); assert(emitted == 2);
    for (unsigned int i = 0; i < 8; ++i) assert(g_draw_buffers[i] == GL_NONE);
    return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_drawbuffers_abi_test: actual C encoder supports all eight advertised draw buffers and validates count/pointer bounds (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
