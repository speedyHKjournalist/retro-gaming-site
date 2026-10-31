"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const { hostPath } = require("./host_paths.js");
const caps = require(hostPath("gl-webgpu/gl_caps.js"));
const { GLStream } = require(hostPath("../../../tests/glbridge/gl_stream_builder.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef int BOOL; typedef int GLsizei; typedef char GLchar; typedef unsigned char GLubyte;
#define APIENTRY
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
typedef struct { char gl_strings[6][4096]; } V86GLWGLContext;
static V86GLWGLContext contexts[2]; static unsigned current;
static unsigned GetCurrentThreadId(void) { return 1; }
static V86GLWGLContext* find_thread_current_context(unsigned thread) { assert(thread==1);return &contexts[current]; }
static uint32_t g_dma_size;
static int v86gl_trace_call_allowed(void) { return 0; }
#define v86gl_trace(...) ((void)0)
${body(source, "emit_query_gl_string", "static int ")}
${body(source, "glGetString", "const GLubyte* APIENTRY ")}
int main(void) {
    const GLubyte* vendor=glGetString(GL_VENDOR);
    assert(vendor && !strcmp((const char*)vendor,"VMware, Inc."));
    const GLubyte* renderer=glGetString(GL_RENDERER);
    assert(renderer && !strcmp((const char*)renderer,"SVGA3D; v86 WebGPU bridge"));
    assert(!strcmp((const char*)vendor,"VMware, Inc."));
    assert(strstr((const char*)glGetString(GL_EXTENSIONS),"GL_ARB_shader_objects"));
    assert(!strcmp((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),"1.20 v86gl"));
    assert(glGetString(0xDEADBEEF)==NULL);
    uint32_t mode=GL_POINTS;emit_gl_call(GLFN_BEGIN,&mode,4);
    assert(glGetString(GL_VENDOR)==NULL);
    emit_gl_call(GLFN_END,NULL,0);
    current=1;
    assert(glGetString(GL_VENDOR)!=vendor);
    assert(!strcmp((const char*)vendor,"VMware, Inc."));
    assert(glGetString(GL_PROGRAM_ERROR_STRING_ARB)!=NULL);
    return 0;
}
`;
run(code, shadow => {
    assert.equal(shadow.current.error, GL.INVALID_ENUM);
}, shadow => {
    shadow.setCapabilities(caps.capabilities({}, {}));
    // Harness contexts predate the hint; this query path is still CPU-owned.
    shadow.process(new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1).bytes());
}).then(() => console.log("gl_string_query_abi_test: actual C strings, stable context pointers, CPU invalid-enum/Begin errors in both modes passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });
