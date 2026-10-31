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
#include "../tests/gl_api_fixture.h"
typedef unsigned int GLenum;typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport.replace(/static void v86gl_set_error[^\n]+\n/, "").replace("wire_size, error", "wire_size")}
${body(source, "v86gl_set_error", "static void ")}

static uint32_t g_dma_size,waits;
static BOOL wait_for_host_status(const uint8_t* field,const char* what) { (void)what;waits++;assert(read_u32le(field)!=0);return read_u32le(field)==1; }
static int v86gl_trace_call_allowed(void) { return 0; }
#define v86gl_error(...) ((void)0)
${body(source, "emit_query_error", "static int ")}
${body(source, "glGetError", "GLenum APIENTRY ")}
int main(void) {
    v86gl_set_error(GL_INVALID_ENUM);
    assert(glGetError()==GL_INVALID_ENUM);
    assert(glGetError()==GL_NO_ERROR);
    assert(glGetError()==GL_NO_ERROR);
    float bad=0;emit_gl_call(GLFN_LINE_WIDTH,&bad,sizeof(bad));
    v86gl_set_error(GL_INVALID_VALUE); /* Duplicate rejection does not create a second flag. */
    assert(glGetError()==GL_INVALID_VALUE);
    assert(glGetError()==GL_NO_ERROR);
    emit_gl_call(GLFN_LINE_WIDTH,&bad,sizeof(bad));
    assert(glGetError()==GL_INVALID_VALUE);
    assert(glGetError()==GL_NO_ERROR && waits==7);
    return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.error, 0); assert.equal(gpu.current.error, GL.INVALID_OPERATION);
}, (shadow, gpu) => { gpu.current.error = GL.INVALID_OPERATION; })
    .then(() => console.log("gl_error_query_abi_test: actual C GetError consumes CPU errors and NO_ERROR before rendering, keeps rejection ordering and ignores renderer flags in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
