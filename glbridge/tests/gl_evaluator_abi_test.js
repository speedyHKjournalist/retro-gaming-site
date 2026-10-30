"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"),
    section = source.slice(source.indexOf("static unsigned int evaluator_components("), source.indexOf("static void raster_pos4f_compat(", source.indexOf("static unsigned int evaluator_components("))).replaceAll("__declspec(dllexport)", "");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef unsigned short GLushort;typedef int GLint,GLsizei,BOOL;typedef float GLfloat;typedef double GLdouble;typedef unsigned char GLboolean;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
${body(source, "emit_query_typed", "static int ")}
${body(source, "emit_query_state", "static int ")}
${section}
int main(void){
    GLdouble points[]={-0.5,0,0,99,0,1,0,99,0.5,0,0,99},got[256];GLint ints[256];GLfloat floats[256];
    glGetMapdv(GL_MAP2_VERTEX_4,GL_COEFF,got);assert(got[0]==0&&got[1]==0&&got[2]==0&&got[3]==1);
    glMap1d(GL_MAP1_VERTEX_3,-2,2,4,3,points);points[0]=99;glGetMapdv(GL_MAP1_VERTEX_3,GL_COEFF,got);
    assert(got[0]==-0.5&&got[3]==0&&got[4]==1&&got[6]==0.5);glGetMapiv(GL_MAP1_VERTEX_3,GL_ORDER,ints);assert(ints[0]==3);
    GLfloat color[]={-1.5,-0.5,0.5,1.5};glMap1f(GL_MAP1_COLOR_4,0,1,4,1,color);glGetMapiv(GL_MAP1_COLOR_4,GL_COEFF,ints);
    assert(ints[0]==-2&&ints[1]==-1&&ints[2]==1&&ints[3]==2);glGetMapfv(GL_MAP1_COLOR_4,GL_COEFF,floats);assert(!memcmp(floats,color,sizeof(color)));
    GLdouble surface[]={-0.25,-0.375,0,-0.25,0.375,0,0.25,-0.375,0,0.25,0.375,0};glMap2d(GL_MAP2_VERTEX_3,0,1,6,2,0,1,3,2,surface);
    glGetMapdv(GL_MAP2_VERTEX_3,GL_COEFF,got);assert(!memcmp(got,surface,sizeof(surface)));glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,ints);assert(ints[0]==2&&ints[1]==2);
    GLfloat padded[]={1,2,3,99,4,5,6};glMap2f(GL_MAP2_NORMAL,0,1,7,1,0,1,4,2,padded);glGetMapfv(GL_MAP2_NORMAL,GL_COEFF,floats);assert(floats[0]==1&&floats[3]==4&&floats[5]==6);
    GLdouble largest[256];for(int i=0;i<256;i++)largest[i]=i/255.0;glMap2d(GL_MAP2_TEXTURE_COORD_4,0,1,32,8,0,1,4,8,largest);
    glGetMapdv(GL_MAP2_TEXTURE_COORD_4,GL_COEFF,got);assert(!memcmp(got,largest,sizeof(largest)));
    glMapGrid1f(3,-2,2);glMapGrid2d(2,0,1,4,-1,1);assert(emit_query_state(GL_MAP1_GRID_DOMAIN,V86GL_STATE_DOUBLE,got)==1&&got[0]==-2&&got[1]==2);
    assert(emit_query_state(GL_MAP2_GRID_SEGMENTS,V86GL_STATE_INTEGER,ints)==1&&ints[0]==2&&ints[1]==4);
    GLuint cap=GL_MAP2_VERTEX_3;emit_gl_call(GLFN_ENABLE,&cap,4);GLuint mode=GL_POINTS;emit_gl_call(GLFN_BEGIN,&mode,4);
    glEvalCoord2f(0.5,0.5);GLdouble uv[]={0.25,0.75};glEvalCoord2dv(uv);glEvalPoint2(1,2);emit_gl_call(GLFN_END,NULL,0);glEvalMesh2(GL_FILL,0,2,0,4);
    assert(!error);glGetMapdv(GL_MAP1_VERTEX_3,GL_TEXTURE_2D,got);assert(error==GL_INVALID_ENUM);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.error, GL.INVALID_ENUM); assert.equal(shadow.current.state.evaluators[GL.MAP1_VERTEX_3].points[0], -0.5);
    assert.ok(gpu.stats.draws >= 2, "evaluated vertices and mesh reach the renderer");
}).then(() => console.log("gl_evaluator_abi_test: actual C maps, strided float/double capture, typed queries, maximum surface, grids and evaluated draws in both modes passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });
