"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
typedef unsigned int GLenum,GLuint;typedef unsigned short GLushort;typedef int GLint,BOOL;typedef float GLfloat;typedef double GLdouble;typedef unsigned char GLboolean;
#define APIENTRY
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
${body(source, "emit_query_typed", "static int ")}
${body(source, "emit_query_state", "static int ")}
${["func", "mask", "op"].map(name => body(source, "emit_stencil_" + name + "_separate", "static void ")).join("\n")}
${["glActiveStencilFaceEXT", "glStencilFunc", "glStencilMask", "glStencilOp", "glStencilFuncSeparate", "glStencilMaskSeparate", "glStencilOpSeparate"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
static void check(GLenum pname,GLint expected){GLint i=0;assert(emit_query_state(pname,V86GL_STATE_INTEGER,&i)==1&&i==expected);}
int main(void){
    glStencilFuncSeparate(GL_BACK,GL_LESS,5,15);glStencilMaskSeparate(GL_BACK,51);glStencilOpSeparate(GL_BACK,GL_REPLACE,GL_INCR,GL_DECR);
    glActiveStencilFaceEXT(GL_BACK);glStencilFunc(GL_GREATER,7,240);glStencilMask(119);glStencilOp(GL_KEEP,GL_DECR_WRAP,GL_INCR_WRAP);
    check(GL_ACTIVE_STENCIL_FACE_EXT,GL_BACK);check(GL_STENCIL_TEST_TWO_SIDE_EXT,0);
    check(GL_STENCIL_FUNC,GL_GREATER);check(GL_STENCIL_REF,7);check(GL_STENCIL_VALUE_MASK,240);check(GL_STENCIL_WRITEMASK,119);
    check(GL_STENCIL_PASS_DEPTH_FAIL,GL_DECR_WRAP);check(GL_STENCIL_BACK_FUNC,GL_LESS);check(GL_STENCIL_BACK_REF,5);
    check(GL_STENCIL_BACK_VALUE_MASK,15);check(GL_STENCIL_BACK_WRITEMASK,51);check(GL_STENCIL_BACK_PASS_DEPTH_FAIL,GL_INCR);
    glActiveStencilFaceEXT(GL_FRONT);glStencilFunc(GL_EQUAL,-7,~0u);glStencilMask(85);glStencilOp(GL_INVERT,GL_ZERO,GL_KEEP);
    check(GL_STENCIL_FUNC,GL_EQUAL);check(GL_STENCIL_REF,0);check(GL_STENCIL_BACK_REF,0);check(GL_STENCIL_BACK_WRITEMASK,85);
    glActiveStencilFaceEXT(GL_BACK);check(GL_STENCIL_FUNC,GL_GREATER);check(GL_STENCIL_REF,7);check(GL_STENCIL_WRITEMASK,119);
    assert(!error);return 0;
}
`;
run(code, shadow => { assert.equal(shadow.current.error, 0); })
    .then(() => console.log("gl_stencil_ext_abi_test: actual C ordinary/Separate stencil encoders and typed queries preserve all three independent banks in both modes passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });
