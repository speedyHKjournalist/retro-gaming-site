"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum;typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
#define g_error error
static uint32_t g_dma_size,waits;
static BOOL wait_for_host_status(const uint8_t* field,const char* what) { (void)what;waits++;assert(read_u32le(field)!=0);return read_u32le(field)==1; }
static int v86gl_trace_call_allowed(void) { return 0; }
#define v86gl_error(...) ((void)0)
${body(source, "emit_query_error", "static int ")}
${body(source, "glGetError", "GLenum APIENTRY ")}
int main(void) {
    error=GL_INVALID_ENUM;
    assert(glGetError()==GL_INVALID_ENUM && error==GL_INVALID_VALUE);
    assert(glGetError()==GL_INVALID_VALUE && !error);
    assert(glGetError()==GL_NO_ERROR);
    float bad=0;emit_gl_call(GLFN_LINE_WIDTH,&bad,sizeof(bad));
    error=GL_INVALID_VALUE; /* The DLL saw the same CPU query failure. */
    assert(glGetError()==GL_INVALID_VALUE && !error);
    assert(glGetError()==GL_NO_ERROR);
    emit_gl_call(GLFN_LINE_WIDTH,&bad,sizeof(bad));
    assert(glGetError()==GL_INVALID_VALUE);
    assert(glGetError()==GL_NO_ERROR && waits==7);
    return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.error, 0); assert.equal(gpu.current.error, 0);
}, (shadow, gpu) => { gpu.current.error = GL.INVALID_VALUE; })
    .then(() => console.log("gl_error_query_abi_test: actual C GetError waits for fallback, drains duplicate CPU flags and preserves a different pending error in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
