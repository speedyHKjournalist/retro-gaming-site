#!/usr/bin/env node
// Actual guest span helpers, including overlapping rows and overflow guards,
// then the CPU validator consuming the resulting declared upload length.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_objects.js")); require(hostPath("gl-webgpu/gl_shadow_textures.js"));
const caps = require(hostPath("gl-webgpu/gl_caps.js")), P = require(hostPath("gl-webgpu/gl_protocol.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function extract(name) {
    const match = new RegExp("static uint32_t " + name + "\\([^)]*\\)\\s*\\{").exec(source); assert.ok(match, name);
    let end = match.index + match[0].length, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(match.index, end);
}
const functions = ["align_u32", "gl_format_components", "gl_pixel_bytes", "gl_pixel_span_with_state", "gl_pixel_row_stride",
    "gl_pixel_span_3d_with_state", "gl_bitmap_span", "gl_pixel_tight_span", "gl_pixel_tight_span_3d"].map(extract).join("\n");
const values = { ...GL, BGR_EXT: GL.BGR, BGRA_EXT: GL.BGRA, DEPTH_STENCIL_EXT: GL.DEPTH_STENCIL };
const enums = [...new Set(functions.match(/\bGL_[A-Z0-9_]+\b/g))];
for (const name of enums) assert.ok(values[name.slice(3)] !== undefined, name);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-pixel-span-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned int GLenum;
typedef int GLint, GLsizei;
${enums.map(name => "#define " + name + " 0x" + values[name.slice(3)].toString(16)).join("\n")}
static GLint g_unpack_alignment=4, g_unpack_row_length=9, g_unpack_skip_rows=1, g_unpack_skip_pixels=3;
${functions}
int main(void) {
    const uint32_t spans[]={
        gl_pixel_span_with_state(1,2,GL_RGB,GL_UNSIGNED_BYTE,4,0,0,0),
        gl_pixel_span_with_state(3,2,GL_RGBA,GL_UNSIGNED_BYTE,4,1,0,0),
        gl_pixel_span_3d_with_state(2,2,2,GL_RGB,GL_UNSIGNED_SHORT,8,4,1,1,3,1),
        gl_pixel_span_with_state(3,2,GL_BGRA_EXT,GL_UNSIGNED_SHORT_4_4_4_4,8,0,0,0),
        gl_pixel_row_stride(3,GL_RGBA,GL_UNSIGNED_BYTE,4,1),
        gl_bitmap_span(6,2),
        gl_format_components(GL_COLOR_INDEX),gl_format_components(GL_STENCIL_INDEX),
        gl_pixel_span_with_state(1,2,GL_RGBA,GL_FLOAT,8,INT32_MAX,0,0),
        gl_pixel_span_3d_with_state(1,1,INT32_MAX,GL_RGBA,GL_UNSIGNED_BYTE,4,1024,0,0,INT32_MAX,INT32_MAX),
        gl_pixel_tight_span(INT32_MAX,INT32_MAX,GL_RGBA,GL_FLOAT),
        gl_pixel_tight_span_3d(INT32_MAX,INT32_MAX,INT32_MAX,GL_RGBA,GL_FLOAT),
        gl_pixel_span_with_state(1,2,GL_RGBA,GL_UNSIGNED_BYTE,4,1<<30,0,0),
        gl_pixel_span_3d_with_state(1,1,(1<<20)+1,GL_RGBA,GL_UNSIGNED_BYTE,4,1<<20,0,0,1<<22,0),
        gl_pixel_tight_span_3d(16385,268419073,262144,GL_RGBA,GL_FLOAT),
        gl_pixel_tight_span_3d(2,3,4,GL_RGBA,GL_UNSIGNED_BYTE),
    };
    assert(fwrite(spans,sizeof(spans),1,stdout)==1); return 0;
}
`;
    const file = path.join(dir, "test.c"), binary = path.join(dir, "test"); fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);
    const output = execFileSync(binary), spans = Array.from({ length: output.length / 4 }, (_, i) => output.readUInt32LE(i * 4));
    assert.deepEqual(spans, [7, 16, 210, 14, 4, 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 96]);
    for (const complete of [false, true]) {
        const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1).call("PIXEL_STOREI", GL.UNPACK_ALIGNMENT, 8)
            .call("PIXEL_STOREI", GL.UNPACK_ROW_LENGTH, 4).call("PIXEL_STOREI", GL.UNPACK_IMAGE_HEIGHT, 3)
            .call("PIXEL_STOREI", GL.UNPACK_SKIP_IMAGES, 1).call("PIXEL_STOREI", GL.UNPACK_SKIP_ROWS, 1).call("PIXEL_STOREI", GL.UNPACK_SKIP_PIXELS, 1);
        const bytes = spans[2] - +!complete, header = P.encodeRecord("TEX_IMAGE_3D", { target: GL.TEXTURE_3D,
            level: 0, internalformat: GL.RGB8, width: 2, height: 2, depth: 2, border: 0, format: GL.RGB, type: GL.UNSIGNED_SHORT, data_size: bytes });
        const payload = new Uint8Array(header.length + bytes); payload.set(header); stream.record(P.GLFN.TEX_IMAGE_3D, payload);
        const shadow = new GLShadow(); shadow.setCapabilities(caps.capabilities({}, {})); shadow.process(stream.bytes());
        assert.equal(shadow.current.error, complete ? 0 : GL.INVALID_OPERATION);
    }
    console.log("gl_pixel_span_abi_test: actual C padding/overlap/bitmap/overflow calculations match guarded CPU upload validation (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
