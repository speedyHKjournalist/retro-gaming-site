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

const { hostPath: capsPath } = require("./host_paths.js");
const caps = require(capsPath("gl-webgpu/gl_caps.js"));
const { GL: enums } = require(capsPath("gl-webgpu/gl_constants.js"));
const features = caps.freeze(caps.capabilities({}, { bc: true }));
const legacy = caps.profileString(features, 1, enums.EXTENSIONS);
const gl21 = caps.profileString(features, 5, enums.EXTENSIONS);
for (const name of ["GL_ARB_shader_objects", "GL_ARB_vertex_shader", "GL_ARB_fragment_shader", "GL_ARB_shading_language_100"]) {
    assert.equal(legacy.split(" ").includes(name), false);
    assert.equal(gl21.split(" ").includes(name), true);
}
assert.equal(legacy.includes("GL_EXT_framebuffer_object"), false);
assert.equal(legacy.includes("GL_EXT_framebuffer_blit"), false);
assert.ok(legacy.includes("GL_ARB_multitexture"));
assert.ok(legacy.includes("GL_EXT_texture_compression_s3tc"));
assert.match(proxy, /GetModuleHandleA\("wined3d\.dll"\)[\s\S]*V86GL_CAPS_PROFILE_WINED3D_GL15/);
assert.match(proxy, /GetEnvironmentVariableA\("V86GL_CAPS_PROFILE"/);
assert.match(proxy, /"gl21-no-fbo"/);
assert.match(proxy, /"gl21-fbo-ffp"/);
assert.equal(caps.profileString(features, 1, enums.VERSION), "1.5 v86gl (WineD3D backbuffer profile)");
assert.equal(caps.profileString(features, 5, enums.VENDOR), "VMware, Inc.");
assert.equal(caps.profileString(features, 5, enums.RENDERER), "SVGA3D; v86 WebGPU bridge");
assert.ok(gl21.includes("GL_ARB_texture_non_power_of_two"));
for (const extension of ["GL_ARB_texture_compression", "GL_ARB_multisample", "GL_ARB_texture_border_clamp", "GL_EXT_generate_mipmap", "GL_SGI_color_matrix"])
    assert.ok(gl21.split(" ").includes(extension));
assert.match(proxy, /emit_gl_call\(GLFN_BLIT_FRAMEBUFFER,/);
assert.match(proxy, /void APIENTRY glBlitFramebufferEXT\(/);
// Matrix storage moved out of the DLL. Check the promised color stack in
// the CPU state machine instead of requiring the retired C cache.
const { hostPath, testPath } = require("./host_paths.js"), { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_state.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js")), matrices = new GLShadow();
matrices.process(new GLStream().makeCurrent(1, 0, 0, 16, 16, 1, 1).call("MATRIX_MODE", GL.COLOR)
    .call("SCALEF", 2, 3, 4).call("PUSH_MATRIX").call("TRANSLATEF", 1, 2, 3).bytes());
assert.equal(matrices.stateValue(GL.COLOR_MATRIX_SGI)[0][12], 2);
matrices.process(new GLStream().call("POP_MATRIX").bytes());
assert.equal(matrices.stateValue(GL.COLOR_MATRIX_SGI)[0][12], 0);
assert.equal(matrices.stateValue(GL.COLOR_MATRIX_SGI)[0][5], 3);
require(hostPath("gl-webgpu/gl_shadow_misc.js"));
matrices.process(new GLStream().call("PIXEL_TRANSFERF",GL.POST_COLOR_MATRIX_RED_SCALE_SGI,2.5).bytes());
assert.equal(matrices.stateValue(GL.POST_COLOR_MATRIX_RED_SCALE_SGI)[0][0],2.5);
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
