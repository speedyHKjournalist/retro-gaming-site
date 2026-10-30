"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef uint32_t GLuint; typedef int32_t GLsizei; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
${transport}
static void emit_name_array_call(uint16_t fn,GLsizei count,const GLuint* names) { (void)fn;(void)count;(void)names;abort(); }
${body(source, "emit_query_names", "static int ")}
static GLuint g_next_glsl_id=777,shaders[16],programs[16];
static unsigned int shader_count,program_count;
static void* find_program_state(GLuint name,int create) {
    for(unsigned int i=0;i<program_count;i++)if(programs[i]==name)return &programs[i];
    if(!create)return NULL; assert(program_count<16); programs[program_count++]=name; return &programs[program_count-1];
}
static void* find_shader_state(GLuint name,int create,GLenum type) {
    (void)type; for(unsigned int i=0;i<shader_count;i++)if(shaders[i]==name)return &shaders[i];
    if(!create)return NULL; assert(shader_count<16); shaders[shader_count++]=name; return &shaders[shader_count-1];
}
${body(source, "allocate_glsl_name", "static GLuint ")}
${body(source, "glCreateProgram", "GLuint APIENTRY ")}
${body(source, "glCreateShader", "GLuint APIENTRY ")}
int main(void) {
    uint32_t existing[]={1,GL_VERTEX_SHADER}; emit_gl_call(GLFN_CREATE_SHADER,existing,8);
    assert(glCreateProgram()==2); assert(glCreateShader(GL_VERTEX_SHADER)==3);
    assert(glCreateShader(GL_FRAGMENT_SHADER)==4); assert(glCreateProgram()==5);
    assert(g_next_glsl_id==777 && !error);
    assert(glCreateShader(0)==0 && error==GL_INVALID_ENUM); error=0;
    uint32_t shared[]={2,0,0,64,64,2,1}; emit_gl_call(V86GL_CTRL_MAKE_CURRENT,shared,sizeof(shared));
    assert(glCreateProgram()==6 && !error);
    uint32_t private_context[]={3,0,0,64,64,3,3}; emit_gl_call(V86GL_CTRL_MAKE_CURRENT,private_context,sizeof(private_context));
    assert(glCreateProgram()==1 && !error && g_next_glsl_id==777);
    return 0;
}
`;
run(code, shadow => {
    assert.equal(shadow.stats.rejected, 0); assert.equal(shadow.current.group.programs.has(1), true);
    assert.equal(shadow.current.group.glslNames.size, 0);
    const shared = [...shadow.groups.values()].find(g => g.shaders.has(3));
    assert.deepEqual([...shared.shaders.keys()], [1, 3, 4]); assert.deepEqual([...shared.programs.keys()], [2, 5, 6]);
    assert.equal(shared.glslNames.size, 0);
}).then(() => console.log("gl_glsl_cpu_names_abi_test: actual C creators use CPU names across shared/private contexts and keep the legacy counter unchanged in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
