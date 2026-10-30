#!/usr/bin/env node
// Run the real guest encoders against the real host decoder. In particular,
// signed fields and each multi-draw entry must survive the C/JS boundary.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLWebGPUExecutor } = require(hostPath("gl-webgpu/gl_executor.js"));
const { createFakeHost } = require(testPath("gl_fake_gpu.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function body(name) {
    const match = new RegExp("static (?:void|BOOL|uint32_t) " + name + "\\(").exec(source); assert.ok(match, name);
    let end = source.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(match.index, end);
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-draw-wire-"));
(async () => {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei, BOOL;
typedef void GLvoid;
typedef unsigned char GLboolean;
#define TRUE 1
#define FALSE 0
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_OUT_OF_MEMORY 0x505
#define CopyMemory memcpy
static GLuint g_array_buffer_binding = 7;
static void v86gl_set_error(GLenum error) { (void)error; abort(); }
static uint8_t* alloc_payload(uint32_t size) { return malloc(size); }
static void* GetProcessHeap(void) { return NULL; }
static void HeapFree(void* heap, int flags, void* p) { (void)heap; (void)flags; free(p); }
static void emit_gl_call(uint16_t opcode, const void* payload, uint32_t size) {
    const uint16_t length = (uint16_t)size;
    assert(fwrite(&opcode,2,1,stdout)==1 && fwrite(&length,2,1,stdout)==1);
    assert(fwrite(payload,1,size,stdout)==size);
}
typedef V86GLRecordVertexPointerVbo VBOPointerPayload;
typedef V86GLRecordVertexAttribPointerVbo VBOAttribPointerPayload;
${["pointer_offset_u32", "emit_vbo_pointer", "emit_vbo_attrib_pointer", "emit_draw_arrays_direct",
    "emit_draw_elements_direct", "emit_multi_draw_arrays_direct", "emit_multi_draw_elements_direct"].map(body).join("\n")}
int main(void) {
    for (unsigned int i=GLFN_VERTEX_POINTER_VBO; i<=GLFN_FOG_COORD_POINTER_VBO; ++i)
        emit_vbo_pointer((uint16_t)i, 3, 0x1406, 32, (const GLvoid*)(uintptr_t)12);
    emit_vbo_attrib_pointer(9, 4, 0x1401, GL_TRUE, 16, (const GLvoid*)(uintptr_t)24);
    emit_draw_arrays_direct(4, 2, 3);
    emit_draw_elements_direct(GLFN_DRAW_ELEMENTS_DIRECT, 4, 4, 8, 3, 0x1403, (const GLvoid*)(uintptr_t)6);
    emit_draw_elements_direct(GLFN_DRAW_RANGE_ELEMENTS_DIRECT, 4, 4, 8, 3, 0x1403, (const GLvoid*)(uintptr_t)6);
    const GLint first[]={2,5}; const GLsizei count[]={3,6};
    const GLvoid* indices[]={(const GLvoid*)(uintptr_t)6,(const GLvoid*)(uintptr_t)8};
    assert(emit_multi_draw_arrays_direct(4,first,count,2));
    assert(emit_multi_draw_elements_direct(4,count,0x1403,indices,2));
    return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    const binary = path.join(dir, "test");
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", binary]);
    const wire = execFileSync(binary), { host } = createFakeHost();
    const executor = new GLWebGPUExecutor(null, { host, strict: true }); await executor.initialize();
    executor.submit(new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1).call("BIND_BUFFER", GL.ARRAY_BUFFER, 7).bytes(), {});
    const calls = []; executor.drawFromBuffers = (...args) => calls.push(args);
    executor.submit(wire, {});
    for (const name of ["vertex", "color", "texCoord0", "normal", "secondaryColor", "fogCoord"]) {
        const array = executor.current.arrays[name];
        assert.equal(array.buffer, 7); assert.equal(array.size, 3); assert.equal(array.type, GL.FLOAT);
        assert.equal(array.stride, 32); assert.equal(array.offset, 12);
    }
    const generic = executor.current.arrays.generic9;
    assert.equal(generic.buffer, 7); assert.equal(generic.size, 4); assert.equal(generic.type, GL.UNSIGNED_BYTE);
    assert.equal(generic.normalized, true); assert.equal(generic.stride, 16); assert.equal(generic.offset, 24);
    const indexed = count => [GL.TRIANGLES, 0, count, { type: GL.UNSIGNED_SHORT, bufferOffset: 6 }];
    assert.deepEqual(calls, [[GL.TRIANGLES, 2, 3, null], indexed(3), indexed(3),
        [GL.TRIANGLES, 2, 3, null], [GL.TRIANGLES, 5, 6, null], indexed(3),
        [GL.TRIANGLES, 0, 6, { type: GL.UNSIGNED_SHORT, bufferOffset: 8 }]]);
    assert.equal(executor.stats.refusals, 0); assert.equal(executor.hostErrors.length, 0);
    console.log("gl_draw_wire_abi_test: actual C VBO, generic pointer, direct and multi-draw encoders reach the real host decoder (passed)");
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => fs.rmSync(dir, { recursive: true, force: true }));
