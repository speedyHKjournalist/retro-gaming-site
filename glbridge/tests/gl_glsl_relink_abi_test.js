#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const begin = source.indexOf("void APIENTRY glShaderSource("), end = source.indexOf("void APIENTRY glUseProgram(", begin);
assert.ok(begin >= 0 && end > begin);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-glsl-relink-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
#define APIENTRY
#define FALSE 0
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_INVALID_VALUE 0x501
#define GL_OUT_OF_MEMORY 0x505
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define V86GL_DIAG_ENABLED 0
#define CopyMemory memcpy
typedef uint32_t GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
typedef size_t SIZE_T;
typedef struct { GLchar* source; GLsizei source_length; int compiled; } ShaderObjectState;
typedef struct { int linked, has_executable, validated; } ProgramObjectState;
static ShaderObjectState shader;
static ProgramObjectState program;
static unsigned int error, invalidated, emitted;
static GLint query_status;
static ShaderObjectState* find_shader_state(GLuint name, int create, int type) {
    (void)create; (void)type; return name == 1 ? &shader : NULL;
}
static ProgramObjectState* find_program_state(GLuint name, int create) {
    (void)create; return name == 5 ? &program : NULL;
}
static void v86gl_set_error(unsigned int value) { error = value; }
static unsigned int gl_string_length(const char* text) { return (unsigned int)strlen(text); }
static void* GetProcessHeap(void) { return NULL; }
static void* HeapAlloc(void* heap, int flags, size_t size) { (void)heap; (void)flags; return malloc(size); }
static void HeapFree(void* heap, int flags, void* data) { (void)heap; (void)flags; free(data); }
static uint8_t* alloc_payload(uint32_t size) { return malloc(size); }
static void emit_gl_call(uint16_t opcode, const void* payload, uint32_t size) {
    assert(payload && size >= 4);
    assert(opcode == GLFN_SHADER_SOURCE || opcode == GLFN_COMPILE_SHADER || opcode == GLFN_LINK_PROGRAM || opcode == GLFN_VALIDATE_PROGRAM);
    ++emitted;
}
static int emit_query_object_iv(uint32_t kind, GLuint name, uint32_t pname, GLint* status) {
    (void)name; (void)pname; assert(kind == V86GL_OBJECT_KIND_SHADER || kind == V86GL_OBJECT_KIND_PROGRAM);
    *status = query_status; return 1;
}
static void invalidate_program_locations(GLuint name) { assert(name == 5); ++invalidated; }
${source.slice(begin, end)}
int main(void) {
    const char* text = "bad source";
    shader.compiled = GL_TRUE;
    glShaderSource(1, 1, &text, NULL);
    assert(shader.compiled && shader.source_length == 10 && !strcmp(shader.source, text));
    query_status = 0; glCompileShader(1); assert(!shader.compiled);
    program.linked = program.has_executable = program.validated = GL_TRUE;
    glLinkProgram(5);
    assert(!program.linked && program.has_executable && !program.validated && !invalidated);
    query_status = 1; glLinkProgram(5);
    assert(program.linked && program.has_executable && !program.validated && invalidated == 1);
    glValidateProgram(5); assert(program.validated);
    query_status = 0; glLinkProgram(5);
    assert(!program.linked && !program.validated && program.has_executable && invalidated == 1);
    assert(!error && emitted == 6);
    free(shader.source); return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_glsl_relink_abi_test: actual C source/compile/link functions retain compiled code and invalidate locations only after successful relinks (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
