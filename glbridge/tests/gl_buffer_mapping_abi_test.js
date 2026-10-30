// Run the actual guest C entry points against the CPU shadow, with both
// shared and copied renderer streams. No mocked query answers or C cache.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync, spawn } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_objects.js")); require(hostPath("gl-webgpu/gl_shadow_images.js")); require(hostPath("gl-webgpu/gl_shadow_buffers.js"));
const { GLWebGPUExecutor } = require(hostPath("gl-webgpu/gl_executor.js"));
const { createFakeHost } = require(testPath("gl_fake_gpu.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const P = require(hostPath("gl-webgpu/gl_protocol.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function body(name, prefix = "") {
    const at = source.indexOf(prefix + name + "("); assert.ok(at >= 0, name);
    let end = source.indexOf("{", at) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(at, end);
}
const helperAt = source.indexOf("static int emit_query_typed("), helperEnd = source.indexOf("static GLint query_limit_or_default", helperAt);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-buffer-mapping-"));
const names = ["glGetBufferSubDataARB", "glMapBufferARB", "glUnmapBufferARB", "glGetBufferParameterivARB", "glGetBufferPointervARB", "glReadPixels"];
const c = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef int32_t GLint; typedef uint32_t GLuint; typedef uint16_t GLushort;
typedef float GLfloat; typedef double GLdouble; typedef unsigned char GLboolean; typedef void GLvoid;
typedef int32_t GLintptrARB; typedef int32_t GLsizeiptrARB; typedef int32_t GLsizei; typedef size_t SIZE_T; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define GetProcessHeap() 0
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
${["FALSE", "TRUE", "INVALID_OPERATION", "INVALID_VALUE", "INVALID_ENUM", "OUT_OF_MEMORY", "ARRAY_BUFFER_ARB", "PIXEL_PACK_BUFFER", "RGBA", "UNSIGNED_BYTE", "READ_ONLY_ARB", "WRITE_ONLY_ARB", "READ_WRITE_ARB",
    "BUFFER_SIZE_ARB", "BUFFER_USAGE_ARB", "BUFFER_ACCESS_ARB", "BUFFER_MAPPED_ARB", "BUFFER_MAP_POINTER_ARB"]
    .map(p => "#define GL_" + p + " 0x" + (GL[p] ?? (p === "TRUE" ? 1 : 0)).toString(16)).join("\n")}
static unsigned char wire[700000]; static uint32_t wire_size, header_size, error, updates;
static void v86gl_set_error(GLenum value) { if(!error) error=value; }
static uint32_t read_u32le(const uint8_t* p) { uint32_t v; memcpy(&v,p,4); return v; }
static int reserve_pci_record(uint16_t opcode, const void* payload, uint32_t size, uint8_t** out) {
    (void)payload; assert(size+8 < sizeof(wire)); header_size=size>=0xFFFF?8:4;
    uint16_t small=size>=0xFFFF?0xFFFF:(uint16_t)size;
    memcpy(wire,&opcode,2); memcpy(wire+2,&small,2); if(header_size==8)memcpy(wire+4,&size,4);
    wire_size=header_size+size; memset(wire+header_size,0,size); *out=wire+header_size; return 1;
}
static int emit_pci_batch(BOOL present) {
    (void)present; assert(fwrite(&wire_size,4,1,stdout)==1); assert(fwrite(wire,1,wire_size,stdout)==wire_size); fflush(stdout);
    assert(fread(wire,1,wire_size,stdin)==wire_size); return 1;
}
${source.slice(helperAt, helperEnd)}
typedef struct { uint32_t size; GLenum usage,access; BOOL mapped; uint8_t* data; } BufferObjectState;
static BufferObjectState local;
static GLuint g_pixel_pack_buffer_binding=7;
static BufferObjectState* bound_buffer_state(GLenum target) { assert(target==GL_ARRAY_BUFFER_ARB); return &local; }
static BufferObjectState* find_buffer_state(GLuint name,BOOL create) { assert(name==7 && !create); return &local; }
static void emit_buffer_sub_data(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid* data) {
    V86GLRecordBufferSubData request; uint8_t* args;
    memset(&request,0,sizeof(request)); request.target=target; request.offset=offset; request.size=size; request.data_size=size;
    reserve_pci_record(GLFN_BUFFER_SUB_DATA,NULL,sizeof(request)+size,&args); memcpy(args,&request,sizeof(request));
    memcpy(args+sizeof(request),data,size); assert(emit_pci_batch(FALSE)); ++updates;
}
static uint32_t gl_read_pixel_span(GLsizei width,GLsizei height,GLenum format,GLenum type) {
    assert(width==1 && height==1 && format==GL_RGBA && type==GL_UNSIGNED_BYTE); return 4;
}
${body("buffer_data_at", "static const uint8_t* ")}
${body("pack_pixel_pointer", "static GLvoid* ")}
static int emit_read_pixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,uint32_t size,GLvoid* destination) {
    (void)x;(void)y;(void)width;(void)height;(void)format;(void)type; assert(size==4);
    const uint8_t pixel[]={20,90,170,255}; memcpy(destination,pixel,4); return 1;
}
${["emit_query_buffer_data", "emit_query_buffer_map", "emit_query_buffer_unmap"].map(n => body(n, "static int ")).join("\n")}
${names.map(n => body(n, (n === "glMapBufferARB" ? "GLvoid*" : n === "glUnmapBufferARB" ? "GLboolean" : "void") + " APIENTRY ")).join("\n")}
static uint8_t pattern(unsigned int i) { return (i*7+3)%251; }
int main(void) {
    uint8_t* out=malloc(600000); assert(out); GLint value; GLvoid* pointer;
    memset(out,0xCC,600000); glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB,3,600000,out);
    for(unsigned int i=0;i<600000;i++)assert(out[i]==pattern(i+3)); assert(!error);
    memset(out,0xCC,600000); glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB,600031,2,out);
    assert(error==GL_INVALID_VALUE && out[0]==0xCC && out[1]==0xCC); error=0;
    uint8_t* mapped=glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_READ_WRITE_ARB); assert(mapped && !error);
    assert(mapped[17]==pattern(17) && mapped[600031]==pattern(600031));
    glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_MAPPED_ARB,&value); assert(value==1);
    glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_ACCESS_ARB,&value); assert(value==GL_READ_WRITE_ARB);
    pointer=NULL; glGetBufferPointervARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_MAP_POINTER_ARB,&pointer);
    assert((uintptr_t)pointer==(uint32_t)(uintptr_t)mapped);
    assert(glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_READ_ONLY_ARB)==NULL && error==GL_INVALID_OPERATION); error=0;
    memset(out,0xCC,4); glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB,0,4,out);
    assert(error==GL_INVALID_OPERATION && out[0]==0xCC && out[3]==0xCC); error=0;
    mapped[17]=230; assert(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB)==GL_TRUE && updates==1 && !error);
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB,17,1,out); assert(out[0]==230);
    pointer=(GLvoid*)42; glGetBufferPointervARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_MAP_POINTER_ARB,&pointer); assert(pointer==NULL);
    assert(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB)==GL_FALSE && error==GL_INVALID_OPERATION); error=0;
    assert(glMapBufferARB(GL_ARRAY_BUFFER_ARB,0)==NULL && error==GL_INVALID_ENUM); error=0;
    mapped=glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_READ_ONLY_ARB); assert(mapped && mapped[17]==230 && !error);
    assert(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB)==GL_TRUE && updates==1);
    pointer=(GLvoid*)42; glGetBufferPointervARB(GL_ARRAY_BUFFER_ARB,0,&pointer);
    assert(error==GL_INVALID_ENUM && pointer==(GLvoid*)42); error=0;
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,(GLvoid*)20);
    assert(updates==2 && !error); glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB,20,4,out);
    assert(out[0]==20 && out[1]==90 && out[2]==170 && out[3]==255);
    mapped=glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_READ_ONLY_ARB); assert(mapped);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,(GLvoid*)20); assert(error==GL_INVALID_OPERATION && updates==2); error=0;
    assert(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB)==GL_TRUE); free(out); free(local.data); return 0;
}
`;
(async () => {
    try {
        fs.writeFileSync(path.join(dir, "test.c"), c);
        execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
        for (const copied of [false, true]) {
            const shadow = new GLShadow(), { host } = createFakeHost(), gpu = new GLWebGPUExecutor(null, { host, strict: true }); await gpu.initialize();
            const data = Uint8Array.from({ length: 600032 }, (_, i) => (i * 7 + 3) % 251);
            const initial = new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1).call("BIND_BUFFER", GL.ARRAY_BUFFER, 7)
                .call("BIND_BUFFER", GL.PIXEL_PACK_BUFFER, 7).bufferData(GL.ARRAY_BUFFER, data.length, GL.STATIC_DRAW, data).bytes();
            shadow.process(initial); gpu.submit(copied ? initial.slice() : initial, {});
            const child = spawn(path.join(dir, "test")), records = []; let pending = Buffer.alloc(0), stderr = "", failure;
            child.stderr.on("data", b => { stderr += b; });
            child.stdout.on("data", bytes => {
                pending = Buffer.concat([pending, bytes]);
                try { while (pending.length >= 4 && pending.length >= 4 + pending.readUInt32LE(0)) {
                    const length = pending.readUInt32LE(0), command = Uint8Array.from(pending.subarray(4, 4 + length)); pending = pending.subarray(4 + length);
                    const view = new DataView(command.buffer), fn = view.getUint16(0, true), offset = view.getUint16(2, true) === 0xFFFF ? 8 : 4;
                    records.push([fn, fn === P.GLFN.QUERY_BUFFER_DATA ? P.decodeRecord("QUERY_BUFFER_DATA", view, offset) : null]);
                    shadow.process(command); const answered = command.slice(); gpu.submit(copied ? command.slice() : command, {});
                    assert.deepEqual(command, answered, "the renderer must not alter CPU replies"); child.stdin.write(command);
                } } catch (error) { failure = error; child.kill(); }
            });
            const timer = setTimeout(() => child.kill(), 30000);
            const code = await new Promise((resolve, reject) => { child.once("error", reject); child.once("close", resolve); }); clearTimeout(timer);
            if (failure) throw failure; assert.equal(code, 0, stderr); assert.equal(pending.length, 0);
            const chunks = records.filter(([fn]) => fn === P.GLFN.QUERY_BUFFER_DATA).map(([, r]) => r);
            assert.deepEqual(chunks.slice(0, 3).map(r => [r.offset, r.size, r.range_size]), [[3, 262144, 600000], [262147, 262144, 337856], [524291, 75712, 75712]]);
            assert.equal(shadow.boundBuffer(GL.ARRAY_BUFFER).data[17], 230); assert.equal(gpu.bufferFor(GL.ARRAY_BUFFER).shadow[17], 230);
            assert.equal(shadow.boundBuffer(GL.ARRAY_BUFFER).mapped, false); assert.equal(shadow.boundBuffer(GL.ARRAY_BUFFER).pointer, 0);
            assert.equal(gpu.stats.refusals, 0); assert.equal(gpu.hostErrors.length, 0);
        }
        console.log("gl_buffer_mapping_abi_test: actual C read/map/unmap/pointer getters round-trip 600 KB through CPU shadow and both renderer modes (passed)");
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
