#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const begin = source.indexOf("static GLuint allocate_glsl_name("), end = source.indexOf("void APIENTRY glDeleteProgram(", begin);
assert.ok(begin >= 0 && end > begin);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-glsl-names-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "gl_protocol.h"
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_INVALID_ENUM 0x500
#define GL_OUT_OF_MEMORY 0x505
typedef uint32_t GLuint;
typedef uint32_t GLenum;
typedef int32_t GLsizei;
static int emit_query_names(uint32_t kind, uint16_t opcode, GLsizei count, GLuint* names) {
    (void)kind; (void)opcode; (void)count; (void)names; return 0; /* legacy host */
}
static GLuint g_next_glsl_id = 1;
static GLuint shaders[16], programs[16];
static unsigned int shader_count, program_count, emitted, error;
static void* find_program_state(GLuint name, int create) {
    for (unsigned int i=0; i<program_count; ++i) if (programs[i] == name) return &programs[i];
    if (!create) return NULL;
    assert(program_count < 16); programs[program_count++] = name; return &programs[program_count-1];
}
static void* find_shader_state(GLuint name, int create, GLenum type) {
    (void)type;
    for (unsigned int i=0; i<shader_count; ++i) if (shaders[i] == name) return &shaders[i];
    if (!create) return NULL;
    assert(shader_count < 16); shaders[shader_count++] = name; return &shaders[shader_count-1];
}
static void v86gl_set_error(GLenum e) { error = e; }
static void emit_gl_call(uint16_t opcode, const void* payload, uint32_t size) {
    const uint32_t* words = payload;
    assert(words[0]);
    assert((opcode == GLFN_CREATE_SHADER && size == 8) || (opcode == GLFN_CREATE_PROGRAM && size == 4));
    ++emitted;
}
${source.slice(begin, end)}
int main(void) {
    assert(glCreateProgram() == 1);
    assert(glCreateShader(GL_VERTEX_SHADER) == 2);
    assert(glCreateShader(GL_FRAGMENT_SHADER) == 3);
    assert(glCreateProgram() == 4);
    assert(glCreateShader(0) == 0 && error == GL_INVALID_ENUM);
    assert(emitted == 4);
    g_next_glsl_id = UINT32_MAX;
    assert(glCreateShader(GL_VERTEX_SHADER) == UINT32_MAX);
    assert(glCreateProgram() == 5); /* skip zero and both kinds after wrap */
    assert(emitted == 6);
    return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_glsl_names_abi_test: actual C allocator/creators share names, handle invalid types and skip live names after wrap (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
