#!/usr/bin/env node
// Compile the DLL's actual typed-query function. Its requests go through the
// real CPU shadow and executor, and the resulting bytes go back through the
// C function's decoding: a wrong C layout/conversion cannot agree with itself.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_objects.js"));
require(hostPath("gl-webgpu/gl_shadow_state.js"));
require(hostPath("gl-webgpu/gl_shadow_raster.js"));
require(hostPath("gl-webgpu/gl_shadow_fixed.js"));
require(hostPath("gl-webgpu/gl_shadow_misc.js"));
require(hostPath("gl-webgpu/gl_shadow_rasterpos.js"));
require(hostPath("gl-webgpu/gl_shadow_pixelmaps.js"));
require(hostPath("gl-webgpu/gl_shadow_images.js"));
require(hostPath("gl-webgpu/gl_shadow_textures.js"));
const caps = require(hostPath("gl-webgpu/gl_caps.js"));
const { GLWebGPUExecutor } = require(hostPath("gl-webgpu/gl_executor.js"));
const { createFakeHost } = require(testPath("gl_fake_gpu.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const P = require(hostPath("gl-webgpu/gl_protocol.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const start = source.indexOf("static int emit_query_typed("), end = source.indexOf("static GLint query_limit_or_default", start);
assert.ok(start >= 0 && end > start);
const helper = source.slice(start, end);
const fbStart = source.indexOf("void APIENTRY glGetFramebufferAttachmentParameterivEXT("),
    fbEnd = source.indexOf("void APIENTRY glGetFramebufferAttachmentParameteriv(", fbStart);
assert.ok(fbStart >= 0 && fbEnd > fbStart);
const rbStart = source.indexOf("void APIENTRY glGetRenderbufferParameterivEXT("),
    rbEnd = source.indexOf("void APIENTRY glGetRenderbufferParameteriv(", rbStart);
assert.ok(rbStart >= 0 && rbEnd > rbStart);
function functionBody(name) {
    const at = source.indexOf("void APIENTRY " + name + "("); assert.ok(at >= 0);
    let end = source.indexOf("{", at) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(at, end);
}
const texturePnames = ["TEXTURE_MIN_FILTER", "TEXTURE_MAG_FILTER", "TEXTURE_WRAP_S", "TEXTURE_WRAP_T", "TEXTURE_WRAP_R",
    "TEXTURE_BASE_LEVEL", "TEXTURE_MAX_LEVEL", "TEXTURE_MIN_LOD", "TEXTURE_MAX_LOD", "TEXTURE_PRIORITY", "TEXTURE_RESIDENT", "TEXTURE_BORDER_COLOR"];
// The four public getters must use this compiled function with the right type.
for (const [name, kind] of [["Integer", "INTEGER"], ["Float", "FLOAT"], ["Double", "DOUBLE"], ["Boolean", "BOOLEAN"]]) {
    const body = source.slice(source.indexOf("void APIENTRY glGet" + name + "v("));
    assert.ok(body.slice(0, body.indexOf("\n__declspec(dllexport)")).includes("emit_query_state(pname, V86GL_STATE_" + kind + ", params)"));
}
assert.ok(source.slice(source.indexOf("GLboolean APIENTRY glIsEnabled(")).startsWith(
    "GLboolean APIENTRY glIsEnabled(GLenum cap)"));
assert.ok(source.slice(source.indexOf("GLboolean APIENTRY glIsEnabled("), source.indexOf("void APIENTRY glViewport("))
    .includes("emit_query_state(cap, V86GL_STATE_IS_ENABLED, &enabled)"));
const c = `
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum;
typedef int32_t GLint;
typedef uint32_t GLuint;
typedef uint16_t GLushort;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned char GLboolean;
typedef int BOOL;
#define FALSE 0
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_INVALID_OPERATION 0x502
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_ENUM 0x500
#define GL_NONE 0
#define APIENTRY
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT 0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT 0x8CD1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT 0x8CD2
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT 0x8CD3
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT 0x8CD4
#define GL_RENDERBUFFER_WIDTH_EXT 0x8D42
#define GL_RENDERBUFFER_HEIGHT_EXT 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT_EXT 0x8D44
#define GL_RENDERBUFFER_RED_SIZE_EXT 0x8D50
#define GL_RENDERBUFFER_GREEN_SIZE_EXT 0x8D51
#define GL_RENDERBUFFER_BLUE_SIZE_EXT 0x8D52
#define GL_RENDERBUFFER_ALPHA_SIZE_EXT 0x8D53
#define GL_RENDERBUFFER_DEPTH_SIZE_EXT 0x8D54
#define GL_RENDERBUFFER_STENCIL_SIZE_EXT 0x8D55
${texturePnames.map(p => "#define GL_" + p + " 0x" + GL[p].toString(16)).join("\n")}
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
static unsigned char buffer[4096];
static unsigned int length, error;
static int encode;
static void v86gl_set_error(GLenum e) { if (!error) error = e; }
static uint32_t read_u32le(const uint8_t* p) { uint32_t v; memcpy(&v,p,4); return v; }
static int reserve_pci_record(uint16_t fn, const void* data, uint32_t size, uint8_t** out) {
    uint16_t short_size = (uint16_t)size;
    (void)data;
    assert((fn == GLFN_QUERY_STATE || fn == GLFN_QUERY_PARAMETER) && size < sizeof(buffer)-4);
    memcpy(buffer, &fn, 2); memcpy(buffer+2, &short_size, 2);
    length = size+4; *out = buffer+4; return 1;
}
static int emit_pci_batch(BOOL present) {
    (void)present;
    if (encode) { assert(fwrite(buffer,1,length,stdout) == length); return 0; }
    assert(fread(buffer,1,length,stdin) == length);
    return 1;
}
${helper}
typedef struct { GLenum object_type, object_name, texture_target; GLint texture_level, texture_zoffset; } FramebufferAttachmentState;
static int valid_framebuffer_target(GLenum target) { (void)target; return 1; }
static FramebufferAttachmentState* bound_framebuffer_attachment(GLenum target, GLenum attachment, int create) {
    (void)target; (void)attachment; (void)create; return NULL;
}
${source.slice(fbStart, fbEnd)}
typedef struct { GLenum internalformat; GLint width, height; } RenderbufferObjectState;
static GLuint g_renderbuffer_binding = 1;
static int valid_renderbuffer_target(GLenum target) { (void)target; return 1; }
static RenderbufferObjectState* find_renderbuffer_state(GLuint name, int create) { (void)name; (void)create; return NULL; }
static GLint renderbuffer_component_bits(GLenum format, GLenum pname) { (void)format; (void)pname; return 0; }
${source.slice(rbStart, rbEnd)}
typedef struct { GLint min_filter, mag_filter, wrap_s, wrap_t, wrap_r, base_level, max_level;
    GLfloat min_lod, max_lod, priority, border_color[4]; } TextureObjectState;
static TextureObjectState* bound_texture_state(GLenum target, int create) { (void)target; (void)create; return NULL; }
${functionBody("glGetTexParameteriv")}
${functionBody("glGetTexParameterfv")}
static int get_texture_level_parameter(GLenum target, GLint level, GLenum pname, GLint* value) {
    (void)target; (void)level; (void)pname; (void)value; assert(0); return 0;
}
${functionBody("glGetTexLevelParameteriv")}
${functionBody("glGetTexLevelParameterfv")}
int main(int argc, char** argv) {
    union { GLdouble align; unsigned char bytes[4096]; } out;
    unsigned int kind, pname;
    int result;
    assert(argc == 4 || argc == 6 || argc == 7 || argc == 8);
    encode = atoi(argv[1]); kind = (unsigned int)atoi(argv[2]); pname = (unsigned int)strtoul(argv[3], NULL, 0);
    memset(out.bytes, 0xA5, sizeof(out.bytes));
    if (argc == 8 && atoi(argv[4]) == V86GL_PARAMETER_TEXTURE_LEVEL) {
        if (kind == V86GL_STATE_INTEGER) glGetTexLevelParameteriv((GLenum)strtoul(argv[5], NULL, 0), atoi(argv[7]), pname, (GLint*)(out.bytes+8));
        else glGetTexLevelParameterfv((GLenum)strtoul(argv[5], NULL, 0), atoi(argv[7]), pname, (GLfloat*)(out.bytes+8));
        result = error ? -1 : 1;
    } else if (argc == 8) {
        glGetFramebufferAttachmentParameterivEXT((GLenum)strtoul(argv[5], NULL, 0), (GLenum)strtoul(argv[7], NULL, 0), pname, (GLint*)(out.bytes+8));
        result = error ? -1 : 1;
    } else if (argc == 6 && atoi(argv[4]) == V86GL_PARAMETER_RENDERBUFFER) {
        glGetRenderbufferParameterivEXT((GLenum)strtoul(argv[5], NULL, 0), pname, (GLint*)(out.bytes+8));
        result = error ? -1 : 1;
    } else if (argc == 6 && atoi(argv[4]) == V86GL_PARAMETER_TEXTURE) {
        if (kind == V86GL_STATE_INTEGER) glGetTexParameteriv((GLenum)strtoul(argv[5], NULL, 0), pname, (GLint*)(out.bytes+8));
        else glGetTexParameterfv((GLenum)strtoul(argv[5], NULL, 0), pname, (GLfloat*)(out.bytes+8));
        result = error ? -1 : 1;
    } else result = argc == 4 ? emit_query_state(pname, kind, out.bytes+8) : argc == 6 ?
        emit_query_parameter((unsigned int)atoi(argv[4]), (GLenum)strtoul(argv[5], NULL, 0), pname, kind, out.bytes+8) :
        emit_query_parameter_count((unsigned int)atoi(argv[4]), (GLenum)strtoul(argv[5], NULL, 0), pname, kind, out.bytes+8, (unsigned int)atoi(argv[6]));
    if (!encode) {
        assert(fwrite(&result,4,1,stdout) == 1);
        assert(fwrite(&error,4,1,stdout) == 1);
        assert(fwrite(out.bytes,1,sizeof(out.bytes),stdout) == sizeof(out.bytes));
    }
    return 0;
}
`;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-state-abi-"));
(async () => {
    const file = path.join(dir, "test.c"), binary = path.join(dir, "test");
    fs.writeFileSync(file, c);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror",
        "-I", path.join(__dirname, "../openglproxy"), file, "-o", binary]);
    const { host } = createFakeHost(), executor = new GLWebGPUExecutor(null, { host }); await executor.initialize();
    for (const [kind, pname, expected, family, target, capacity, index] of [
        [P.C.V86GL_STATE_INTEGER, GL.VIEWPORT, [-11, 7, 90, 30]],
        [P.C.V86GL_STATE_FLOAT, GL.DEPTH_RANGE, [0.25, 0.75]],
        [P.C.V86GL_STATE_DOUBLE, GL.DEPTH_RANGE, [0.25, 0.75]],
        [P.C.V86GL_STATE_BOOLEAN, GL.COLOR_WRITEMASK, [1, 0, 1, 0]],
        [P.C.V86GL_STATE_INTEGER, GL.COLOR_CLEAR_VALUE, [1073741823, 0, 2147483647, 536870911]],
        [P.C.V86GL_STATE_INTEGER, GL.STENCIL_VALUE_MASK, [-1]],
        [P.C.V86GL_STATE_FLOAT, GL.STENCIL_VALUE_MASK, [4294967296]],
        [P.C.V86GL_STATE_DOUBLE, GL.STENCIL_VALUE_MASK, [4294967295]],
        [P.C.V86GL_STATE_IS_ENABLED, GL.DITHER, [1]],
        [P.C.V86GL_STATE_IS_ENABLED, GL.BLEND, [0]],
        [P.C.V86GL_STATE_INTEGER, GL.FOG_COLOR, [1073741823, 0, 2147483647, 536870911]],
        [P.C.V86GL_STATE_DOUBLE, GL.POINT_DISTANCE_ATTENUATION, [1, 2, 3]],
        [P.C.V86GL_STATE_INTEGER, GL.INDEX_SHIFT, [3]],
        [P.C.V86GL_STATE_DOUBLE, GL.CURRENT_RASTER_POSITION, [34, 22, 0.5, 2]],
        [P.C.V86GL_STATE_BOOLEAN, GL.CURRENT_RASTER_POSITION_VALID, [1]],
        [P.C.V86GL_STATE_DOUBLE, GL.MODELVIEW_MATRIX, [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1]],
        [P.C.V86GL_STATE_DOUBLE, GL.POSITION, [7, 12, 19, 1], P.C.V86GL_PARAMETER_LIGHT, GL.LIGHT0 + 2],
        [P.C.V86GL_STATE_INTEGER, GL.DIFFUSE, [2147483647, 0, 1073741823, 2147483647], P.C.V86GL_PARAMETER_MATERIAL, GL.BACK],
        [P.C.V86GL_STATE_FLOAT, GL.TEXTURE_ENV_COLOR, [1, 0, 0.5, 1], P.C.V86GL_PARAMETER_TEX_ENV, GL.TEXTURE_ENV],
        [P.C.V86GL_STATE_DOUBLE, GL.OBJECT_PLANE, [1, 2, 3, 4], P.C.V86GL_PARAMETER_TEX_GEN, GL.S],
        [P.C.V86GL_STATE_DOUBLE, 0, [0.5, 0, 0, -2.5], P.C.V86GL_PARAMETER_CLIP_PLANE, GL.CLIP_PLANE0],
        [P.C.V86GL_STATE_UNSIGNED_INT, 0, [0, 2147483647, 4294967295, 4294967295], P.C.V86GL_PARAMETER_PIXEL_MAP, GL.PIXEL_MAP_R_TO_R],
        [P.C.V86GL_STATE_UNSIGNED_SHORT, 0, [0, 32768, 65535, 65535], P.C.V86GL_PARAMETER_PIXEL_MAP, GL.PIXEL_MAP_R_TO_R],
        [P.C.V86GL_STATE_FLOAT, 0, Array.from({ length: 32 }, (_, i) => Math.fround(i / 31)), P.C.V86GL_PARAMETER_PIXEL_MAP, GL.PIXEL_MAP_G_TO_G, 256],
        [P.C.V86GL_STATE_FLOAT, GL.CURRENT_INDEX, [1]],
        [P.C.V86GL_STATE_BOOLEAN, 0, [0], P.C.V86GL_PARAMETER_IS_OBJECT, P.C.V86GL_OBJECT_KIND_TEXTURE],
        [P.C.V86GL_STATE_BOOLEAN, 7, [1], P.C.V86GL_PARAMETER_IS_OBJECT, P.C.V86GL_OBJECT_KIND_TEXTURE],
        [P.C.V86GL_STATE_BOOLEAN, 8, [0], P.C.V86GL_PARAMETER_IS_OBJECT, P.C.V86GL_OBJECT_KIND_TEXTURE],
        [P.C.V86GL_STATE_BOOLEAN, 9, [1], P.C.V86GL_PARAMETER_IS_OBJECT, P.C.V86GL_OBJECT_KIND_RENDERBUFFER],
        [P.C.V86GL_STATE_BOOLEAN, 3, [1], P.C.V86GL_PARAMETER_IS_OBJECT, P.C.V86GL_OBJECT_KIND_FRAMEBUFFER],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_BINDING_2D, [7]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_TEXTURE_IMAGE_UNITS, [8]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS, [8]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS, [8]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_ATTRIB_STACK_DEPTH, [16]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_CLIENT_ATTRIB_STACK_DEPTH, [16]],
        [P.C.V86GL_STATE_INTEGER, GL.MAX_COLOR_MATRIX_STACK_DEPTH_SGI, [4]],
        [P.C.V86GL_STATE_INTEGER, GL.ATTRIB_STACK_DEPTH, [0]],
        [P.C.V86GL_STATE_INTEGER, GL.CLIENT_ATTRIB_STACK_DEPTH, [0]],
        [P.C.V86GL_STATE_FLOAT, GL.POINT_SIZE_RANGE, [1, 64]],
        [P.C.V86GL_STATE_DOUBLE, GL.LINE_WIDTH_RANGE, [1, 64]],
        [P.C.V86GL_STATE_INTEGER, GL.ALIASED_LINE_WIDTH_RANGE, [1, 64]],
        [P.C.V86GL_STATE_FLOAT, GL.POINT_SIZE_GRANULARITY, [1]],
        [P.C.V86GL_STATE_BOOLEAN, GL.RGBA_MODE, [1]],
        [P.C.V86GL_STATE_BOOLEAN, GL.STEREO, [0]],
        [P.C.V86GL_STATE_INTEGER, GL.PROGRAM_ERROR_POSITION_ARB, [-1]],
        [P.C.V86GL_STATE_INTEGER, GL.COMPRESSED_TEXTURE_FORMATS, []],
        [P.C.V86GL_STATE_INTEGER, GL.RED_BITS, [8]],
        [P.C.V86GL_STATE_FLOAT, GL.GREEN_BITS, [8]],
        [P.C.V86GL_STATE_DOUBLE, GL.BLUE_BITS, [8]],
        [P.C.V86GL_STATE_BOOLEAN, GL.ALPHA_BITS, [1]],
        [P.C.V86GL_STATE_DOUBLE, GL.DEPTH_BITS, [0]],
        [P.C.V86GL_STATE_INTEGER, GL.STENCIL_BITS, [0]],
        [P.C.V86GL_STATE_INTEGER, GL.SAMPLES, [0]],
        [P.C.V86GL_STATE_BOOLEAN, GL.SAMPLE_BUFFERS, [0]],
        [P.C.V86GL_STATE_INTEGER, 0x0C01, [GL.COLOR_ATTACHMENT0 + 2]],
        [P.C.V86GL_STATE_INTEGER, 0x0C02, [GL.NONE]],
        [P.C.V86GL_STATE_INTEGER, GL.DRAW_BUFFER0, [GL.COLOR_ATTACHMENT0 + 2]],
        [P.C.V86GL_STATE_INTEGER, GL.DRAW_BUFFER0 + 1, [GL.NONE]],
        [P.C.V86GL_STATE_INTEGER, GL.DRAW_BUFFER0 + 2, [GL.COLOR_ATTACHMENT0 + 1]],
        [P.C.V86GL_STATE_INTEGER, GL.DRAW_BUFFER0 + 7, [GL.NONE]],
        [P.C.V86GL_STATE_INTEGER, GL.FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT, [GL.TEXTURE], P.C.V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT, GL.FRAMEBUFFER_EXT, 16, GL.COLOR_ATTACHMENT0],
        [P.C.V86GL_STATE_INTEGER, GL.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT, [7], P.C.V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT, GL.FRAMEBUFFER_EXT, 16, GL.COLOR_ATTACHMENT0],
        [P.C.V86GL_STATE_INTEGER, GL.FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT, [2], P.C.V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT, GL.FRAMEBUFFER_EXT, 16, GL.COLOR_ATTACHMENT0],
        [P.C.V86GL_STATE_INTEGER, GL.FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT, [0], P.C.V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT, GL.FRAMEBUFFER_EXT, 16, GL.COLOR_ATTACHMENT0],
        [P.C.V86GL_STATE_INTEGER, GL.RENDERBUFFER_WIDTH_EXT, [7], P.C.V86GL_PARAMETER_RENDERBUFFER, GL.RENDERBUFFER_EXT],
        [P.C.V86GL_STATE_INTEGER, GL.RENDERBUFFER_HEIGHT_EXT, [5], P.C.V86GL_PARAMETER_RENDERBUFFER, GL.RENDERBUFFER_EXT],
        [P.C.V86GL_STATE_INTEGER, GL.RENDERBUFFER_INTERNAL_FORMAT_EXT, [GL.STENCIL_INDEX16_EXT], P.C.V86GL_PARAMETER_RENDERBUFFER, GL.RENDERBUFFER_EXT],
        [P.C.V86GL_STATE_INTEGER, GL.RENDERBUFFER_STENCIL_SIZE_EXT, [8], P.C.V86GL_PARAMETER_RENDERBUFFER, GL.RENDERBUFFER_EXT],
        [P.C.V86GL_STATE_INTEGER, GL.RENDERBUFFER_DEPTH_SIZE_EXT, [0], P.C.V86GL_PARAMETER_RENDERBUFFER, GL.RENDERBUFFER_EXT],
        [P.C.V86GL_STATE_FLOAT, GL.TEXTURE_BORDER_COLOR, [0, 0, 0.5, 1], P.C.V86GL_PARAMETER_TEXTURE, GL.TEXTURE_2D],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_BORDER_COLOR, [0, 0, 1073741823, 2147483647], P.C.V86GL_PARAMETER_TEXTURE, GL.TEXTURE_2D],
        [P.C.V86GL_STATE_FLOAT, GL.TEXTURE_PRIORITY, [0.25], P.C.V86GL_PARAMETER_TEXTURE, GL.TEXTURE_2D],
        [P.C.V86GL_STATE_FLOAT, GL.TEXTURE_MAX_ANISOTROPY_EXT, [2.5], P.C.V86GL_PARAMETER_TEXTURE, GL.TEXTURE_2D],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_MAX_ANISOTROPY_EXT, [3], P.C.V86GL_PARAMETER_TEXTURE, GL.TEXTURE_2D],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_WIDTH, [3], P.C.V86GL_PARAMETER_TEXTURE_LEVEL, GL.TEXTURE_2D, 16, 0],
        [P.C.V86GL_STATE_FLOAT, GL.TEXTURE_HEIGHT, [5], P.C.V86GL_PARAMETER_TEXTURE_LEVEL, GL.TEXTURE_2D, 16, 0],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_RED_SIZE, [4], P.C.V86GL_PARAMETER_TEXTURE_LEVEL, GL.TEXTURE_2D, 16, 0],
        [P.C.V86GL_STATE_INTEGER, GL.TEXTURE_WIDTH, [0], P.C.V86GL_PARAMETER_TEXTURE_LEVEL, GL.TEXTURE_2D, 16, 1],
    ]) {
        const args = [String(kind), String(pname), ...(family ? [String(family), String(target), ...(capacity ? [String(capacity)] : []), ...(index !== undefined ? [String(index)] : [])] : [])];
        const wire = execFileSync(binary, ["1", ...args]);
        const s = new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1)
            .call("VIEWPORT", -11, 7, 90, 30).call("DEPTH_RANGE", 0.25, 0.75)
            .call("COLOR_MASK", 1, 0, 1, 0).call("CLEAR_COLOR", 0.5, 0, 1, 0.25)
            .call("TRANSLATEF", 5, 6, 7).call("SCALEF", 2, 3, 4)
            .call("LIGHTFV", GL.LIGHT0 + 2, GL.POSITION, 4, 1, 2, 3, 1)
            .call("MATERIALIV", GL.BACK, GL.DIFFUSE, 4, 2147483647, 0, 1073741824, 2147483647)
            .call("TEX_ENVIV", GL.TEXTURE_ENV, GL.TEXTURE_ENV_COLOR, 4, 2147483647, 0, 1073741824, 2147483647)
            .call("TEX_GENFV", GL.S, GL.OBJECT_PLANE, 4, 1, 2, 3, 4).call("CLIP_PLANE", GL.CLIP_PLANE0, 1, 0, 0, 0);
        s.call("FOGFV", GL.FOG_COLOR, 4, 0.5, 0, 1, 0.25).call("POINT_PARAMETERIV", GL.POINT_DISTANCE_ATTENUATION, 1, 2, 3)
            .call("PIXEL_TRANSFERF", GL.INDEX_SHIFT, 2.75);
        s.call("PUSH_MATRIX").call("LOAD_IDENTITY").call("RASTER_POS4F", 0, 0, 0, 2).call("POP_MATRIX");
        for (const [map, values] of [[GL.PIXEL_MAP_R_TO_R, [-1, 0.5, 1, 2]],
            [GL.PIXEL_MAP_G_TO_G, Array.from({ length: 32 }, (_, i) => i / 31)]]) {
            const payload = new Uint8Array(8 + values.length * 4), view = new DataView(payload.buffer);
            payload.set(P.encodeRecord("PIXEL_MAPFV", { map, count: values.length })); values.forEach((v, i) => view.setFloat32(8 + i * 4, v, true));
            s.record(P.GLFN.PIXEL_MAPFV, payload);
        }
        s.names(P.GLFN.GEN_TEXTURES, [8]).call("BIND_TEXTURE", GL.TEXTURE_2D, 7)
            .call("BIND_RENDERBUFFER", GL.RENDERBUFFER_EXT, 9).call("BIND_FRAMEBUFFER", GL.FRAMEBUFFER_EXT, 3)
            .names(P.GLFN.DRAW_BUFFERS, [GL.COLOR_ATTACHMENT0 + 2, GL.NONE, GL.COLOR_ATTACHMENT0 + 1]).call("READ_BUFFER", GL.NONE);
        s.texImage2D(GL.TEXTURE_2D, 2, GL.RGBA8, 4, 4, GL.RGBA, GL.UNSIGNED_BYTE, new Uint8Array(0));
        s.call("FRAMEBUFFER_TEXTURE", GL.FRAMEBUFFER_EXT, GL.COLOR_ATTACHMENT0, GL.TEXTURE_2D, 7, 2, 0);
        s.call("RENDERBUFFER_STORAGE", GL.RENDERBUFFER_EXT, GL.STENCIL_INDEX16_EXT, 7, 5);
        s.call("TEX_PARAMETERIV", GL.TEXTURE_2D, GL.TEXTURE_BORDER_COLOR, 4, -2147483648, 0, 1073741824, 2147483647)
            .call("TEX_PARAMETERF", GL.TEXTURE_2D, GL.TEXTURE_PRIORITY, 0.25).call("TEX_PARAMETERF", GL.TEXTURE_2D, GL.TEXTURE_MAX_ANISOTROPY_EXT, 2.5);
        s.texImage2D(GL.TEXTURE_2D, 0, GL.RGBA4, 3, 5, GL.RGBA, GL.UNSIGNED_BYTE, new Uint8Array(0));
        const at = s.length; s.record(family ? P.GLFN.QUERY_PARAMETER : P.GLFN.QUERY_STATE, wire.subarray(4));
        const bytes = s.bytes(), shadow = new GLShadow(); shadow.setCapabilities(caps.capabilities({}, {}));
        shadow.process(bytes); executor.submit(bytes, {});
        const answer = bytes.subarray(at);
        const decoded = execFileSync(binary, ["0", ...args], { input: answer });
        assert.equal(decoded.readInt32LE(0), expected ? 1 : 0);
        assert.equal(decoded.readUInt32LE(4), 0);
        const boolean = kind === P.C.V86GL_STATE_BOOLEAN || kind === P.C.V86GL_STATE_IS_ENABLED;
        const output = decoded.subarray(8), itemBytes = kind === P.C.V86GL_STATE_DOUBLE ? 8 : boolean ? 1 : kind === P.C.V86GL_STATE_UNSIGNED_SHORT ? 2 : 4;
        if (expected) expected.forEach((v, i) => {
            const at = 8 + i * itemBytes;
            const actual = kind === P.C.V86GL_STATE_INTEGER ? output.readInt32LE(at) : kind === P.C.V86GL_STATE_UNSIGNED_INT ? output.readUInt32LE(at) : kind === P.C.V86GL_STATE_UNSIGNED_SHORT ? output.readUInt16LE(at) : kind === P.C.V86GL_STATE_FLOAT ? output.readFloatLE(at) : kind === P.C.V86GL_STATE_DOUBLE ? output.readDoubleLE(at) : output[at];
            assert.equal(actual, v);
        });
        assert.ok(output.subarray(0, 8).every(b => b === 0xA5));
        assert.ok(output.subarray(8 + (expected ? expected.length : 0) * itemBytes).every(b => b === 0xA5));
        // A transport/query failure must preserve output, even with a fake
        // oversized count. It must not silently enter the old local path.
        const failed = Buffer.from(answer); failed.writeUInt32LE(2, 4 + P.F.QUERY_STATE.status); failed.writeUInt32LE(GL.INVALID_OPERATION, 4 + P.F.QUERY_STATE.error);
        const error = execFileSync(binary, ["0", ...args], { input: failed });
        assert.equal(error.readInt32LE(0), -1); assert.equal(error.readUInt32LE(4), GL.INVALID_OPERATION);
        assert.ok(error.subarray(8).every(b => b === 0xA5));
    }
    console.log("gl_state_query_abi_test: actual C encoder/decoder, real shadow/executor, four types, enables, unsigned masks, five parameter families and output guards passed");
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => fs.rmSync(dir, { recursive: true, force: true }));
