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
static uint32_t g_dma_size;
${body(source,"emit_query_program_iv_arb","static int ")}
${["glMatrixMode", "glLoadIdentity", "glLoadMatrixd", "glMultMatrixd", "glTranslatef", "glRotatef", "glScalef", "glFrustum", "glOrtho", "glPushMatrix", "glPopMatrix", "glBindProgramARB", "glGetProgramivARB"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
static void scalar(GLenum pname,GLint expected){GLint i=0;assert(emit_query_state(pname,V86GL_STATE_INTEGER,&i)==1&&i==expected);}
int main(void){
    GLdouble m[16],out[16];for(int i=0;i<16;i++)m[i]=i+1;
    scalar(GL_MAX_PROGRAM_MATRICES_ARB,8);scalar(GL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB,2);
    glMatrixMode(GL_MATRIX0_ARB);glLoadMatrixd(m);glPushMatrix();glTranslatef(1,2,3);scalar(GL_CURRENT_MATRIX_STACK_DEPTH_ARB,2);
    assert(emit_query_state(GL_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[12]==51&&out[15]==72);
    glPopMatrix();assert(emit_query_state(GL_TRANSPOSE_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[1]==5&&out[12]==4);
    glMatrixMode(GL_MATRIX0_ARB+7);glLoadIdentity();glScalef(2,3,4);glTranslatef(1,2,3);glRotatef(0,1,0,0);
    assert(emit_query_state(GL_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[0]==2&&out[12]==2&&out[13]==6&&out[14]==12);
    glLoadIdentity();GLdouble scale[16]={2,0,0,0,0,3,0,0,0,0,4,0,0,0,0,1};glMultMatrixd(scale);
    assert(emit_query_state(GL_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[5]==3);
    glLoadIdentity();glOrtho(-1,1,-1,1,1,3);assert(emit_query_state(GL_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[10]==-1&&out[14]==-2);
    glLoadIdentity();glFrustum(-1,1,-1,1,1,3);assert(emit_query_state(GL_CURRENT_MATRIX_ARB,V86GL_STATE_DOUBLE,out)==1&&out[10]==-2&&out[11]==-1&&out[14]==-3);
    glBindProgramARB(GL_VERTEX_PROGRAM_ARB,71);GLint bound=-1;glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_BINDING_ARB,&bound);assert(bound==71);
    glPopMatrix();scalar(GL_CURRENT_MATRIX_STACK_DEPTH_ARB,1);assert(!error);return 0;
}
`;
run(code, shadow => { assert.equal(shadow.current.error, GL.STACK_UNDERFLOW); }, (shadow, gpu) => shadow.setCapabilities(gpu.capabilities()))
    .then(() => console.log("gl_program_matrix_abi_test: actual C matrix modes, loads, transforms, stack errors and typed queries use independent CPU program matrices in both modes passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });
