"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef unsigned short GLushort;typedef int GLint,BOOL;typedef float GLfloat;typedef double GLdouble;typedef unsigned char GLboolean;
#define APIENTRY
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
${body(source, "emit_query_typed", "static int ")}
${body(source, "emit_query_state", "static int ")}
${body(source, "emit_index_state", "static void ")}
${["glClearIndex", "glIndexMask", "glIndexf", "glIndexd"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
int main(void){
    GLdouble d;GLfloat f;GLint i;GLboolean b;
    glIndexd(16777217.25);assert(emit_query_state(GL_CURRENT_INDEX,V86GL_STATE_DOUBLE,&d)==1&&d==16777218);
    glIndexf(-0.5f);assert(emit_query_state(GL_CURRENT_INDEX,V86GL_STATE_INTEGER,&i)==1&&i==-1);
    glClearIndex(-3.5f);assert(emit_query_state(GL_INDEX_CLEAR_VALUE,V86GL_STATE_INTEGER,&i)==1&&i==-4);
    glIndexMask(0x80000001u);assert(emit_query_state(GL_INDEX_WRITEMASK,V86GL_STATE_INTEGER,&i)==1&&i==-2147483647);
    assert(emit_query_state(GL_INDEX_WRITEMASK,V86GL_STATE_FLOAT,&f)==1&&f==2147483648.0f);
    assert(emit_query_state(GL_INDEX_WRITEMASK,V86GL_STATE_BOOLEAN,&b)==1&&b==1);assert(!error);return 0;
}
`;
run(code, shadow => { assert.equal(shadow.current.error, 0); assert.equal(shadow.current.state.values[GL.CURRENT_INDEX][0], -0.5); })
    .then(() => console.log("gl_index_state_abi_test: actual C index setters and typed getters preserve precision, negative rounding and unsigned masks in both modes passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });
