"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const start = source.indexOf("void APIENTRY glGetBufferParameterivARB("), end = source.indexOf("void APIENTRY glGetBufferParameteriv(", start);
assert.ok(start >= 0 && end > start);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-buffer-queries-"));
try {
    fs.writeFileSync(path.join(dir, "test.c"), `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "gl_protocol.h"
#define APIENTRY
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_BUFFER_SIZE_ARB 0x8764
#define GL_BUFFER_USAGE_ARB 0x8765
#define GL_BUFFER_ACCESS_ARB 0x88BB
#define GL_BUFFER_MAPPED_ARB 0x88BC
#define GL_INVALID_ENUM 0x500
typedef uint32_t GLenum;
typedef int32_t GLint;
typedef struct { uint32_t size,usage,access; int mapped; } BufferObjectState;
static BufferObjectState local = {3,4,5,1};
static unsigned int asked, local_queries, errors;
static GLenum expected_target, expected_pname;
static int result, answer;
static BufferObjectState* bound_buffer_state(GLenum target) { assert(target == expected_target); ++local_queries; return &local; }
static void v86gl_set_error(GLenum error) { assert(error == GL_INVALID_ENUM); ++errors; }
static int emit_query_parameter(unsigned int family, GLenum target, GLenum pname, unsigned int kind, void* out) {
    assert(family == V86GL_PARAMETER_BUFFER && kind == V86GL_STATE_INTEGER);
    assert(target == expected_target && pname == expected_pname); ++asked;
    if(result > 0) *(GLint*)out = answer; return result;
}
${source.slice(start, end)}
int main(void) {
    GLint out; expected_target = 0x8892;
    for(unsigned int i=0;i<2;i++) {
        expected_pname = i ? GL_BUFFER_USAGE_ARB : GL_BUFFER_SIZE_ARB;
        result=1; answer=i?0x88E8:8192; out=-1;
        glGetBufferParameterivARB(expected_target,expected_pname,&out); assert(out == answer && local_queries == 0);
        result=-1; out=12345; glGetBufferParameterivARB(expected_target,expected_pname,&out);
        assert(out == 12345 && local_queries == 0);
    }
    result=0; expected_pname=GL_BUFFER_MAPPED_ARB; out=0;
    glGetBufferParameterivARB(expected_target,expected_pname,&out); assert(out == 1 && local_queries == 1);
    glGetBufferParameterivARB(expected_target,expected_pname,NULL); assert(asked == 5 && errors == 0);
    return 0;
}`);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"),
        path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_buffer_queries_abi_test: actual C buffer metadata getter uses CPU answers and preserves failed destinations (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
