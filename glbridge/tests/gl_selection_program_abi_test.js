"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js"), { hostPath, testPath } = require("./host_paths.js");
const P = require(hostPath("gl-webgpu/gl_protocol.js")), { Glslang } = require(hostPath("gl-webgpu/gl_glslang.js")),
    { GLStream } = require(testPath("gl_stream_builder.js")), cases = require(testPath("gl_selection_program_cases.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef int GLint,GLsizei,BOOL;typedef float GLfloat;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define v86gl_trace(...) ((void)0)
static int v86gl_trace_call_allowed(void){return 0;}
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static uint32_t selected[32];static GLfloat feedback[64];
static void copy_guest(void* destination,const void* source,size_t size){
    uintptr_t at=(uintptr_t)destination;
    if(at>=0x1000000&&at<0x1000000+sizeof(selected)){assert(at-0x1000000+size<=sizeof(selected));memcpy((uint8_t*)selected+at-0x1000000,source,size);}
    else if(at>=0x2000000&&at<0x2000000+sizeof(feedback)){assert(at-0x2000000+size<=sizeof(feedback));memcpy((uint8_t*)feedback+at-0x2000000,source,size);}
    else memcpy(destination,source,size);
}
#define CopyMemory copy_guest
${transport}
static uint32_t g_dma_size;
static BOOL wait_for_host_status(const uint8_t* p,const char* name){(void)name;return read_u32le(p)==1;}
${body(source, "query_render_mode", "static BOOL ")}
${body(source, "emit_name_command", "static void ")}
${["glSelectBuffer", "glFeedbackBuffer", "glInitNames", "glPushName", "glBegin", "glEnd", "glVertex3f"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
${body(source, "glRenderMode", "GLint APIENTRY ")}
int main(void){
    memset(selected,0xA5,sizeof(selected));glSelectBuffer(32,(GLuint*)0x1000000);assert(glRenderMode(GL_SELECT)==0);
    glInitNames();glPushName(7);glBegin(GL_POINTS);glVertex3f(0.75,0,0);glEnd();assert(glRenderMode(GL_RENDER)==0);assert(selected[0]==0xA5A5A5A5);
    glFeedbackBuffer(64,GL_4D_COLOR_TEXTURE,(GLfloat*)0x2000000);assert(glRenderMode(GL_FEEDBACK)==0);
    glBegin(GL_POINTS);glVertex3f(0.25,0,0);glEnd();assert(glRenderMode(GL_RENDER)==13);
    GLfloat expected[]={GL_POINT_TOKEN,12,8,0.5,1,1,0,0.5,1,5,5,5,5};assert(!memcmp(feedback,expected,sizeof(expected)));assert(!error);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.state.selection.mode, GL.RENDER); assert.equal(shadow.selectionJobs.size, 0); assert.equal(gpu.stats.draws, 0);
}, async (shadow, gpu) => {
    shadow.setCapabilities(gpu.capabilities()); shadow.setGlslang(new Glslang(fs.readFileSync(path.resolve(hostPath("gl-webgpu"), "../../../../bios/glslang.wasm"))));
    const setup = cases.setup(); const submit = async stream => { shadow.process(stream.bytes()); gpu.submit(stream.bytes(), {}); await gpu.idle(); };
    await submit(setup.stream);
    await submit(new GLStream().uniformfv(setup.location.view.getInt32(P.F.QUERY_LOCATION.result, true), 1, 1, [2]).call("VIEWPORT", 0, 0, 16, 16));
    // Only GPU arithmetic is substituted in this native ABI test. The real
    // compute shader and its resources run in v86's browser acceptance page.
    gpu.captureVertices = function(bytes, view, offset, size, metadata) {
        const r = P.decodeRecord("CAPTURE_VERTICES", view, offset), values = new Float32Array(r.count * 20);
        for (let i = 0; i < r.count; ++i) {
            const at = offset + 16 + i * 256, p = Array.from({ length: 4 }, (_, k) => view.getFloat32(at + k * 4, true));
            values.set([p[0] * 2, ...p.slice(1), 1, 0, 0.5, 1, 0.25, 0.5, 0.75, 1, 5, 5, 5, 5, ...p], i * 20);
        }
        this.trackReadback(Promise.resolve().then(() => metadata.onVertexCapture({ job: r.job, first: r.first, values })));
    };
}).then(() => console.log("gl_selection_program_abi_test: actual C APIs consume asynchronous programmable selection/feedback through CPU results in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
