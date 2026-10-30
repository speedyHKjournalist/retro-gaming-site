#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const names = ["glIsShader", "glIsProgram", "glIsTexture", "glIsRenderbufferEXT", "glIsFramebufferEXT"];
function body(name) {
    const start = source.indexOf("GLboolean APIENTRY " + name + "("); assert.ok(start >= 0);
    let end = source.indexOf("{", start) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(start, end);
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-object-query-"));
try {
    const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "gl_protocol.h"
#define APIENTRY
#define FALSE 0
#define GL_FALSE 0
#define GL_TRUE 1
typedef uint32_t GLuint;
typedef unsigned char GLboolean;
typedef struct { int target; } TextureObjectState;
typedef struct { int used; } ProgramObjectState;
typedef struct { int used; } ShaderObjectState;
static TextureObjectState texture = {1};
static ProgramObjectState program;
static ShaderObjectState shader;
static unsigned int expected_kind, expected_name, asked;
static int query_result;
static GLboolean query_value;
static int emit_query_parameter(unsigned int family, unsigned int kind, unsigned int name, unsigned int type, void* out) {
    assert(family == V86GL_PARAMETER_IS_OBJECT && kind == expected_kind && name == expected_name && type == V86GL_STATE_BOOLEAN);
    ++asked; if (query_result > 0) *(GLboolean*)out = query_value; return query_result;
}
static ProgramObjectState* find_program_state(GLuint n, int create) { (void)n; (void)create; return &program; }
static ShaderObjectState* find_shader_state(GLuint n, int create, int type) { (void)n; (void)create; (void)type; return &shader; }
static void* find_framebuffer_state(GLuint n, int create) { (void)n; (void)create; return &program; }
static void* find_renderbuffer_state(GLuint n, int create) { (void)n; (void)create; return &program; }
static TextureObjectState* lookup_texture_state(GLuint n) { (void)n; return &texture; }
${names.map(body).join("\n")}
int main(void) {
    GLboolean (*getters[])(GLuint) = {glIsShader, glIsProgram, glIsTexture, glIsRenderbufferEXT, glIsFramebufferEXT};
    unsigned int kinds[] = {V86GL_OBJECT_KIND_SHADER, V86GL_OBJECT_KIND_PROGRAM, V86GL_OBJECT_KIND_TEXTURE,
        V86GL_OBJECT_KIND_RENDERBUFFER, V86GL_OBJECT_KIND_FRAMEBUFFER};
    for (unsigned int i = 0; i < 5; ++i) {
        expected_kind = kinds[i]; expected_name = 7;
        query_result = 1; query_value = 0; assert(getters[i](7) == 0);
        query_value = 1; assert(getters[i](7) == 1);
        query_result = -1; assert(getters[i](7) == 0); /* failed answers must not fall back */
        query_result = 0; assert(getters[i](7) == 1); /* older unsupported hosts only */
        expected_name = 0; query_result = 1; query_value = 0; assert(getters[i](0) == 0);
    }
    assert(asked == 25); return 0;
}
`;
    fs.writeFileSync(path.join(dir, "test.c"), code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
        path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
    execFileSync(path.join(dir, "test"));
    console.log("gl_object_query_abi_test: five actual C IsObject getters use CPU answers, including false, failure and zero names (passed)");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
