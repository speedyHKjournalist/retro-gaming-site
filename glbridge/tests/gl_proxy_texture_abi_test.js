#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_objects.js"));
require(hostPath("gl-webgpu/gl_shadow_textures.js"));
const caps = require(hostPath("gl-webgpu/gl_caps.js")), P = require(hostPath("gl-webgpu/gl_protocol.js"));
const { GLWebGPUExecutor } = require(hostPath("gl-webgpu/gl_executor.js"));
const { createFakeHost } = require(testPath("gl_fake_gpu.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function extract(signature) {
    const at = source.indexOf(signature); assert.ok(at >= 0, signature);
    let end = source.indexOf("{", at) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(at, end);
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-proxy-abi-"));
(async () => {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum;
typedef int GLint, GLsizei, BOOL;
typedef void GLvoid;
typedef size_t SIZE_T;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_OUT_OF_MEMORY 0x505
#define GL_TEXTURE_1D 0xDE0
#define GL_PROXY_TEXTURE_1D 0x8063
#define GL_PROXY_TEXTURE_2D 0x8064
#define GL_PROXY_TEXTURE_3D 0x8070
#define GL_PROXY_TEXTURE_CUBE_MAP 0x851B
#define CopyMemory memcpy
static unsigned int g_pixel_unpack_buffer_binding = 55;
static int forbidden(void) { abort(); return 0; }
#define gl_pixel_span(...) (forbidden(),0u)
#define gl_pixel_span_3d(...) (forbidden(),0u)
#define unpack_pixel_pointer(pointer,size) ((void)(pointer),(void)(size),forbidden(),(const GLvoid*)0)
#define cache_texture_image(...) forbidden()
#define cache_texture_image_3d(...) forbidden()
#define cache_compressed_texture_image(...) forbidden()
#define bound_texture_state(...) (forbidden(),(void*)0)
static void v86gl_set_error(GLenum e) { (void)e; forbidden(); }
static uint8_t* alloc_payload(uint32_t size) { (void)size; forbidden(); return NULL; }
static void* GetProcessHeap(void) { forbidden(); return NULL; }
static void HeapFree(void* heap, int flags, void* p) { (void)heap; (void)flags; (void)p; forbidden(); }
static void emit_gl_call(uint16_t opcode, const void* payload, uint32_t size) {
    const uint16_t length=(uint16_t)size;
    assert(fwrite(&opcode,2,1,stdout)==1 && fwrite(&length,2,1,stdout)==1 && fwrite(payload,1,size,stdout)==size);
}
${["static BOOL is_proxy_texture_target(", "static BOOL emit_proxy_texture_image(", "void APIENTRY glTexImage1D(",
    "void APIENTRY glTexImage2D(", "void APIENTRY glTexImage3DEXT(", "static void emit_compressed_tex_image("].map(extract).join("\n")}
int main(void) {
    const GLvoid* invalid=(const GLvoid*)(uintptr_t)1;
    glTexImage1D(GL_PROXY_TEXTURE_1D,0,0x8040,16,0,0x1909,0x1401,invalid);
    glTexImage2D(GL_PROXY_TEXTURE_2D,0,0x8058,64,32,0,0x1908,0x1401,invalid);
    glTexImage3DEXT(GL_PROXY_TEXTURE_3D,0,0x881A,8,4,2,0,0x1908,0x1406,invalid);
    glTexImage2D(GL_PROXY_TEXTURE_CUBE_MAP,0,0x8051,8,8,0,0x1907,0x1401,invalid);
    emit_compressed_tex_image(GLFN_COMPRESSED_TEX_IMAGE_2D,GL_PROXY_TEXTURE_2D,0,0x83F3,8,4,1,0,32,invalid);
    return 0;
}
`;
    const file = path.join(dir, "test.c"), binary = path.join(dir, "test"); fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), file, "-o", binary]);
    const wire = execFileSync(binary), view = new DataView(wire.buffer, wire.byteOffset, wire.byteLength);
    let at = 0, count = 0;
    while (at < wire.length) {
        const name = P.NAME_BY_OPCODE[view.getUint16(at, true)], length = view.getUint16(at + 2, true);
        assert.equal(length, P.RECORDS[name].size, "a proxy must carry no pixel bytes");
        at += 4 + length; ++count;
    }
    assert.equal(count, 5);
    const shadow = new GLShadow(); shadow.setCapabilities(caps.capabilities({}, {}));
    const initial = new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1); shadow.process(initial.bytes());
    const { host, log } = createFakeHost(), executor = new GLWebGPUExecutor(null, { host, strict: true }); await executor.initialize();
    executor.submit(initial.bytes(), {}); const allocated = log.textures.length;
    shadow.process(wire); executor.submit(wire.slice(), {});
    assert.equal(shadow.current.error, 0); assert.equal(shadow.stats.rejected, 0);
    for (const [target, dimensions, format] of [[GL.PROXY_TEXTURE_1D, [16, 1, 1], GL.LUMINANCE8],
        [GL.PROXY_TEXTURE_2D, [8, 4, 1], GL.COMPRESSED_RGBA_S3TC_DXT5_EXT],
        [GL.PROXY_TEXTURE_3D, [8, 4, 2], GL.RGBA16F_ARB], [GL.PROXY_TEXTURE_CUBE_MAP, [8, 8, 1], GL.RGB8]]) {
        const image = shadow.current.proxyImages.get(target).get(0);
        assert.deepEqual([image.width, image.height, image.depth], dimensions); assert.equal(image.internalFormat, format);
    }
    assert.equal(log.textures.length, allocated); assert.equal(shadow.current.group.textureImages.size, 0);
    assert.equal(executor.current.shareGroup.textureImages.size, 0);
    assert.equal(executor.hostErrors.length, 0); assert.equal(executor.stats.refusals, 0);
    console.log("gl_proxy_texture_abi_test: actual C entry points bypass all pixel/PBO reads and caches; CPU owns probes; host allocates no images (passed)");
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => fs.rmSync(dir, { recursive: true, force: true }));
