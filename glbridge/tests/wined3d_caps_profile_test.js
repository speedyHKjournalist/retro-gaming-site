"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const proxyPath = path.join(
    __dirname, "..", "openglproxy", "opengl32_proxy.c");
const headerPath = path.join(
    __dirname, "..", "openglproxy", "v86gl_ioctl.h");
const samplePath = path.join(
    __dirname, "..", "sample", "d3d8_triangle_test.c");
const proxy = fs.readFileSync(proxyPath, "utf8");
const header = fs.readFileSync(headerPath, "utf8");
const sample = fs.readFileSync(samplePath, "utf8");

function cString(name) {
    const expression = new RegExp(
        "static const char " + name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") +
        "\\[\\]\\s*=([\\s\\S]*?);");
    const match = proxy.match(expression);
    assert.ok(match, "missing C string array " + name);
    return Array.from(match[1].matchAll(/"([^"]*)"/g), part => part[1]).join("");
}

const legacy = cString("g_gl_extensions_wined3d_gl15");
const gl21 = cString("g_gl_extensions_gl21_base");
const shaderBackends = [
    "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader",
    "GL_ARB_fragment_shader",
    "GL_ARB_shading_language_100",
    "GL_ARB_vertex_program",
    "GL_ARB_fragment_program",
];

for (const extension of shaderBackends) {
    assert.equal(legacy.includes(extension), false,
        "fixed-function WineD3D profile must hide " + extension);
    assert.equal(gl21.includes(extension), true,
        "OpenGL 2.1 profile must retain " + extension);
}
assert.equal(legacy.includes("GL_EXT_framebuffer_object"), false);
assert.equal(gl21.includes("GL_EXT_framebuffer_object"), false,
    "the shared GL 2.1 base must remain no-FBO");

assert.ok(legacy.includes("GL_ARB_multitexture"));
assert.ok(legacy.includes("GL_EXT_texture_compression_s3tc"));
assert.match(proxy,
    /GetModuleHandleA\("wined3d\.dll"\)[\s\S]*V86GL_CAPS_PROFILE_WINED3D_GL15/);
assert.match(proxy, /GetEnvironmentVariableA\("V86GL_CAPS_PROFILE"/);
assert.match(proxy, /"1\.5 v86gl \(WineD3D backbuffer profile\)"/);
assert.match(proxy, /"gl21-no-fbo"/);
assert.match(proxy, /"gl21-fbo-ffp"/);
assert.match(proxy,
    /profile == V86GL_CAPS_PROFILE_GL21 \|\|[\s\S]*V86GL_CAPS_PROFILE_GL21_FBO_FFP[\s\S]*append_gl_extension\("GL_EXT_framebuffer_object"\)/);
assert.match(proxy, /append_gl_extension\("GL_EXT_framebuffer_blit"\)/);
assert.match(proxy, /GLFN_BLIT_FRAMEBUFFER = 217/);
assert.match(proxy, /void APIENTRY glBlitFramebufferEXT\(/);
assert.match(proxy,
    /profile == V86GL_CAPS_PROFILE_GL21_FBO_FFP[\s\S]*remove_gl_extension\("GL_ARB_vertex_program"\)[\s\S]*remove_gl_extension\("GL_ARB_fragment_program"\)/);
assert.match(proxy,
    /caps_profile_is_gl21\(current_caps_profile\(\)\)[\s\S]*"2\.1 v86gl/);
assert.match(proxy,
    /case GL_VENDOR:[\s\S]*"VMware, Inc\."[\s\S]*case GL_RENDERER:[\s\S]*"SVGA3D; v86 WebGPU bridge"/);
assert.equal(proxy.includes('case GL_VENDOR:     return (const GLubyte*)"v86"'),
    false, "unknown vendor must not trigger WineD3D's NVIDIA/GeForce FX fallback");
assert.ok(gl21.includes("GL_ARB_texture_non_power_of_two"),
    "the WebGPU profile must retain the NPOT capability selected by SVGA3D");
for (const extension of [
    "GL_ARB_texture_compression", "GL_ARB_multisample",
    "GL_ARB_texture_border_clamp", "GL_EXT_generate_mipmap",
    "GL_SGI_color_matrix",
]) {
    assert.ok(gl21.split(" ").includes(extension),
        "GLView 1.3/1.4 capability is missing " + extension);
}
assert.match(proxy,
    /case GL_COLOR:[\s\S]*return g_color_matrix_stack\[g_color_matrix_stack_depth\]/,
    "GL_SGI_color_matrix needs a real matrix stack");
assert.match(proxy,
    /pname >= GL_POST_COLOR_MATRIX_RED_SCALE_SGI[\s\S]*g_post_color_matrix_scale/,
    "GL_SGI_color_matrix post-transfer state must be retained in the proxy");
assert.equal(gl21.includes("GL_ARB_imaging"), false,
    "do not claim the unimplemented full imaging subset");

for (const stage of [
    "23 VertexBuffer::Release",
    "24 Device::Release",
    "25 Direct3D8::Release",
    "26 teardown complete",
    "27 ExitProcess",
]) {
    assert.ok(sample.includes(stage), "missing teardown checkpoint " + stage);
}
assert.match(sample,
    /g_vertex_buffer = NULL;[\s\S]*IDirect3DVertexBuffer8_Release\(vertex_buffer\)/);
assert.match(sample,
    /g_device = NULL;[\s\S]*IDirect3DDevice8_Release\(device\)/);
assert.match(proxy,
    /arb_program_parameter_limit\(GLenum target\)[\s\S]*GL_VERTEX_PROGRAM_ARB \? 96u : 28u/);
assert.equal(proxy.includes("v86glTraceCheckpoint"), false,
    "test-only PCI checkpoints must stay out of the production proxy");
assert.equal(sample.includes("TRACE EXPORT MISSING"), false,
    "triangle title must report only D3D8 results");

assert.equal(proxy.includes("V86GL_PRESENT_STATUS_"), false,
    "proxy must not depend on a synthetic PCI Present-completion status");
assert.equal(header.includes("V86GL_PRESENT_STATUS_"), false,
    "PCI ABI must leave the descriptor completion words reserved");

console.log("wined3d_caps_profile_test: ok");
