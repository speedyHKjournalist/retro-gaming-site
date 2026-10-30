"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js"), { hostPath, testPath } = require("./host_paths.js"),
    { Glslang } = require(hostPath("gl-webgpu/gl_glslang.js")), cases = require(testPath("gl_raster_program_cases.js")), { GLStream } = require(testPath("gl_stream_builder.js")), P = require(hostPath("gl-webgpu/gl_protocol.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef unsigned short GLushort;typedef int GLint,BOOL;typedef float GLfloat;typedef double GLdouble;typedef unsigned char GLboolean;
#define TRUE 1
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static GLfloat g_current_raster_position[4],g_current_raster_color[4],g_current_raster_texcoord[4],g_current_color[4]={0.25,0.5,0.75,1},g_current_texcoord[4]={1,2,3,4},g_current_index,g_current_raster_index,g_current_raster_distance;
static GLboolean g_current_raster_valid;
${transport}
${body(source, "raster_pos4f_compat", "static void ")}
${body(source, "window_pos3f_compat", "static void ")}
${body(source, "emit_query_typed", "static int ")}
${body(source, "emit_query_state", "static int ")}
int main(void){
    GLfloat values[16];raster_pos4f_compat(0.25,0.5,0,1);
    assert(emit_query_state(GL_CURRENT_RASTER_POSITION,V86GL_STATE_FLOAT,values)==1);assert(values[0]==12&&values[1]==12&&values[2]==0.5&&values[3]==1);
    assert(emit_query_state(GL_CURRENT_RASTER_COLOR,V86GL_STATE_FLOAT,values)==1);assert(values[0]==1&&values[1]==0&&values[2]==0.5&&values[3]==1);
    assert(emit_query_state(GL_CURRENT_RASTER_DISTANCE,V86GL_STATE_FLOAT,values)==1&&values[0]==3.5);
    GLuint fbo[]={GL_FRAMEBUFFER_EXT,9};emit_gl_call(GLFN_BIND_FRAMEBUFFER,fbo,sizeof(fbo));window_pos3f_compat(3,4,0.25);
    assert(emit_query_state(GL_CURRENT_RASTER_POSITION,V86GL_STATE_FLOAT,values)==1);assert(values[0]==3&&values[1]==4&&values[2]==0.25&&values[3]==1);
    assert(emit_query_state(GL_CURRENT_RASTER_COLOR,V86GL_STATE_FLOAT,values)==1);assert(values[0]==0.25&&values[1]==0.5&&values[2]==0.75&&values[3]==1);assert(!error);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.error, 0); assert.equal(shadow.cpuBarrier, null); assert.equal(gpu.rasterCaptures, 1, "WindowPos bypasses the active vertex program");
}, async (shadow, gpu) => {
    shadow.setCapabilities(gpu.capabilities()); shadow.setGlslang(new Glslang(fs.readFileSync(path.resolve(hostPath("gl-webgpu"), "../../../../bios/glslang.wasm"))));
    const setup = cases.setup("glsl"); shadow.process(setup.stream.bytes()); gpu.submit(setup.stream.bytes(), {}); await gpu.idle();
    const shift = new GLStream().uniformfv(setup.location.view.getInt32(P.F.QUERY_LOCATION.result, true), 1, 1, [0.25]); shadow.process(shift.bytes()); gpu.submit(shift.bytes(), {});
    gpu.rasterCaptures = 0;
    gpu.captureVertices = function(bytes, view, offset, size, metadata, raster) {
        assert.equal(raster, true); ++this.rasterCaptures;
        const r = P.decodeRecord("CAPTURE_RASTER_VERTEX", view, offset), a = new Float32Array(bytes.buffer.slice(bytes.byteOffset + offset + 8, bytes.byteOffset + offset + size)), p = Array.from(a.subarray(0, 4)), values = new Float32Array(56);
        values.set([p[0] + 0.25, ...p.slice(1), 1, 0, 0.5, 1, 1, 1, 1, 1, ...a.subarray(32, 36), ...p]);
        values.set([0.25, 0.5, 0.75, 1, 3.5], 20); for (let i = 1; i < 8; ++i) values.set(i === 7 ? a.subarray(60, 64) : Array.from(a.subarray(32, 36), n => n + i), 28 + (i - 1) * 4);
        this.trackReadback(Promise.resolve().then(() => metadata.onVertexCapture({ job: r.job, first: 0, values })));
    };
}).then(() => console.log("gl_raster_program_abi_test: actual C RasterPos/WindowPos and typed getters consume completed CPU state in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
