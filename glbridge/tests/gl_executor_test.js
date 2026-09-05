#!/usr/bin/env node
// Unit tests for glbridge/gl-webgpu/gl_executor.js -- the OpenGL state machine
// and draw path, driven by real command records and a fake GPUDevice.
//
// Every fixture here is encoded through gl_stream_builder.js, which uses the
// same signature table the executor decodes with, so a layout mistake shows up
// as a decode mismatch rather than as a plausible-looking wrong picture. What
// the assertions check is the *decision*: which pipeline state a GL state
// produced, which vertex layout, which bind groups, and what a synchronous
// query answered.

"use strict";

const assert = require("assert");
const { createFakeHost } = require("./gl_fake_gpu.js");
const { GLStream, GL, GLFN } = require("./gl_stream_builder.js");
const executorModule = require("../gl-webgpu/gl_executor.js");

let passed = 0;
const failures = [];

function test(name, fn) {
    try {
        fn();
        ++passed;
    } catch (error) {
        failures.push([name, error]);
    }
}

/*
 * Two results in this path cannot be answered inside the batch that asked for
 * them -- glReadPixels and occlusion queries both wait on a buffer mapping.
 * Their tests therefore have to await the same microtask the real bridge does,
 * which is the whole point: a synchronous assertion would pass against an
 * executor that never completes them at all.
 */
const asyncTests = [];
function asyncTest(name, fn) {
    asyncTests.push([name, fn]);
}

function newExecutor(options) {
    const { host, log } = createFakeHost();
    const executor = new executorModule.GLWebGPUExecutor(null,
        Object.assign({ host }, options || {}));
    executor.initializeSync = () => {
        // initialize() is async only because a real adapter request is; the
        // fake resolves immediately, so the tests drive it synchronously.
        executor.device = host.device;
        executor.deviceFeatures = host.deviceFeatures;
        executor.limits = host.limits;
        executor.uniformCapacity = 1 << 20;
        executor.vertexCapacity = 1 << 20;
        executor.uniformRing = host.device.createBuffer({ size: executor.uniformCapacity });
        executor.uniformStaging = new Uint8Array(executor.uniformCapacity);
        executor.vertexRing = host.device.createBuffer({ size: executor.vertexCapacity });
        executor.vertexStaging = new Uint8Array(executor.vertexCapacity);
        executor.fallbackTexture = host.device.createTexture({
            size: { width: 1, height: 1 }, format: "rgba8unorm" });
        executor.fallbackView = executor.fallbackTexture.createView();
        executor.fallbackSampler = host.device.createSampler({});
        executor.fallbackComparisonSampler = host.device.createSampler({});
        executor.readyPromise = Promise.resolve(executor);
    };
    executor.initializeSync();
    return { executor, log, host };
}

function run(executor, stream) {
    executor.submit(stream.bytes(), {});
}

/* ---- the state machine ---- */

test("a context is created on demand and keeps GL's defaults", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(7, 0, 0, 320, 240));
    const state = executor.current;
    assert.ok(state, "makeCurrent creates a context");
    assert.strictEqual(state.depthFunc, GL.LESS);
    assert.strictEqual(state.depthMask, true);
    assert.strictEqual(state.frontFace, GL.CCW);
    assert.strictEqual(state.cullFace, GL.BACK);
    assert.strictEqual(state.shadeModel, GL.SMOOTH);
    assert.strictEqual(state.blend.srcRGB, GL.ONE);
    assert.strictEqual(state.blend.dstRGB, GL.ZERO);
    assert.strictEqual(state.lights[0].diffuse[0], 1,
        "GL_LIGHT0 starts with a white diffuse and the rest black");
    assert.strictEqual(state.lights[1].diffuse[0], 0);
    assert.ok(state.polygonStipple.every(value => value === 0xff),
        "the default 32x32 polygon stipple is all ones");
    const ambient = [...state.material.front.ambient];
    assert.ok(Math.abs(ambient[0] - 0.2) < 1e-6 && ambient[3] === 1,
        "the default front material ambient is 0.2, 0.2, 0.2, 1");
});

test("WGL context ids separate state while share-group ids share objects", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(77, 0, 0, 64, 64, 1, 11)
        .names(GLFN.GEN_TEXTURES, [91]));
    const first = executor.current;

    run(executor, new GLStream().makeCurrent(77, 0, 0, 64, 64, 2, 22));
    const second = executor.current;
    assert.notStrictEqual(second, first,
        "two HGLRCs on one HWND must not alias context state");
    assert.notStrictEqual(second.shareGroup, first.shareGroup);
    assert.ok(!second.shareGroup.textures.has(91));

    run(executor, new GLStream().makeCurrent(88, 0, 0, 64, 64, 3, 11));
    const shared = executor.current;
    assert.notStrictEqual(shared, first);
    assert.strictEqual(shared.shareGroup, first.shareGroup,
        "wglShareLists contexts use the same object namespace");
    assert.ok(shared.shareGroup.textures.has(91));
});

test("late teardown deletes and ARB unbinds are silent no-ops", () => {
    const { executor } = newExecutor();
    let warnings = 0;
    const originalWarn = console.warn;
    console.warn = () => { ++warnings; };
    try {
        run(executor, new GLStream()
            .names(GLFN.DELETE_TEXTURES, [91])
            .call("BIND_PROGRAM_ARB", GL.VERTEX_PROGRAM_ARB, 0)
            .call("BIND_PROGRAM_ARB", GL.FRAGMENT_PROGRAM_ARB, 0));
        assert.strictEqual(warnings, 0,
            "process-detach cleanup must not produce a no-context warning");
        run(executor, new GLStream()
            .call("BIND_PROGRAM_ARB", GL.VERTEX_PROGRAM_ARB, 7));
        assert.strictEqual(warnings, 1,
            "a non-zero context-free bind still exposes a real ordering bug");
    } finally {
        console.warn = originalWarn;
    }
});

test("the matrix stack multiplies in GL's order", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("MATRIX_MODE", GL.MODELVIEW)
        .call("LOAD_IDENTITY")
        .call("TRANSLATEF", 1, 2, 3)
        .call("SCALEF", 2, 2, 2);
    run(executor, stream);
    const m = executor.topOf(GL.MODELVIEW);
    // glTranslate then glScale means the scale is applied first to a vertex.
    assert.strictEqual(m[0], 2);
    assert.strictEqual(m[12], 1);
    assert.strictEqual(m[13], 2);
    assert.strictEqual(m[14], 3);
});

test("SGI color matrix transforms pixel rectangles before post scale and bias", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("MATRIX_MODE", GL.COLOR)
        .call("LOAD_IDENTITY")
        .call("SCALEF", 0.5, 1, 1)
        .call("PIXEL_TRANSFERF", GL.POST_COLOR_MATRIX_RED_BIAS_SGI, 0.25));
    const rgba = new Uint8Array([200, 64, 0, 255]);
    executor.applyPixelTransfer(rgba);
    assert.deepStrictEqual([...rgba], [164, 64, 0, 255]);
});

test("glPushMatrix past the stack depth raises GL_STACK_OVERFLOW", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("MATRIX_MODE", GL.PROJECTION);
    for (let i = 0; i < 8; ++i) stream.call("PUSH_MATRIX");
    run(executor, stream);
    assert.strictEqual(executor.current.error, GL.STACK_OVERFLOW);
});

test("glEnable routes per-unit capabilities to the active unit", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0 + 3)
        .call("ENABLE", GL.TEXTURE_2D)
        .call("ENABLE", GL.TEXTURE_GEN_S));
    const units = executor.current.textureUnits;
    assert.ok(units[3].enabledTargets.has(GL.TEXTURE_2D));
    assert.ok(!units[0].enabledTargets.has(GL.TEXTURE_2D),
        "the capability belongs to the active unit alone");
    assert.strictEqual(units[3].texGen[0].enabled, true);
});

test("glLight POSITION is captured in eye space at call time", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("MATRIX_MODE", GL.MODELVIEW)
        .call("LOAD_IDENTITY")
        .call("TRANSLATEF", 10, 0, 0)
        .call("LIGHTFV", GL.LIGHT0, GL.POSITION, 4, 0, 0, 0, 1)
        .call("TRANSLATEF", 100, 0, 0);
    run(executor, stream);
    const light = executor.current.lights[0];
    assert.strictEqual(light.eyePosition[0], 10,
        "a later modelview change must not move an already-positioned light");
});

/* ---- synchronous queries ---- */

test("glGetError reports the first error and clears it", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0 + 99);
    const first = stream.queryError();
    const second = stream.queryError();
    run(executor, stream);
    assert.strictEqual(first.view.getUint32(0, true), executorModule.SYNC_STATUS_OK);
    assert.strictEqual(first.view.getUint32(4, true), GL.INVALID_ENUM);
    assert.strictEqual(second.view.getUint32(4, true), GL.NO_ERROR,
        "the error queue is emptied by a read");
});

test("glGetIntegerv is answered without touching the GPU", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
    const answer = stream.queryInteger(GL.MAX_TEXTURE_UNITS);
    const sampleBuffers = stream.queryInteger(GL.SAMPLE_BUFFERS);
    const samples = stream.queryInteger(GL.SAMPLES);
    const before = log.submits.length;
    run(executor, stream);
    assert.strictEqual(answer.view.getUint32(4, true), executorModule.SYNC_STATUS_OK);
    assert.strictEqual(answer.view.getUint32(8, true), 8);
    assert.strictEqual(sampleBuffers.view.getUint32(8, true), 0);
    assert.strictEqual(samples.view.getUint32(8, true), 0,
        "a non-multisample pixel format reports zero GL samples");
    assert.strictEqual(log.submits.length, before,
        "answering a state query must not submit GPU work");
});

test("the extension string only advertises what the adapter backs", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64));
    const withBC = executor.extensionString();
    assert.ok(withBC.indexOf("GL_EXT_texture_compression_s3tc") >= 0);
    executor.deviceFeatures = { bc: false, float32Filterable: false };
    const withoutBC = executor.extensionString();
    assert.ok(withoutBC.indexOf("GL_EXT_texture_compression_s3tc") < 0,
        "S3TC is not advertised when the adapter has no BC formats");
    assert.ok(withoutBC.indexOf("GL_ARB_multitexture") >= 0);
    for (const extension of [
        "GL_ARB_texture_compression", "GL_ARB_multisample",
        "GL_ARB_texture_border_clamp", "GL_EXT_generate_mipmap",
        "GL_SGI_color_matrix",
    ]) {
        assert.ok(withoutBC.split(" ").includes(extension),
            extension + " is part of the bridge-guaranteed 1.3/1.4 profile");
    }
});

test("ARB sample coverage reaches the WebGPU pipeline mask", () => {
    const { executor, log } = newExecutor();
    run(executor, immediateTriangle(
        new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("ENABLE", GL.SAMPLE_COVERAGE)
        .call("SAMPLE_COVERAGE", 0, 0)));
    assert.strictEqual(log.draws[0].pipeline.descriptor.multisample.mask, 0,
        "zero sample coverage suppresses the one-sample framebuffer");
});

test("glGetString reports GL 2.1 and GLSL 1.20", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
    const version = stream.queryString(GL.VERSION, 64);
    const glsl = stream.queryString(GL.SHADING_LANGUAGE_VERSION, 64);
    run(executor, stream);
    const read = answer => {
        const payload = answer.bytes;
        let text = "";
        for (let i = 16; i < payload.length && payload[i]; ++i)
            text += String.fromCharCode(payload[i]);
        return text;
    };
    assert.ok(read(version).startsWith("2.1"), read(version));
    assert.strictEqual(read(glsl), "1.20");
});

/* ---- clears and the frame ---- */

test("glClear before any draw becomes a load operation", () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("CLEAR_COLOR", 0.25, 0.5, 0.75, 1)
        .call("CLEAR", GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT));
    assert.strictEqual(log.passes.length, 1);
    const attachment = log.passes[0].descriptor.colorAttachments[0];
    assert.strictEqual(attachment.loadOp, "clear");
    assert.strictEqual(attachment.clearValue.r, 0.25);
    assert.strictEqual(
        log.passes[0].descriptor.depthStencilAttachment.depthLoadOp, "clear");
});

test("scissored and masked clears are rendered instead of widening the clear", () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("ENABLE", GL.SCISSOR_TEST)
        .call("SCISSOR", 10, 20, 30, 40)
        .call("COLOR_MASK", 1, 0, 1, 0)
        .call("CLEAR_COLOR", 0.2, 0.4, 0.6, 1)
        .call("CLEAR", GL.COLOR_BUFFER_BIT));
    const pass = log.passes.find(entry =>
        entry.descriptor.label === "GL masked/scissored clear");
    assert.ok(pass, "the restricted clear uses its dedicated draw pass");
    assert.deepStrictEqual(pass.scissor, [10, 20, 30, 40]);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].pipeline.descriptor.fragment.targets[0].writeMask,
        1 | 4);
});

test("a depth-only FBO omits stencil operations from its render pass", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_FRAMEBUFFERS, [1])
        .names(GLFN.GEN_RENDERBUFFERS, [1])
        .call("BIND_RENDERBUFFER", GL.RENDERBUFFER_EXT, 1)
        .call("RENDERBUFFER_STORAGE", GL.RENDERBUFFER_EXT,
            GL.DEPTH_COMPONENT24, 64, 64)
        .call("BIND_FRAMEBUFFER", GL.FRAMEBUFFER_EXT, 1)
        .call("FRAMEBUFFER_RENDERBUFFER", GL.FRAMEBUFFER_EXT,
            GL.DEPTH_ATTACHMENT, GL.RENDERBUFFER_EXT, 1)
        .call("DRAW_BUFFER", GL.NONE)
        .call("CLEAR", GL.DEPTH_BUFFER_BIT);
    immediateTriangle(stream);
    run(executor, stream);

    const pass = log.passes.find(entry =>
        entry.descriptor.label === "GL pass");
    const attachment = pass.descriptor.depthStencilAttachment;
    assert.strictEqual(attachment.view.texture.descriptor.format,
        "depth32float");
    assert.strictEqual(attachment.depthLoadOp, "clear");
    assert.ok(!("stencilLoadOp" in attachment));
    assert.ok(!("stencilStoreOp" in attachment));
    assert.ok(!("stencilClearValue" in attachment));
});

test("a masked depth clear also omits stencil operations on a depth-only FBO", () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_FRAMEBUFFERS, [1])
        .names(GLFN.GEN_RENDERBUFFERS, [1])
        .call("BIND_RENDERBUFFER", GL.RENDERBUFFER_EXT, 1)
        .call("RENDERBUFFER_STORAGE", GL.RENDERBUFFER_EXT,
            GL.DEPTH_COMPONENT24, 64, 64)
        .call("BIND_FRAMEBUFFER", GL.FRAMEBUFFER_EXT, 1)
        .call("FRAMEBUFFER_RENDERBUFFER", GL.FRAMEBUFFER_EXT,
            GL.DEPTH_ATTACHMENT, GL.RENDERBUFFER_EXT, 1)
        .call("DRAW_BUFFER", GL.NONE)
        .call("ENABLE", GL.SCISSOR_TEST)
        .call("SCISSOR", 2, 2, 32, 32)
        .call("CLEAR", GL.DEPTH_BUFFER_BIT));

    const pass = log.passes.find(entry =>
        entry.descriptor.label === "GL masked/scissored clear");
    const attachment = pass.descriptor.depthStencilAttachment;
    assert.ok(!("stencilLoadOp" in attachment));
    assert.ok(!("stencilStoreOp" in attachment));
});

/* ---- drawing ---- */

function immediateTriangle(stream) {
    return stream.call("BEGIN", GL.TRIANGLES)
        .call("COLOR4F", 1, 0, 0, 1).call("VERTEX3F", -1, -1, 0)
        .call("COLOR4F", 0, 1, 0, 1).call("VERTEX3F", 1, -1, 0)
        .call("COLOR4F", 0, 0, 1, 1).call("VERTEX3F", 0, 1, 0)
        .call("END");
}

test("an immediate-mode triangle produces one draw with one vertex buffer", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240);
    immediateTriangle(stream);
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].count, 3);
    const pipeline = log.draws[0].pipeline;
    assert.strictEqual(pipeline.descriptor.vertex.buffers.length, 1);
    const attributes = pipeline.descriptor.vertex.buffers[0].attributes;
    // Position at location 0 and colour at 3 -- the historical slots every
    // engine of this era assumes.
    assert.deepStrictEqual(attributes.map(a => a.shaderLocation).sort((a, b) => a - b),
        [0, 3]);
    assert.strictEqual(executor.stats.immediateBatches, 1);
});

test("immediate mode ignores stale client-array attributes", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("ENABLE_CLIENT_STATE", GL.NORMAL_ARRAY);
    immediateTriangle(stream);
    run(executor, stream);

    const descriptor = log.draws[0].pipeline.descriptor.vertex;
    assert.ok(!descriptor.module.code.includes("@location(2) normal"),
        "an enabled normal array is irrelevant to glBegin/glEnd vertices");
    assert.deepStrictEqual(descriptor.buffers[0].attributes.map(attribute =>
        attribute.shaderLocation).sort((a, b) => a - b), [0, 3],
        "the shader and immediate buffer agree on their attribute slots");
});

test("GL_QUADS expands to two triangles with GL's provoking vertex first", () => {
    const expanded = executorModule.expandIndices(GL.QUADS, 4, 0);
    assert.strictEqual(expanded.length, 6);
    // GL takes a quad's flat colour from its fourth vertex, so vertex 3 leads
    // both triangles -- WGSL's @interpolate(flat) uses the first.
    assert.strictEqual(expanded[0], 3);
    assert.strictEqual(expanded[3], 3);
    const fan = executorModule.expandIndices(GL.TRIANGLE_FAN, 5, 0);
    assert.strictEqual(fan.length, 9);
    assert.strictEqual(fan[0], 1, "a fan's provoking vertex is not the centre");
    const loop = executorModule.expandIndices(GL.LINE_LOOP, 4, 0);
    assert.deepStrictEqual([...loop], [0, 1, 2, 3, 0]);
});

test("a GL_QUADS draw is issued as an indexed triangle list", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("BEGIN", GL.QUADS);
    for (const [x, y] of [[-1, -1], [1, -1], [1, 1], [-1, 1]])
        stream.call("VERTEX3F", x, y, 0);
    stream.call("END");
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].indexed, true);
    assert.strictEqual(log.draws[0].count, 6);
    assert.strictEqual(log.draws[0].pipeline.descriptor.primitive.topology,
        "triangle-list");
});

test("glPolygonMode line and point rasterize triangle edges and vertices", () => {
    const line = newExecutor();
    let stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("POLYGON_MODE", GL.FRONT_AND_BACK, GL.LINE);
    immediateTriangle(stream);
    run(line.executor, stream);
    assert.strictEqual(line.log.draws[0].indexed, true);
    assert.strictEqual(line.log.draws[0].count, 6);
    assert.strictEqual(line.log.draws[0].pipeline.descriptor.primitive.topology,
        "line-list");

    const point = newExecutor();
    stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("POLYGON_MODE", GL.FRONT_AND_BACK, GL.POINT);
    immediateTriangle(stream);
    run(point.executor, stream);
    assert.strictEqual(point.log.draws[0].count, 3);
    assert.strictEqual(point.log.draws[0].pipeline.descriptor.primitive.topology,
        "point-list");
});

test("culling both faces drops polygon draws", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("ENABLE", GL.CULL_FACE)
        .call("CULL_FACE", GL.FRONT_AND_BACK);
    immediateTriangle(stream);
    run(executor, stream);
    assert.strictEqual(log.draws.length, 0);
});

test("wide points and point sprites expand to instanced quads", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("POINT_SIZE", 8)
        .call("BEGIN", GL.POINTS)
        .call("VERTEX3F", 0, 0, 0)
        .call("END");
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].count, 6,
        "one logical point is rendered as a six-vertex quad");
    assert.strictEqual(log.draws[0].instances, 1);
    const buffers = log.draws[0].pipeline.descriptor.vertex.buffers;
    assert.strictEqual(buffers[0].stepMode, "instance");
    assert.strictEqual(buffers[buffers.length - 1].stepMode, "vertex");
    assert.strictEqual(
        buffers[buffers.length - 1].attributes[0].shaderLocation,
        require("../gl-webgpu/gl_shader_translator.js").POINT_CORNER_LOCATION);
    assert.strictEqual(log.draws[0].pipeline.descriptor.primitive.topology,
        "triangle-list");
});

test("colour logic XOR snapshots the attachment and disables blending", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("ENABLE", GL.BLEND)
        .call("ENABLE", GL.COLOR_LOGIC_OP)
        .call("LOGIC_OP", GL.XOR);
    immediateTriangle(stream);
    run(executor, stream);
    assert.ok(log.encoders.some(encoder =>
        encoder.copies.some(copy => copy[0] === "t2t")),
    "the destination colour attachment is copied before drawing");
    const draw = log.draws[0];
    assert.equal(draw.pipeline.descriptor.fragment.targets[0].blend, undefined,
        "logic operations take precedence over blending");
    assert.match(draw.pipeline.descriptor.fragment.module.code, /s \^ d/);
    assert.ok(draw.pass.calls.some(call =>
        call[0] === "bindGroup" && call[1] === 3));
});

test("point-sprite lower-left origin reaches the generated shader", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("POINT_PARAMETERI", GL.POINT_SPRITE_COORD_ORIGIN, GL.LOWER_LEFT)
        .call("POINT_SIZE", 4)
        .call("BEGIN", GL.POINTS)
        .call("VERTEX3F", 0, 0, 0)
        .call("END");
    run(executor, stream);
    assert.match(log.draws[0].pipeline.descriptor.vertex.module.code,
        /vin\.corner\.y \* 0\.5 \+ 0\.5/);
});

test("the clip-space flip is paired with reversed winding", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("ENABLE", GL.CULL_FACE)
        .call("FRONT_FACE", GL.CCW)
        .call("CULL_FACE", GL.BACK);
    immediateTriangle(stream);
    run(executor, stream);
    const primitive = log.draws[0].pipeline.descriptor.primitive;
    assert.strictEqual(primitive.frontFace, "cw",
        "GL_CCW is reported as cw because the vertex shader negates clip Y");
    assert.strictEqual(primitive.cullMode, "back");
    assert.ok(log.draws[0].pipeline.descriptor.vertex.module.code
        .indexOf("clip.y = -clip.y;") >= 0, "the flip is in the shader");
});

test("blend and depth state reach the pipeline descriptor", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("ENABLE", GL.BLEND)
        .call("BLEND_FUNC_SEPARATE", GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA,
            GL.ONE, GL.ZERO)
        .call("BLEND_EQUATION_SEPARATE", GL.FUNC_ADD, GL.FUNC_SUBTRACT)
        .call("ENABLE", GL.DEPTH_TEST)
        .call("DEPTH_FUNC", GL.LEQUAL)
        .call("DEPTH_MASK", 0)
        .call("COLOR_MASK", 1, 1, 0, 1);
    immediateTriangle(stream);
    run(executor, stream);
    const descriptor = log.draws[0].pipeline.descriptor;
    const target = descriptor.fragment.targets[0];
    assert.strictEqual(target.blend.color.srcFactor, "src-alpha");
    assert.strictEqual(target.blend.color.dstFactor, "one-minus-src-alpha");
    assert.strictEqual(target.blend.alpha.operation, "subtract");
    assert.strictEqual(target.writeMask, 1 | 2 | 8);
    assert.strictEqual(descriptor.depthStencil.depthCompare, "less-equal");
    assert.strictEqual(descriptor.depthStencil.depthWriteEnabled, false);
});

test("changing a state that changes the shader produces a second pipeline", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240);
    immediateTriangle(stream);
    stream.call("ENABLE", GL.FOG).call("FOGI", GL.FOG_MODE, GL.LINEAR);
    immediateTriangle(stream);
    run(executor, stream);
    assert.strictEqual(log.draws.length, 2);
    assert.notStrictEqual(log.draws[0].pipeline, log.draws[1].pipeline,
        "enabling fog must reach the fixed-function signature");
    assert.ok(log.draws[1].pipeline.descriptor.fragment.module.code
        .indexOf("fogFactor") >= 0);
});

test("a client-array draw packs every enabled array into one buffer", () => {
    const { executor, log } = newExecutor();
    const positions = new Float32Array([-1, -1, 0, 1, -1, 0, 0, 1, 0]);
    const colors = new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255]);
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, stride: 12,
                      data: new Uint8Array(positions.buffer) },
            color: { size: 4, type: GL.UNSIGNED_BYTE, stride: 4, data: colors },
        });
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].count, 3);
    const buffers = log.draws[0].pipeline.descriptor.vertex.buffers;
    assert.strictEqual(buffers.length, 1);
    assert.strictEqual(buffers[0].arrayStride, (4 + 4) * 4,
        "position is always a vec4 and colour is widened to float");
});

test("Warcraft unlit indexed draws ignore a stale normal array", () => {
    const { executor, log } = newExecutor();
    const positions = new Float32Array([
        -1, -1, 0, 1, -1, 0, 0, 1, 0,
    ]);
    const normals = new Float32Array([
        0, 0, 1, 0, 0, 1, 0, 0, 1,
    ]);
    const indices = new Uint16Array([0, 1, 2]);
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("ENABLE_CLIENT_STATE", GL.VERTEX_ARRAY)
        .call("ENABLE_CLIENT_STATE", GL.NORMAL_ARRAY)
        .drawElements(GL.TRIANGLES,
            new Uint8Array(indices.buffer), GL.UNSIGNED_SHORT, {
                vertex: { size: 3, type: GL.FLOAT, stride: 12,
                    data: new Uint8Array(positions.buffer) },
                normal: { size: 3, type: GL.FLOAT, stride: 12,
                    data: new Uint8Array(normals.buffer) },
            });
    run(executor, stream);

    assert.strictEqual(log.draws.length, 1);
    const vertex = log.draws[0].pipeline.descriptor.vertex;
    assert.ok(!vertex.module.code.includes("@location(2) normal"),
        "an unlit pass must not make VSIn consume the stale normal array");
    assert.deepStrictEqual(vertex.buffers[0].attributes.map(attribute =>
        attribute.shaderLocation).sort((a, b) => a - b), [0, 3],
        "the fixed shader and the packed layout expose the same slots");
});

/* ---- textures ---- */

test("a LUMINANCE upload is expanded to (l, l, l, 1)", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [5])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 5)
        .texImage2D(GL.TEXTURE_2D, 0, GL.LUMINANCE, 2, 1, GL.LUMINANCE,
            GL.UNSIGNED_BYTE, new Uint8Array([64, 200]));
    run(executor, stream);
    const texture = executor.current.shareGroup.textures.get(5);
    const pixels = texture.levels[0][0].pixels;
    assert.deepStrictEqual([...pixels.subarray(0, 8)],
        [64, 64, 64, 255, 200, 200, 200, 255]);
    assert.strictEqual(texture.baseFormat, "LUMINANCE");
});

test("an ALPHA upload leaves RGB at zero", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [6])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 6)
        .texImage2D(GL.TEXTURE_2D, 0, GL.ALPHA, 1, 1, GL.ALPHA,
            GL.UNSIGNED_BYTE, new Uint8Array([137])));
    const texture = executor.current.shareGroup.textures.get(6);
    assert.deepStrictEqual([...texture.levels[0][0].pixels], [0, 0, 0, 137]);
});

test("BGRA and packed 5-6-5 sources are converted", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [7, 8])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 7)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGBA, 1, 1, GL.BGRA,
            GL.UNSIGNED_BYTE, new Uint8Array([1, 2, 3, 4]))
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 8)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGB, 1, 1, GL.RGB,
            GL.UNSIGNED_SHORT_5_6_5, new Uint8Array([0x00, 0xf8]));
    run(executor, stream);
    const bgra = executor.current.shareGroup.textures.get(7);
    assert.deepStrictEqual([...bgra.levels[0][0].pixels], [3, 2, 1, 4]);
    const packed = executor.current.shareGroup.textures.get(8);
    // 0xf800 is full red in 5-6-5, and GL widens by bit replication.
    assert.deepStrictEqual([...packed.levels[0][0].pixels], [255, 0, 0, 255]);
});

test("an incomplete texture samples as opaque black", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [9])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 9)
        .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
            GL.LINEAR_MIPMAP_LINEAR)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGBA, 4, 4, GL.RGBA, GL.UNSIGNED_BYTE,
            new Uint8Array(4 * 4 * 4)));
    const texture = executor.current.shareGroup.textures.get(9);
    assert.strictEqual(executor.textureIsComplete(texture), false,
        "a mipmapped filter with only level 0 is incomplete");
});

test("glDrawPixels uploads a pixel rectangle and draws it at the raster position", () => {
    const { executor, log } = newExecutor();
    const pixels = new Uint8Array([
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    ]);
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("WINDOW_POS3F", 10, 12, 0.5)
        .drawPixels(2, 2, GL.RGBA, GL.UNSIGNED_BYTE, pixels);
    run(executor, stream);
    assert.strictEqual(log.textureWrites.length, 1);
    assert.strictEqual(log.textureWrites[0].byteLength, 16);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.draws[0].count, 6);
    assert.strictEqual(executor.stats.refusals, 0);
});

test("glBitmap expands bits with the current colour and advances raster position", () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("WINDOW_POS3F", 4, 5, 0)
        .call("COLOR4F", 1, 0.5, 0, 1)
        .bitmap(8, 1, 0, 0, 3, -2, new Uint8Array([0x81, 0, 0, 0]));
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(executor.current.current.rasterPos[0], 7);
    assert.strictEqual(executor.current.current.rasterPos[1], 3);
    assert.strictEqual(executor.stats.refusals, 0);
});

test("glGenerateMipmap builds the whole chain", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [10])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 10)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGBA, 4, 4, GL.RGBA, GL.UNSIGNED_BYTE,
            new Uint8Array(4 * 4 * 4).fill(128))
        .call("GENERATE_MIPMAP", GL.TEXTURE_2D));
    const texture = executor.current.shareGroup.textures.get(10);
    assert.strictEqual(texture.levels[0].length, 3);
    assert.strictEqual(texture.levels[0][2].width, 1);
    assert.strictEqual(texture.levels[0][1].pixels[0], 128,
        "a box filter over a constant image is that constant");
});

test("DXT1 decodes deterministically when the adapter has no BC", () => {
    // Two-colour block: colour0 red, colour1 blue, all texels index 0.
    const block = new Uint8Array([0x00, 0xf8, 0x1f, 0x00, 0, 0, 0, 0]);
    const rgba = executorModule.decodeDXT(1, block, 4, 4);
    assert.strictEqual(rgba.length, 4 * 4 * 4);
    assert.deepStrictEqual([...rgba.subarray(0, 4)], [255, 0, 0, 255]);
});

test("native BC textures omit render-attachment usage", () => {
    const { executor, log } = newExecutor();
    const blocks = new Uint8Array(16).fill(0x5a);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [227])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 227)
        .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
            GL.LINEAR)
        .compressedTexImage2D(GL.TEXTURE_2D, 0,
            GL.COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, blocks));
    const texture = executor.current.shareGroup.textures.get(227);
    assert.deepStrictEqual([...texture.levels[0][0].pixels], [...blocks],
        "the fixed 2D wire header must not consume the DXT payload");
    const gpuTexture = executor.ensureTextureUploaded(texture);
    assert.ok(gpuTexture, "the BC3 texture is created successfully");
    assert.strictEqual(gpuTexture.descriptor.format, "bc3-rgba-unorm");
    assert.strictEqual(gpuTexture.descriptor.usage & 0x10, 0,
        "RENDER_ATTACHMENT is illegal for block-compressed formats");
    assert.ok(gpuTexture.descriptor.usage & 0x04,
        "the compressed texture remains sampleable");
    assert.strictEqual(log.textureWrites.length, 1,
        "the authored DXT block is uploaded normally");
});

test("compressed 2D subimages use the guest's fixed z/depth wire fields", () => {
    const { executor } = newExecutor();
    const initial = new Uint8Array(8).fill(1);
    const replacement = new Uint8Array(8).fill(7);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [228])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 228)
        .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
            GL.LINEAR)
        .compressedTexImage2D(GL.TEXTURE_2D, 0,
            GL.COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, initial)
        .compressedTexSubImage2D(GL.TEXTURE_2D, 0, 0, 0, 4, 4,
            GL.COMPRESSED_RGBA_S3TC_DXT1_EXT, replacement));
    const texture = executor.current.shareGroup.textures.get(228);
    assert.deepStrictEqual([...texture.levels[0][0].pixels], [...replacement]);
});

test("native BC tail mips upload using their complete physical block", () => {
    for (const [name, size] of [[229, 2], [230, 1]]) {
        const { executor, log } = newExecutor();
        const block = new Uint8Array(8).fill(size);
        run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
            .names(GLFN.GEN_TEXTURES, [name])
            .call("BIND_TEXTURE", GL.TEXTURE_2D, name)
            .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
                GL.LINEAR)
            .compressedTexImage2D(GL.TEXTURE_2D, 0,
                GL.COMPRESSED_RGBA_S3TC_DXT1_EXT, size, size, block));
        const texture = executor.current.shareGroup.textures.get(name);
        executor.ensureTextureUploaded(texture);
        const write = log.textureWrites.at(-1);
        assert.deepStrictEqual(write.size,
            { width: 4, height: 4, depthOrArrayLayers: 1 },
            size + "x" + size + " still occupies one DXT block");
        assert.strictEqual(write.byteLength, 8);
    }
});

test("glTexImage with an S3TC internal format stores ordinary pixels as RGBA8", () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_TEXTURES, [43])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 43)
        .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
            GL.LINEAR)
        .texImage2D(GL.TEXTURE_2D, 0,
            GL.COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4,
            GL.RGBA, GL.UNSIGNED_BYTE, new Uint8Array(4 * 4 * 4).fill(127)));
    const texture = executor.current.shareGroup.textures.get(43);
    const level = texture.levels[0][0];
    assert.strictEqual(level.compressed, false,
        "glTexImage input consists of texels, not pre-encoded BC blocks");
    assert.strictEqual(level.gpuFormat, "rgba8unorm");
    const gpuTexture = executor.ensureTextureUploaded(texture);
    assert.strictEqual(gpuTexture.descriptor.format, "rgba8unorm");
    assert.ok(gpuTexture.descriptor.usage & 0x10,
        "the compatibility allocation can receive environment-map rendering");
    assert.strictEqual(log.textureWrites.at(-1).byteLength, 4 * 4 * 4,
        "all RGBA source texels are uploaded");
});

/* ---- programs ---- */

const VS = "attribute vec4 vvertex;\nuniform mat4 mvp;\nvarying vec2 tc;\n" +
    "void main(void) { gl_Position = mvp * vvertex; tc = vvertex.xy; }";
const FS = "uniform sampler2D tex0;\nuniform vec4 tint;\nvarying vec2 tc;\n" +
    "void main(void) { gl_FragColor = texture2D(tex0, tc) * tint; }";

function linkedProgram(executor) {
    const stream = new GLStream().makeCurrent(1, 0, 0, 320, 240)
        .call("CREATE_PROGRAM", 1)
        .call("CREATE_SHADER", 10, GL.VERTEX_SHADER)
        .call("CREATE_SHADER", 11, GL.FRAGMENT_SHADER)
        .shaderSource(10, VS)
        .shaderSource(11, FS)
        .call("COMPILE_SHADER", 10)
        .call("COMPILE_SHADER", 11)
        .call("ATTACH_SHADER", 1, 10)
        .call("ATTACH_SHADER", 1, 11)
        .call("LINK_PROGRAM", 1)
        .call("USE_PROGRAM", 1);
    run(executor, stream);
    return executor.current.shareGroup.programs.get(1);
}

test("a GLSL program links and reports its reflection", () => {
    const { executor } = newExecutor();
    const program = linkedProgram(executor);
    assert.strictEqual(program.linked, true, program.log);
    const reflection = program.link.reflection;
    assert.ok(reflection.attributes.some(a => a.name === "vvertex"));
    assert.ok(reflection.uniforms.some(u => u.name === "mvp"));
    assert.ok(reflection.samplers.some(s => s.name === "tex0"));
});

test("glGetUniformLocation and glGetAttribLocation answer synchronously", () => {
    const { executor } = newExecutor();
    linkedProgram(executor);
    const stream = new GLStream();
    // V86GL_LOCATION_KIND_UNIFORM is 1 and _ATTRIB is 2, which is the order
    // openglproxy sends and the opposite of what this test used to assume.
    const uniform = stream.queryLocation(1, 1, "tint", 32);
    const attribute = stream.queryLocation(2, 1, "vvertex", 32);
    const missing = stream.queryLocation(1, 1, "nosuch", 32);
    run(executor, stream);
    const status = a => a.view.getUint32(12, true);
    const location = a => a.view.getInt32(16, true);
    assert.strictEqual(status(uniform), executorModule.SYNC_STATUS_OK);
    assert.ok(location(uniform) >= 0);
    assert.ok(location(attribute) >= 0);
    assert.strictEqual(location(missing), -1);
});

test("uniform locations do not alias across GLSL programs", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
    for (const [program, vertex, fragment] of [[1, 10, 11], [2, 20, 21]]) {
        stream.call("CREATE_PROGRAM", program)
            .call("CREATE_SHADER", vertex, GL.VERTEX_SHADER)
            .call("CREATE_SHADER", fragment, GL.FRAGMENT_SHADER)
            .shaderSource(vertex, VS)
            .shaderSource(fragment, FS)
            .call("COMPILE_SHADER", vertex)
            .call("COMPILE_SHADER", fragment)
            .call("ATTACH_SHADER", program, vertex)
            .call("ATTACH_SHADER", program, fragment)
            .call("LINK_PROGRAM", program);
    }
    run(executor, stream);

    const first = executor.current.shareGroup.programs.get(1);
    const second = executor.current.shareGroup.programs.get(2);
    const firstTint = first.uniformByName.get("tint");
    const secondTint = second.uniformByName.get("tint");
    assert.notStrictEqual(firstTint.location, secondTint.location,
        "the guest proxy indexes locations globally, not by current program");

    run(executor, new GLStream().call("USE_PROGRAM", 2)
        .uniformfv(secondTint.location, 4, 1, [0.1, 0.2, 0.3, 0.4]));
    const at = secondTint.offsetBytes >> 2;
    assert.ok(Math.abs(second.uniformFloats[at] - 0.1) < 1e-6);
    assert.strictEqual(first.uniformFloats[firstTint.offsetBytes >> 2], 0,
        "updating program B must not land in program A's uniform storage");
});

test("uniform locations also remain unique across non-shared contexts", () => {
    const { executor } = newExecutor();
    const link = (context, shareGroup, vertex, fragment) => {
        const stream = new GLStream().makeCurrent(context, 0, 0, 64, 64,
            context, shareGroup)
            .call("CREATE_PROGRAM", 1)
            .call("CREATE_SHADER", vertex, GL.VERTEX_SHADER)
            .call("CREATE_SHADER", fragment, GL.FRAGMENT_SHADER)
            .shaderSource(vertex, VS)
            .shaderSource(fragment, FS)
            .call("COMPILE_SHADER", vertex)
            .call("COMPILE_SHADER", fragment)
            .call("ATTACH_SHADER", 1, vertex)
            .call("ATTACH_SHADER", 1, fragment)
            .call("LINK_PROGRAM", 1);
        run(executor, stream);
        return executor.current.shareGroup.programs.get(1);
    };
    const first = link(1, 11, 10, 11);
    const second = link(2, 22, 20, 21);
    assert.notStrictEqual(first.uniformByName.get("tint").location,
        second.uniformByName.get("tint").location,
        "the guest location table spans every HGLRC in the process");
});

test("glUniform4fv lands where the reflection says it does", () => {
    const { executor } = newExecutor();
    const program = linkedProgram(executor);
    const tint = program.uniformByName.get("tint");
    run(executor, new GLStream().uniformfv(tint.location, 4, 1,
        [0.25, 0.5, 0.75, 1]));
    const at = tint.offsetBytes >> 2;
    assert.deepStrictEqual([...program.uniformFloats.subarray(at, at + 4)],
        [0.25, 0.5, 0.75, 1]);
});

test("glUniform1i on a sampler rebinds a texture unit rather than writing a value", () => {
    const { executor } = newExecutor();
    const program = linkedProgram(executor);
    const sampler = program.uniformByName.get("tex0");
    run(executor, new GLStream().uniformiv(sampler.location, 1, 1, [3]));
    assert.strictEqual(program.samplerUnits.get("tex0"), 3);
});

test("glGetShaderiv reports a compile failure with a usable log", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_SHADER", 20, GL.VERTEX_SHADER)
        .shaderSource(20, "void main(void) { gl_Position = nonsense; }")
        .call("COMPILE_SHADER", 20);
    const status = stream.queryObjectiv(1, 20, GL.COMPILE_STATUS);
    run(executor, stream);
    assert.strictEqual(status.view.getUint32(16, true), 0);
    const shader = executor.current.shareGroup.shaders.get(20);
    assert.ok(shader.compiled.log.indexOf("undeclared identifier") >= 0,
        shader.compiled.log);
});

test("a program draw binds the sampler's unit, not the sampler's index", () => {
    const { executor, log } = newExecutor();
    const program = linkedProgram(executor);
    const sampler = program.uniformByName.get("tex0");
    const stream = new GLStream()
        .names(GLFN.GEN_TEXTURES, [30])
        .call("ACTIVE_TEXTURE", GL.TEXTURE0 + 2)
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 30)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGBA, 1, 1, GL.RGBA, GL.UNSIGNED_BYTE,
            new Uint8Array([9, 9, 9, 255]))
        .call("TEX_PARAMETERI", GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.LINEAR)
        .uniformiv(sampler.location, 1, 1, [2]);
    immediateTriangle(stream);
    run(executor, stream);
    assert.strictEqual(log.draws.length, 1);
    const groups = log.bindGroups.filter(g =>
        g.descriptor.layout.index === 2);
    assert.ok(groups.length, "the texture group is created");
});

test("program bind groups omit resources used only by uncalled helpers", () => {
    const { executor, log } = newExecutor();
    const vertex =
        "uniform mat4 transform;\n" +
        "vec4 deadState(void) {\n" +
        "  return gl_ModelViewMatrix * gl_Vertex;\n" +
        "}\n" +
        "vec4 livePosition(void) { return transform * gl_Vertex; }\n" +
        "void main(void) { gl_Position = livePosition(); }\n";
    const fragment =
        "uniform sampler2D deadTexture;\n" +
        "uniform sampler2D liveTexture;\n" +
        "vec4 deadSample(void) {\n" +
        "  return texture2D(deadTexture, vec2(0.5));\n" +
        "}\n" +
        "vec4 liveSample(void) {\n" +
        "  return texture2D(liveTexture, vec2(0.5));\n" +
        "}\n" +
        "void main(void) {\n" +
        "  gl_FragColor = liveSample();\n" +
        "}\n";
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_PROGRAM", 1)
        .call("CREATE_SHADER", 10, GL.VERTEX_SHADER)
        .call("CREATE_SHADER", 11, GL.FRAGMENT_SHADER)
        .shaderSource(10, vertex)
        .shaderSource(11, fragment)
        .call("COMPILE_SHADER", 10)
        .call("COMPILE_SHADER", 11)
        .call("ATTACH_SHADER", 1, 10)
        .call("ATTACH_SHADER", 1, 11)
        .call("LINK_PROGRAM", 1)
        .call("USE_PROGRAM", 1);
    immediateTriangle(stream);
    run(executor, stream);

    assert.strictEqual(log.draws.length, 1);
    const uniformGroup = log.bindGroups.find(group =>
        group.descriptor.layout.index === 1);
    assert.deepStrictEqual(uniformGroup.descriptor.entries.map(entry =>
        entry.binding), [1],
        "deadState must not add fixed-state binding 0 to the auto layout");
    const textureGroup = log.bindGroups.find(group =>
        group.descriptor.layout.index === 2);
    assert.deepStrictEqual(textureGroup.descriptor.entries.map(entry =>
        entry.binding), [2, 3],
        "only the sampler reachable from fs_main belongs to the auto layout");
});

/* ---- refusals ---- */

test("the accumulation buffer implements clear, load, arithmetic and return", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CLEAR_ACCUM", 0.1, 0.2, 0.3, 0.4)
        .call("CLEAR", GL.ACCUM_BUFFER_BIT)
        .call("ACCUM", 0x0101, 0.5)  // GL_LOAD
        .call("ACCUM", 0x0100, 0.25) // GL_ACCUM
        .call("ACCUM", 0x0103, 2)    // GL_MULT
        .call("ACCUM", 0x0104, 0.1)  // GL_ADD
        .call("ACCUM", 0x0102, 1));  // GL_RETURN
    assert.ok(executor.accumBuffer);
    assert.strictEqual(executor.accumBuffer.currentTexture.descriptor.format,
        "rgba16float");
    assert.strictEqual(executor.stats.refusals, 0);
});

test("invalid accumulation operations are refused loudly", () => {
    const { executor } = newExecutor();
    const original = console.error;
    console.error = () => {};
    try {
        run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
            .call("ACCUM", 0xDEAD, 1));
    } finally {
        console.error = original;
    }
    assert.strictEqual(executor.stats.refusals, 1);
});

test("a truncated record stops the batch instead of reading past it", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
    const bytes = stream.bytes();
    const truncated = bytes.subarray(0, bytes.length - 4);
    const original = console.error;
    console.error = () => {};
    try {
        executor.submit(truncated, {});
    } finally {
        console.error = original;
    }
    assert.ok(executor.stats.refusals >= 1);
});



test("glDrawElements draws with the indices the guest sent", () => {
    const { executor, log } = newExecutor();
    // 0x544D4143 is the multitexture magic that sits between the fixed header
    // and the indices. Reading the indices from the end of the fixed header
    // instead yielded 16707 and 21581 -- the two halves of that word -- so
    // every indexed draw referenced vertices that were never there.
    const indices = new Uint8Array(new Uint16Array([2, 0, 1]).buffer);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .drawElements(GL.TRIANGLES, indices, GL.UNSIGNED_SHORT, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } }));

    assert.strictEqual(log.draws.length, 1, "the indexed draw must reach the GPU");
    const bind = log.draws[0].pass.calls.filter(call => call[0] === "indexBuffer").pop();
    assert.ok(bind, "an indexed draw binds an index buffer");
    const uploaded = new Uint32Array(executor.vertexStaging.buffer,
        executor.vertexStaging.byteOffset + bind[3], 3);
    assert.deepStrictEqual([...uploaded], [2, 0, 1],
        "the indices must be the guest's own, not the bytes of the header " +
        "that precedes them");
});

/* ---- ARB program error reporting ---- */

function readQueriedString(answer) {
    const payload = answer.bytes;
    let text = "";
    for (let i = 16; i < payload.length && payload[i]; ++i)
        text += String.fromCharCode(payload[i]);
    return text;
}

test("a bad ARB program is the app's error, not a host refusal", () => {
    const { executor } = newExecutor();
    const bad = "this is not a program\nEND\n";
    const payload = new Uint8Array(16 + bad.length);
    const view = new DataView(payload.buffer);
    view.setUint32(0, GL.VERTEX_PROGRAM_ARB, true);
    view.setUint32(4, GL.PROGRAM_FORMAT_ASCII_ARB, true);
    view.setInt32(8, bad.length, true);
    for (let i = 0; i < bad.length; ++i) payload[16 + i] = bad.charCodeAt(i);
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_PROGRAMS_ARB, [1])
        .call("BIND_PROGRAM_ARB", GL.VERTEX_PROGRAM_ARB, 1)
        .record(GLFN.PROGRAM_STRING_ARB, payload);
    const position = stream.queryInteger(GL.PROGRAM_ERROR_POSITION_ARB);
    const text = stream.queryString(GL.PROGRAM_ERROR_STRING_ARB, 256);
    const error = stream.queryError();
    const before = executor.stats.refusals;
    const warn = console.warn;
    console.warn = () => {};
    try { run(executor, stream); } finally { console.warn = warn; }

    assert.strictEqual(executor.stats.refusals, before,
        "the app wrote a bad program; the host refused nothing. Counting it " +
        "would make refusals useless as 'what did the host fail to draw'");
    assert.strictEqual(error.view.getUint32(4, true), GL.INVALID_OPERATION,
        "the extension reports a bad program through glGetError");
    assert.ok(readQueriedString(text).length > 0,
        "GL_PROGRAM_ERROR_STRING_ARB must say why, or the app shows a blank " +
        "error dialog");
    assert.ok(position.view.getInt32(8, true) >= 0,
        "GL_PROGRAM_ERROR_POSITION_ARB is a character offset into the source");
});

test("GL_PROGRAM_ERROR_POSITION_ARB is -1 when nothing failed", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
    const position = stream.queryInteger(GL.PROGRAM_ERROR_POSITION_ARB);
    const text = stream.queryString(GL.PROGRAM_ERROR_STRING_ARB, 64);
    run(executor, stream);
    assert.strictEqual(position.view.getInt32(8, true), -1,
        "0 would read as 'an error at character 0' and make a conformance " +
        "viewer report a failure on a program that compiled");
    assert.strictEqual(readQueriedString(text), "");
});

/* ---- results that arrive after the batch ---- */

test("vertex ring rollover during index expansion keeps the draw's allocations alive", () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64));
    executor.vertexCapacity = 1024;
    const stride = executor.wantedAttributes(null).reduce((n, a) => n + a.components, 0) * 4;
    executor.vertexCursor = 1024 - 4 * stride;
    const original = executor.vertexRing;
    run(executor, new GLStream().drawArrays(GL.QUADS, 4, { vertex: { size: 3, type: GL.FLOAT,
            data: new Uint8Array(48) } }));
    assert.strictEqual(executor.stats.refusals, 0);
    assert.strictEqual(log.draws.length, 1);
    const calls = log.draws[0].pass.calls;
    const vb = calls.find(c => c[0] === "vertexBuffer");
    const ib = calls.find(c => c[0] === "indexBuffer");
    assert.strictEqual(vb[2], original);
    assert.notStrictEqual(vb[2], ib[1], "indices use the new page, vertices the old one");
    assert.strictEqual(ib[3], 0);
    assert.strictEqual(log.submits.length, 1, "submit only after encoding the whole draw");
});

test("uniform rollover retains active uniform slices and an open draw pass", () => {
    const { executor, log } = newExecutor();
    executor.uniformCapacity = 1024;
    executor.uniformCursor = 1024;
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .drawArrays(GL.TRIANGLES, 3, { vertex: { size: 3, type: GL.FLOAT,
            data: new Uint8Array(36) } }));
    assert.strictEqual(executor.stats.refusals, 0);
    assert.strictEqual(log.draws.length, 1);
    assert.strictEqual(log.submits.length, 1);
});

test("bind groups use each uniform slice's page across a rollover", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_PROGRAM", 1)
        .call("CREATE_SHADER", 2, GL.VERTEX_SHADER)
        .call("CREATE_SHADER", 3, GL.FRAGMENT_SHADER)
        .shaderSource(2, "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }")
        .shaderSource(3, "uniform vec4 tint; void main() { gl_FragColor = tint; }")
        .call("COMPILE_SHADER", 2).call("COMPILE_SHADER", 3)
        .call("ATTACH_SHADER", 1, 2).call("ATTACH_SHADER", 1, 3)
        .call("LINK_PROGRAM", 1).call("USE_PROGRAM", 1));
    executor.uniformCapacity = 1024;
    executor.uniformCursor = 768;
    const original = executor.uniformRing;
    executor.ensurePass();
    const shaders = executor.resolveShaders();
    const pipeline = executor.ensurePipeline(shaders, { mode: GL.TRIANGLES,
        buffers: [] }, null);
    const groups = executor.buildBindGroups(pipeline, shaders);
    const entries = groups.find(g => g.index === 1).group.descriptor.entries;
    assert.ok(entries[0].resource.buffer === original);
    assert.ok(entries[1].resource.buffer === executor.uniformRing);
    assert.notStrictEqual(entries[0].resource.buffer, entries[1].resource.buffer);
    assert.strictEqual(entries[0].resource.offset, 768);
    assert.strictEqual(entries[1].resource.offset, 0);
});

asyncTest("query slot exhaustion submits completed segments and resumes the same GL query", async () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [1]).call("BEGIN_QUERY", GL.SAMPLES_PASSED, 1));
    executor.occlusionCapacity = 2;
    executor.endPass(); executor.ensurePass();
    executor.endPass(); executor.ensurePass();
    executor.endQuery(GL.SAMPLES_PASSED);
    executor.flushFrame();
    await Promise.resolve(); await Promise.resolve();
    assert.deepStrictEqual(log.queryResolves.map(r => r.count), [2, 1]);
    assert.strictEqual(executor.queries.get(1).ready, true);
    assert.strictEqual(executor.queries.get(1).result, 0);
    assert.strictEqual(executor.stats.refusals, 0);
});

asyncTest("occlusion segments across passes are ORed, not overwritten", async () => {
    for (const samples of [[1, 0], [0, 1], [0, 0]]) {
        const { executor, log } = newExecutor();
        run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
            .names(GLFN.GEN_QUERIES, [1]).call("BEGIN_QUERY", GL.SAMPLES_PASSED, 1));
        executor.endPass();
        executor.ensurePass();
        executor.endQuery(GL.SAMPLES_PASSED);
        executor.flushFrame();
        const staging = findBuffer(log, "GL occlusion readback");
        const words = new Uint32Array(staging.storage);
        samples.forEach((sample, slot) => { words[slot * 2] = sample; });
        await Promise.resolve(); await Promise.resolve();
        assert.strictEqual(log.queryResolves[0].count, 2);
        const query = executor.queries.get(1);
        assert.strictEqual(query.ready, true);
        assert.strictEqual(query.result > 0, samples.some(Boolean));
    }
});

asyncTest("query spanning submissions waits for every segment, even out of order", async () => {
    const { executor, log } = newExecutor();
    const maps = [];
    const create = executor.device.createBuffer.bind(executor.device);
    executor.device.createBuffer = desc => {
        const buffer = create(desc);
        if (desc.label === "GL occlusion readback")
            buffer.mapAsync = () => new Promise(resolve => maps.push(resolve));
        return buffer;
    };
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [1]).call("BEGIN_QUERY", GL.SAMPLES_PASSED, 1));
    executor.flushFrame();
    const first = findBuffer(log, "GL occlusion readback");
    executor.ensurePass();
    executor.endQuery(GL.SAMPLES_PASSED);
    executor.flushFrame();
    assert.strictEqual(maps.length, 2);
    const query = executor.queries.get(1);
    maps[1]();
    await Promise.resolve(); await Promise.resolve();
    assert.strictEqual(query.ready, false, "the earlier submission is still pending");
    new Uint32Array(first.storage)[0] = 1;
    maps[0]();
    await Promise.resolve(); await Promise.resolve();
    assert.strictEqual(query.ready, true);
    assert.ok(query.result > 0);
});

function findBuffer(log, label) {
    return log.buffers.filter(buffer => buffer.descriptor.label === label).pop();
}

asyncTest("an occlusion query resolves and reports a visible result", async () => {
    const { executor, log } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [5])
        .call("BEGIN_QUERY", GL.SAMPLES_PASSED, 5)
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } })
        .call("END_QUERY", GL.SAMPLES_PASSED);
    run(executor, stream);
    executor.flushFrame();

    assert.strictEqual(log.queryResolves.length, 1,
        "ending a query must resolve the set with the work that produced it");
    assert.strictEqual(log.queryResolves[0].first, 0);
    assert.strictEqual(log.queryResolves[0].count, 1,
        "one query in flight resolves one slot");

    // The GPU says slot 0 had samples pass.
    const staging = findBuffer(log, "GL occlusion readback");
    assert.ok(staging, "the resolved set is copied into a mappable buffer");
    new DataView(staging.storage).setUint32(0, 1, true);
    await Promise.resolve();
    await Promise.resolve();

    const query = executor.queries.get(5);
    assert.strictEqual(query.ready, true,
        "the guest spins on GL_QUERY_RESULT_AVAILABLE; it must become true");
    assert.ok(query.result > 0, "a visible query reports a saturated count");
});

asyncTest("a query the GPU says was fully occluded reports zero", async () => {
    const { executor, log } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [9])
        .call("BEGIN_QUERY", GL.SAMPLES_PASSED, 9)
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } })
        .call("END_QUERY", GL.SAMPLES_PASSED));
    executor.flushFrame();
    findBuffer(log, "GL occlusion readback");   // left zeroed
    await Promise.resolve();
    await Promise.resolve();
    const query = executor.queries.get(9);
    assert.strictEqual(query.ready, true, "an occluded query is still answered");
    assert.strictEqual(query.result, 0);
});

asyncTest("a failed occlusion readback falls back to visible", async () => {
    const { executor } = newExecutor();
    const originalCreateBuffer = executor.device.createBuffer.bind(executor.device);
    executor.device.createBuffer = descriptor => {
        const buffer = originalCreateBuffer(descriptor);
        if (descriptor.label === "GL occlusion readback")
            buffer.mapAsync = () => Promise.reject(new Error("map failed"));
        return buffer;
    };
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [11])
        .call("BEGIN_QUERY", GL.SAMPLES_PASSED, 11)
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } })
        .call("END_QUERY", GL.SAMPLES_PASSED));
    executor.flushFrame();
    await Promise.resolve();
    await Promise.resolve();
    await Promise.resolve();
    const query = executor.queries.get(11);
    assert.strictEqual(query.ready, true);
    assert.ok(query.result > 0,
        "a transport failure is not proof that the geometry was occluded");
});

asyncTest("query slots are reused after each resolve", async () => {
    const { executor, log } = newExecutor();
    const frame = name => new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_QUERIES, [name])
        .call("BEGIN_QUERY", GL.SAMPLES_PASSED, name)
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } })
        .call("END_QUERY", GL.SAMPLES_PASSED);
    run(executor, frame(1));
    executor.flushFrame();
    run(executor, frame(2));
    executor.flushFrame();
    assert.strictEqual(log.queryResolves.length, 2);
    assert.deepStrictEqual(
        log.queryResolves.map(resolve => resolve.first), [0, 0],
        "a frame's queries start again at slot 0, or the set fills up and " +
        "later queries are refused");
    await Promise.resolve();
});

asyncTest("a late query readback cannot overwrite a reused Cube 2 query", async () => {
    const { executor } = newExecutor();
    const originalCreateBuffer = executor.device.createBuffer.bind(executor.device);
    const stagingBuffers = [];
    const mapResolvers = [];
    executor.device.createBuffer = descriptor => {
        const buffer = originalCreateBuffer(descriptor);
        if (descriptor.label === "GL occlusion readback") {
            stagingBuffers.push(buffer);
            buffer.mapAsync = () => new Promise(resolve => mapResolvers.push(resolve));
        }
        return buffer;
    };
    const triangle = { vertex: { size: 3, type: GL.FLOAT,
        data: new Uint8Array(36) } };
    const queryFrame = includeName => {
        const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64);
        if (includeName) stream.names(GLFN.GEN_QUERIES, [7]);
        return stream.call("BEGIN_QUERY", GL.SAMPLES_PASSED, 7)
            .drawArrays(GL.TRIANGLES, 3, triangle)
            .call("END_QUERY", GL.SAMPLES_PASSED);
    };

    run(executor, queryFrame(true));
    executor.flushFrame();
    run(executor, queryFrame(false));
    executor.flushFrame();
    assert.strictEqual(mapResolvers.length, 2,
        "both generations should have independent asynchronous readbacks");

    // Complete generation one as occluded after generation two has already
    // reused the object. It must not make generation two ready with stale 0.
    mapResolvers[0]();
    await Promise.resolve();
    await Promise.resolve();
    const query = executor.queries.get(7);
    assert.strictEqual(query.generation, 2);
    assert.strictEqual(query.ready, false,
        "a stale readback must not publish into the current generation");

    // The current generation remains authoritative and completes normally.
    new DataView(stagingBuffers[1].storage).setUint32(0, 1, true);
    mapResolvers[1]();
    await Promise.resolve();
    await Promise.resolve();
    assert.strictEqual(query.ready, true);
    assert.ok(query.result > 0);
});

asyncTest("a pass already open is rebuilt to carry the occlusion query set", () => {
    const { executor, log } = newExecutor();
    const triangle = { vertex: { size: 3, type: GL.FLOAT,
        data: new Uint8Array(36) } };
    // Draw first, so a render pass exists before any query set does.
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .drawArrays(GL.TRIANGLES, 3, triangle)
        .names(GLFN.GEN_QUERIES, [3])
        .call("BEGIN_QUERY", GL.SAMPLES_PASSED, 3)
        .drawArrays(GL.TRIANGLES, 3, triangle)
        .call("END_QUERY", GL.SAMPLES_PASSED));
    const querying = log.passes.filter(pass =>
        pass.calls.some(call => call[0] === "beginQuery"));
    assert.strictEqual(querying.length, 1);
    assert.ok(querying[0].descriptor.occlusionQuerySet,
        "beginOcclusionQuery is a validation error unless the pass was " +
        "created with the set");
    return Promise.resolve();
});

asyncTest("glReadPixels writes the pixels and then the status word", async () => {
    const { executor, log } = newExecutor();
    const writes = [];
    executor.options.writeGuestMemory = (offset, data) =>
        writes.push({ offset, bytes: Uint8Array.from(data) });
    executor.current = executor.current || null;
    const stream = new GLStream().makeCurrent(1, 0, 0, 4, 4)
        // Read back something that exists: the attachment is created by work,
        // and a readPixels with no colour buffer is a legitimate failure.
        .drawArrays(GL.TRIANGLES, 3, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(36) } });
    const payload = new Uint8Array(32 + 4 * 4 * 4);
    const view = new DataView(payload.buffer);
    view.setInt32(0, 0, true);        // x
    view.setInt32(4, 0, true);        // y
    view.setInt32(8, 4, true);        // width
    view.setInt32(12, 4, true);       // height
    view.setUint32(16, GL.RGBA, true);
    view.setUint32(20, GL.UNSIGNED_BYTE, true);
    view.setUint32(24, 4 * 4 * 4, true);
    stream.record(GLFN.READ_PIXELS, payload);
    const record = stream.payloadView();
    run(executor, stream);

    assert.strictEqual(record.view.getUint32(28, true), 0,
        "the status must still read PENDING when the batch returns -- the " +
        "guest's single check was exactly this bug");
    await Promise.resolve();
    await Promise.resolve();
    assert.strictEqual(record.view.getUint32(28, true), 1,
        "the mapping completes and the status becomes OK");
    assert.deepStrictEqual(writes.map(write => write.offset).slice(-2).sort(
        (a, b) => a - b).length, 2);
    const last = writes[writes.length - 1];
    assert.strictEqual(last.bytes.byteLength, 4,
        "the status word is written last, after the pixels it announces");
});

/* ---- buffer objects and the VBO-direct draw path ---- */

test("pipeline cache separates constant attributes from vertex streams", () => {
    const { executor } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64));
    executor.ensurePass();
    const shaders = executor.resolveShaders();
    const request = { mode: GL.TRIANGLES, buffers: [{ stride: 16,
        attributes: [{ location: 0, format: "float32x4", offset: 0 }] }] };
    const vertex = executor.ensurePipeline(shaders, request, null);
    const constant = executor.ensurePipeline(shaders, { ...request,
        buffers: request.buffers.map(b => ({ ...b, stepMode: "instance" })) }, null);
    assert.notStrictEqual(vertex, constant,
        "the same attribute layout with a different stepping rule is a different pipeline");
    assert.strictEqual(constant.descriptor.vertex.buffers[0].stepMode, "instance");
    assert.strictEqual(executor.ensurePipeline(shaders, request, null), vertex);
});

test("VBO subdata preserves storage referenced by an unsubmitted draw", () => {
    const { executor, log } = newExecutor();
    const data = new Float32Array([-1, -1, 0, 1, -1, 0, 0, 1, 0]);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_BUFFERS, [4])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 4)
        .bufferData(GL.ARRAY_BUFFER, data.byteLength, GL.STATIC_DRAW,
            new Uint8Array(data.buffer))
        .call("ENABLE_CLIENT_STATE", GL.VERTEX_ARRAY)
        .pointerVBO("VERTEX_POINTER_VBO", 3, GL.FLOAT, 12, 0)
        .call("DRAW_ARRAYS_DIRECT", GL.TRIANGLES, 0, 3));
    const buffer = executor.current.shareGroup.buffers.get(4);
    const original = buffer.gpuBuffer;
    const writeCount = log.bufferWrites.filter(w => w.buffer === original).length;
    executor.bufferSubData(GL.ARRAY_BUFFER, 0,
        new Uint8Array(new Float32Array([2, 2, 2]).buffer));
    assert.notStrictEqual(buffer.gpuBuffer, original,
        "queue.writeBuffer precedes submission, so pending draws need the old storage");
    assert.strictEqual(log.bufferWrites.filter(w => w.buffer === original).length,
        writeCount, "old geometry is never overwritten before its draw executes");
    assert.strictEqual(original.destroyed, false, "retire only after submission");
    const replacement = buffer.gpuBuffer;
    executor.bufferSubData(GL.ARRAY_BUFFER, 12, new Uint8Array([1, 2, 3, 4]));
    assert.strictEqual(buffer.gpuBuffer, replacement,
        "storage not yet used by a draw can still be updated in place");
    executor.flushFrame();
    assert.strictEqual(original.destroyed, true);
});

test("odd-sized VBO data and subdata retain their final bytes", () => {
    const { executor, log } = newExecutor();
    const uploads = [];
    const write = executor.device.queue.writeBuffer;
    executor.device.queue.writeBuffer = (buffer, offset, data, start, size) => {
        write(buffer, offset, data, start, size);
        uploads.push(new Uint8Array(data.buffer, data.byteOffset + start, size).slice());
    };
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_BUFFERS, [4])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 4)
        .bufferData(GL.ARRAY_BUFFER, 6, GL.STATIC_DRAW,
            new Uint8Array([1, 2, 3, 4, 5, 6])));
    assert.strictEqual(log.bufferWrites.at(-1).size, 8,
        "upload the padded storage, not just its first four bytes");
    assert.deepStrictEqual([...uploads.at(-1)], [1, 2, 3, 4, 5, 6, 0, 0]);
    executor.bufferSubData(GL.ARRAY_BUFFER, 5, new Uint8Array([7]));
    assert.strictEqual(log.bufferWrites.at(-1).size, 4);
    assert.deepStrictEqual([...uploads.at(-1)], [5, 7, 0, 0]);
    assert.deepStrictEqual([...executor.bufferFor(GL.ARRAY_BUFFER).shadow],
        [1, 2, 3, 4, 5, 7], "the GL-visible storage remains its requested size");
});

/*
 * openglproxy sends GL_ARRAY_BUFFER_ARB (0x8892) -- the name ARB_vertex_buffer_object
 * gave the target, and the only one any GL 1.5 caller uses. The host's enum
 * table happened to carry ARRAY_BUFFER_ARB but not ARRAY_BUFFER, so the
 * comparison in glBindBuffer read `undefined` and refused every array binding
 * ever made: no vertex data reached the GPU and every VBO draw was skipped.
 * That is what a GLview render test looks like as a black window.
 */
test("glBindBuffer accepts GL_ARRAY_BUFFER and glBufferData fills it", () => {
    const { executor, log } = newExecutor();
    const data = new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 0]);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_BUFFERS, [4])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 4)
        .bufferData(GL.ARRAY_BUFFER, data.byteLength, GL.STATIC_DRAW,
            new Uint8Array(data.buffer)));

    assert.deepStrictEqual(log.refusals || [], [],
        "binding the array buffer is not a refusal");
    assert.strictEqual(executor.current.arrayBuffer, 4);
    const buffer = executor.current.shareGroup.buffers.get(4);
    assert.ok(buffer && buffer.gpuBuffer, "glBufferData allocated storage");
    assert.strictEqual(buffer.size, data.byteLength);
    assert.deepStrictEqual(new Float32Array(buffer.shadow.buffer,
        buffer.shadow.byteOffset, 9), data);
});

test("a client-array draw promoted to VBOs reaches the GPU", () => {
    const { executor, log } = newExecutor();
    const vertices = new Float32Array([-1, -1, 0, 1, -1, 0, 0, 1, 0]);
    const indices = new Uint16Array([0, 1, 2]);
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_BUFFERS, [1, 2])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 1)
        .bufferData(GL.ARRAY_BUFFER, vertices.byteLength, GL.STATIC_DRAW,
            new Uint8Array(vertices.buffer))
        .call("BIND_BUFFER", GL.ELEMENT_ARRAY_BUFFER, 2)
        .bufferData(GL.ELEMENT_ARRAY_BUFFER, indices.byteLength, GL.STATIC_DRAW,
            new Uint8Array(indices.buffer))
        .call("ENABLE_CLIENT_STATE", GL.VERTEX_ARRAY)
        .pointerVBO("VERTEX_POINTER_VBO", 3, GL.FLOAT, 12, 0)
        .drawElementsDirect(GL.TRIANGLES, 3, GL.UNSIGNED_SHORT, 0));

    assert.strictEqual(log.draws.length, 1, "the VBO draw was issued");
    assert.strictEqual(log.draws[0].indexed, true);
    assert.strictEqual(log.draws[0].count, 3);
    const bound = log.draws[0].pass.calls.filter(call => call[0] === "vertexBuffer");
    assert.ok(bound.length >= 1, "the vertex buffer was bound to the pass");
    assert.strictEqual(bound[0][2],
        executor.current.shareGroup.buffers.get(1).gpuBuffer,
        "the pass reads the buffer glBufferData filled, not a scratch upload");
});

test("glVertexAttribPointer does not re-enable Cube 2's constant model color", () => {
    const { executor, log } = newExecutor();
    const vertexShader = "attribute vec4 vvertex, vcolor;\n" +
        "varying vec4 color;\n" +
        "void main() { gl_Position = vvertex; color = vcolor; }";
    const fragmentShader = "varying vec4 color;\n" +
        "void main() { gl_FragColor = color; }";
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_PROGRAM", 1)
        .call("CREATE_SHADER", 2, GL.VERTEX_SHADER)
        .call("CREATE_SHADER", 3, GL.FRAGMENT_SHADER)
        .shaderSource(2, vertexShader)
        .shaderSource(3, fragmentShader)
        .call("COMPILE_SHADER", 2)
        .call("COMPILE_SHADER", 3)
        .call("ATTACH_SHADER", 1, 2)
        .call("ATTACH_SHADER", 1, 3)
        .call("LINK_PROGRAM", 1)
        .call("USE_PROGRAM", 1));

    const program = executor.current.shareGroup.programs.get(1);
    assert.ok(program.linked, program.log);
    const vertexLocation = program.link.reflection.attributes.find(
        attribute => attribute.name === "vvertex").location;
    const colorLocation = program.link.reflection.attributes.find(
        attribute => attribute.name === "vcolor").location;
    const vertices = new Float32Array([
        -1, -1, 0, 1,
         1, -1, 0, 1,
         0,  1, 0, 1,
    ]);
    const indices = new Uint16Array([0, 1, 2]);
    const draw = new GLStream()
        .names(GLFN.GEN_BUFFERS, [11, 12])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 11)
        .bufferData(GL.ARRAY_BUFFER, vertices.byteLength, GL.STATIC_DRAW,
            new Uint8Array(vertices.buffer))
        .attribPointerVBO(vertexLocation, 4, GL.FLOAT, false, 16, 0)
        .call("ENABLE_VERTEX_ATTRIB_ARRAY", vertexLocation)
        .attribPointerVBO(colorLocation, 4, GL.FLOAT, false, 16, 0)
        .call("ENABLE_VERTEX_ATTRIB_ARRAY", colorLocation)
        .call("DISABLE_VERTEX_ATTRIB_ARRAY", colorLocation)
        .call("VERTEX_ATTRIB4F", colorLocation, 1, 1, 1, 1)
        // Cube 2 is allowed to update the dormant pointer while keeping the
        // array disabled.  The draw must still consume the current constant.
        .attribPointerVBO(colorLocation, 4, GL.FLOAT, false, 16, 0)
        .call("BIND_BUFFER", GL.ELEMENT_ARRAY_BUFFER, 12)
        .bufferData(GL.ELEMENT_ARRAY_BUFFER, indices.byteLength,
            GL.STATIC_DRAW, new Uint8Array(indices.buffer))
        .drawElementsDirect(GL.TRIANGLES, 3, GL.UNSIGNED_SHORT, 0);
    run(executor, draw);

    assert.strictEqual(executor.current.genericAttribs[colorLocation].enabled,
        false);
    assert.strictEqual(executor.current.arrays["generic" + colorLocation].enabled,
        false, "the VBO array view follows the generic attribute enable");
    assert.strictEqual(log.draws.length, 1);
    const buffers = log.draws[0].pipeline.descriptor.vertex.buffers;
    const constant = buffers.find(buffer => buffer.stepMode === "instance" &&
        buffer.attributes.some(attribute =>
            attribute.shaderLocation === colorLocation));
    assert.ok(constant, "disabled vcolor is supplied by a constant buffer");
    assert.ok(!buffers.some(buffer => buffer.stepMode !== "instance" &&
        buffer.attributes.some(attribute =>
            attribute.shaderLocation === colorLocation)),
        "the stale color VBO is not read by the model draw");
});

/* ---- the ARB program parameter and info-log records ---- */

/*
 * These six-word query headers are where the two sides disagreed silently:
 * every one of them writes its answer back into the record the guest is still
 * holding, so a field in the wrong place is not an error anywhere, just a
 * wrong number handed to the application.
 */
const ARB_PARAMETER_ENV = 1;
const ARB_PARAMETER_LOCAL = 2;

const ARB_VERTEX_PROGRAM = "!!ARBvp1.0\n" +
    "PARAM mvp[4] = { state.matrix.mvp };\n" +
    "TEMP position;\n" +
    "DP4 position.x, mvp[0], vertex.position;\n" +
    "DP4 position.y, mvp[1], vertex.position;\n" +
    "DP4 position.z, mvp[2], vertex.position;\n" +
    "DP4 position.w, mvp[3], vertex.position;\n" +
    "MOV result.position, position;\n" +
    "MOV result.color, program.env[3];\n" +
    "END\n";

const ARB_MULTITEXTURE_VERTEX_PROGRAM = "!!ARBvp1.0\n" +
    "MOV result.position, vertex.position;\n" +
    "MOV result.color, vertex.color;\n" +
    "MOV result.texcoord[0], vertex.texcoord[0];\n" +
    "MOV result.texcoord[1], vertex.texcoord[1];\n" +
    "END\n";

const ARB_IMMEDIATE_VERTEX_PROGRAM = "!!ARBvp1.0\n" +
    "MOV result.position, vertex.position;\n" +
    "ADD result.color, vertex.normal, vertex.attrib[6];\n" +
    "END\n";

function boundARBProgram(executor, stream) {
    return stream.names(GLFN.GEN_PROGRAMS_ARB, [1])
        .call("BIND_PROGRAM_ARB", GL.VERTEX_PROGRAM_ARB, 1);
}

test("ARB immediate mode binds every shader-read vertex attribute", () => {
    const { executor, log } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB,
            ARB_IMMEDIATE_VERTEX_PROGRAM)
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("NORMAL3F", 0, 0, 1)
        .call("VERTEX_ATTRIB4F", 6, 0.25, 0.5, 0.75, 1);
    immediateTriangle(stream);
    run(executor, stream);

    assert.strictEqual(log.draws.length, 1);
    const descriptor = log.draws[0].pipeline.descriptor.vertex;
    assert.deepStrictEqual(descriptor.buffers[0].attributes.map(attribute =>
        attribute.shaderLocation).sort((a, b) => a - b), [0, 2, 6],
        "normal slot 2 and unaliased generic slot 6 are both present");
    const packed = packedAttributes(executor, log, 3);
    assert.deepStrictEqual(packed[2][0], [0, 0, 1]);
    assert.deepStrictEqual(packed[6][0], [0.25, 0.5, 0.75, 1]);
});

test("an ARB program string arrives intact and assembles", () => {
    const { executor, log } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM);
    run(executor, stream);
    const program = executor.current.shareGroup.arbPrograms.get(1);
    assert.strictEqual(program.source, ARB_VERTEX_PROGRAM,
        "the assembly text is read from offset 16, after {target, format, " +
        "length, reserved}");
    assert.ok(program.compiled && program.compiled.ok,
        "it assembled: " + (program.compiled && program.compiled.log));
    assert.strictEqual(executor.arbErrorPosition, -1);
});

test("an ARB vertex program drives the fixed multitexture fragment stage", () => {
    const { executor, log } = newExecutor();
    const vertices = new Float32Array([
        -1, -1, 0, 1,  -1, 1, 0, 1,  1, -1, 0, 1,
    ]);
    const tex0 = new Float32Array([0, 0, 0, 1, 1, 0]);
    const tex1 = new Float32Array([0, 1, 1, 1, 1, 0]);
    const block = (data, size) => ({
        size, type: GL.FLOAT, stride: 0,
        data: new Uint8Array(data.buffer),
    });
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB,
            ARB_MULTITEXTURE_VERTEX_PROGRAM)
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("SHADE_MODEL", GL.FLAT)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0)
        .call("ENABLE", GL.TEXTURE_2D)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0 + 1)
        .call("ENABLE", GL.TEXTURE_2D)
        .drawElements(GL.TRIANGLES, new Uint16Array([0, 1, 2]),
            GL.UNSIGNED_SHORT, {
                vertex: block(vertices, 4),
                texCoord0: block(tex0, 2),
                texCoord1: block(tex1, 2),
            });
    run(executor, stream);

    assert.deepStrictEqual(log.refusals || [], [],
        "the mixed ARB/fixed pipeline is no longer refused");
    assert.strictEqual(log.draws.length, 1,
        "glDrawElements reaches the GPU instead of producing a black frame");
    const pipeline = log.draws[0].pipeline.descriptor;
    assert.ok(pipeline.vertex.module.code.includes(
        "generated by gl_arb_program.js r2"));
    assert.ok(pipeline.fragment.module.code.includes(
        "generated by gl_fixed_function.js r2"));
    assert.ok(pipeline.fragment.module.code.includes(
        "@location(3) v3 : vec4<f32>"));
    assert.ok(pipeline.fragment.module.code.includes(
        "@location(4) v4 : vec4<f32>"));
    assert.ok(pipeline.vertex.module.code.includes(
        "@interpolate(flat) @location(0) frontColor"));
    assert.ok(pipeline.fragment.module.code.includes(
        "@interpolate(flat) @location(0) v0"),
        "GL_FLAT has matching interpolation on both shader stages");
    assert.ok(!pipeline.vertex.module.code.includes(
        "@group(1) @binding(1)"),
        "the parameter-free ARB program does not declare an unused binding");
    const uniformGroup = log.bindGroups.find(group =>
        group.descriptor.layout.index === 1);
    assert.strictEqual(uniformGroup, undefined,
        "the shared state declaration is used by neither selected entry point, " +
        "so WebGPU omits group 1 from the automatic pipeline layout");
});

/*
 * ARB_vertex_program section 2.14.3: generic attributes 0-5 and 8-15 alias the
 * conventional ones. glview's 1.4 vertex-program test draws through that
 * aliasing, and the two directions below both used to pack the generic
 * attribute's (0, 0, 0, 1) default into every vertex -- a black frame with no
 * refusal, no GL error and a draw that reached the GPU looking healthy.
 */
const ARB_ALIASED_POSITION_PROGRAM = "!!ARBvp1.0\n" +
    "MOV result.position, vertex.position;\n" +
    "MOV result.texcoord[0], vertex.texcoord[0];\n" +
    "END\n";

const ARB_GENERIC_POSITION_PROGRAM = "!!ARBvp1.0\n" +
    "MOV result.position, vertex.attrib[0];\n" +
    "MOV result.texcoord[0], vertex.attrib[8];\n" +
    "END\n";

const ALIAS_VERTICES = new Float32Array([
    -1, -1, 0, 1,  -1, 1, 0, 1,  1, -1, 0, 1,
]);
const ALIAS_TEXCOORDS = new Float32Array([0, 0, 0, 1, 1, 0]);

/* The packed vertex data the draw uploaded, as {location -> [components]}. */
function packedAttributes(executor, log, vertexCount) {
    const draw = log.draws[0];
    const out = {};
    for (const [slot, buffer] of draw.pipeline.descriptor.vertex.buffers.entries()) {
        const binding = draw.pass.calls.find(c => c[0] === "vertexBuffer" && c[1] === slot);
        const floats = new Float32Array(binding[2].storage, binding[3] || 0);
        const stride = buffer.arrayStride / 4;
        for (const attribute of buffer.attributes) {
            assert.ok(attribute.format.startsWith("float32"));
            const components = Number(/x(\d)$/.exec(attribute.format) ?
                /x(\d)$/.exec(attribute.format)[1] : 1);
            const at = attribute.offset / 4;
            out[attribute.shaderLocation] = [];
            for (let v = 0; v < vertexCount; ++v) {
                const start = (buffer.stepMode === "instance" ? 0 : v * stride) + at;
                out[attribute.shaderLocation].push(
                    Array.from(floats.subarray(start, start + components)));
            }
        }
    }
    return out;
}

test("vertex.position reads the array sent as generic attribute 0", () => {
    const { executor, log } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_ALIASED_POSITION_PROGRAM)
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0)
        .call("ENABLE", GL.TEXTURE_2D)
        .drawElementsGL2(GL.TRIANGLES, new Uint16Array([0, 1, 2]),
            GL.UNSIGNED_SHORT, {}, [
                { index: 0, size: 4, type: GL.FLOAT, stride: 0,
                  normalized: false,
                  data: new Uint8Array(ALIAS_VERTICES.buffer) },
                { index: 8, size: 2, type: GL.FLOAT, stride: 0,
                  normalized: false,
                  data: new Uint8Array(ALIAS_TEXCOORDS.buffer) },
            ]);
    run(executor, stream);

    assert.deepStrictEqual(log.refusals || [], []);
    assert.strictEqual(log.draws.length, 1);
    const packed = packedAttributes(executor, log, 3);
    assert.deepStrictEqual(packed[0][0], [-1, -1, 0, 1],
        "generic attribute 0 feeds vertex.position rather than its default");
    assert.deepStrictEqual(packed[0][1], [-1, 1, 0, 1]);
    assert.deepStrictEqual(packed[8][1], [0, 1, 0, 1],
        "generic attribute 8 feeds vertex.texcoord[0]");
});

test("vertex.attrib[0] reads the array sent through glVertexPointer", () => {
    const { executor, log } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_GENERIC_POSITION_PROGRAM)
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0)
        .call("ENABLE", GL.TEXTURE_2D)
        .drawElementsGL2(GL.TRIANGLES, new Uint16Array([0, 1, 2]),
            GL.UNSIGNED_SHORT, {
                vertex: { size: 4, type: GL.FLOAT, stride: 0,
                          data: new Uint8Array(ALIAS_VERTICES.buffer) },
                texCoord0: { size: 2, type: GL.FLOAT, stride: 0,
                             data: new Uint8Array(ALIAS_TEXCOORDS.buffer) },
            }, []);
    run(executor, stream);

    assert.deepStrictEqual(log.refusals || [], []);
    assert.strictEqual(log.draws.length, 1);
    const packed = packedAttributes(executor, log, 3);
    assert.deepStrictEqual(packed[0][0], [-1, -1, 0, 1],
        "glVertexPointer's array feeds vertex.attrib[0]");
    assert.deepStrictEqual(packed[8][1], [0, 1, 0, 1],
        "glTexCoordPointer's array feeds vertex.attrib[8]");
});

test("glview's ARB bump program draws from widened interleaved VBO arrays", () => {
    const { executor, log } = newExecutor();
    const program = `!!ARBvp1.0
ATTRIB iPos = vertex.position;
ATTRIB iNormal = vertex.normal;
ATTRIB iTangent = vertex.attrib[6];
ATTRIB iTex0 = vertex.texcoord[0];
ATTRIB iTex1 = vertex.texcoord[1];
PARAM mvp[4] = { state.matrix.mvp };
PARAM lightDir = program.env[0];
PARAM half = 0.5;
OUTPUT oPos = result.position;
OUTPUT oColor = result.color;
OUTPUT oTex0 = result.texcoord[0];
OUTPUT oTex1 = result.texcoord[1];
TEMP T;
TEMP N;
TEMP B;
TEMP light_surf;
DP4 oPos.x, mvp[0], iPos;
DP4 oPos.y, mvp[1], iPos;
DP4 oPos.z, mvp[2], iPos;
DP4 oPos.w, mvp[3], iPos;
MOV T, iTangent;
MOV N, iNormal;
MUL B, N.zxyw, T.yzxw;
MAD B, N.yzxw, T.zxyw, -B;
MUL B.xyz, B, T.w;
DP3 light_surf.x, T, lightDir;
DP3 light_surf.y, B, lightDir;
DP3 light_surf.z, N, lightDir;
MAD oColor, light_surf, half, half;
MOV oTex0, iTex0;
MOV oTex1, iTex1;
END`;
    // position3, normal3, tangent4, texcoord0.xy, texcoord1.xy
    const vertices = new Float32Array([
        -1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0,  0, 0,
         1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 0,  1, 0,
         0,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 1,  0, 1,
    ]);
    const indices = new Uint16Array([0, 1, 2]);
    const stride = 14 * 4;
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, program)
        .programParameterARB(ARB_PARAMETER_ENV, GL.VERTEX_PROGRAM_ARB, 0,
            [0, 0, 1, 0])
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0)
        .call("ENABLE", GL.TEXTURE_2D)
        .call("ACTIVE_TEXTURE", GL.TEXTURE0 + 1)
        .call("ENABLE", GL.TEXTURE_2D)
        .names(GLFN.GEN_BUFFERS, [1, 2])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 1)
        .bufferData(GL.ARRAY_BUFFER, vertices.byteLength, GL.STATIC_DRAW,
            new Uint8Array(vertices.buffer))
        .pointerVBO("VERTEX_POINTER_VBO", 3, GL.FLOAT, stride, 0)
        .call("ENABLE_CLIENT_STATE", GL.VERTEX_ARRAY)
        .pointerVBO("NORMAL_POINTER_VBO", 3, GL.FLOAT, stride, 3 * 4)
        .call("ENABLE_CLIENT_STATE", GL.NORMAL_ARRAY)
        .attribPointerVBO(6, 4, GL.FLOAT, false, stride, 6 * 4)
        .call("ENABLE_VERTEX_ATTRIB_ARRAY", 6)
        .call("CLIENT_ACTIVE_TEXTURE", GL.TEXTURE0)
        .pointerVBO("TEX_COORD_POINTER_VBO", 2, GL.FLOAT, stride, 10 * 4)
        .call("ENABLE_CLIENT_STATE", GL.TEXTURE_COORD_ARRAY)
        .call("CLIENT_ACTIVE_TEXTURE", GL.TEXTURE0 + 1)
        .pointerVBO("TEX_COORD_POINTER_VBO", 2, GL.FLOAT, stride, 12 * 4)
        .call("ENABLE_CLIENT_STATE", GL.TEXTURE_COORD_ARRAY)
        .call("BIND_BUFFER", GL.ELEMENT_ARRAY_BUFFER, 2)
        .bufferData(GL.ELEMENT_ARRAY_BUFFER, indices.byteLength, GL.STATIC_DRAW,
            new Uint8Array(indices.buffer))
        .drawElementsDirect(GL.TRIANGLES, 3, GL.UNSIGNED_SHORT, 0);
    run(executor, stream);

    assert.deepStrictEqual(log.refusals || [], []);
    assert.strictEqual(log.draws.length, 1,
        "the exact 1.5 program/VBO shape reaches a draw");
    const descriptor = log.draws[0].pipeline.descriptor.vertex;
    assert.deepStrictEqual(descriptor.buffers.flatMap(b => b.attributes).map(a =>
        [a.shaderLocation, a.format]).sort((a, b) => a[0] - b[0]), [
        [0, "float32x4"], [6, "float32x4"], [2, "float32x3"],
        [8, "float32x4"], [9, "float32x4"],
    ].sort((a, b) => a[0] - b[0]),
    "GL size-3/2 arrays are widened to the ARB program input widths");
    const packed = packedAttributes(executor, log, 3);
    assert.deepStrictEqual(packed[0][0], [-1, -1, 0, 1]);
    assert.deepStrictEqual(packed[8][1], [1, 0, 0, 1]);
    assert.deepStrictEqual(packed[9][2], [0, 1, 0, 1]);
    assert.ok(descriptor.module.code.includes(
        "vec4<f32>(0.5, 0.5, 0.5, 0.5)"),
        "the shared 1.4 scalar-PARAM fix is present in the 1.5 shader too");
});

test("a vertex and a fragment program keep separate parameter namespaces", () => {
    const { executor, log } = newExecutor();
    const vertexProgram = "!!ARBvp1.0\n" +
        "PARAM scale = program.local[0];\n" +
        "MUL result.position, vertex.position, scale;\n" +
        "END\n";
    const fragmentProgram = "!!ARBfp1.0\n" +
        "PARAM tint = program.local[0];\n" +
        "MOV result.color, tint;\n" +
        "END\n";
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_PROGRAMS_ARB, [1, 2])
        .call("BIND_PROGRAM_ARB", GL.VERTEX_PROGRAM_ARB, 1)
        .programStringARB(GL.VERTEX_PROGRAM_ARB, vertexProgram)
        .programParameterARB(ARB_PARAMETER_LOCAL, GL.VERTEX_PROGRAM_ARB, 0,
            [7, 7, 7, 7])
        .call("BIND_PROGRAM_ARB", GL.FRAGMENT_PROGRAM_ARB, 2)
        .programStringARB(GL.FRAGMENT_PROGRAM_ARB, fragmentProgram)
        .programParameterARB(ARB_PARAMETER_LOCAL, GL.FRAGMENT_PROGRAM_ARB, 0,
            [0.25, 0.5, 0.75, 1])
        .call("ENABLE", GL.VERTEX_PROGRAM_ARB)
        .call("ENABLE", GL.FRAGMENT_PROGRAM_ARB)
        .drawElements(GL.TRIANGLES, new Uint16Array([0, 1, 2]),
            GL.UNSIGNED_SHORT, {
                vertex: { size: 4, type: GL.FLOAT, stride: 0,
                          data: new Uint8Array(ALIAS_VERTICES.buffer) },
            });
    run(executor, stream);

    assert.deepStrictEqual(log.refusals || [], []);
    assert.strictEqual(log.draws.length, 1);
    const group = log.bindGroups.find(entry =>
        entry.descriptor.layout.index === 1);
    assert.deepStrictEqual(group.descriptor.entries.map(e => e.binding),
        [1, 2], "each stage's parameters get a binding of their own");

    // program.local is per-program: the vertex stage must not be handed the
    // fragment program's block, which is what one shared binding did.
    const count = 28 * 4;
    const localOf = binding => {
        const entry = group.descriptor.entries.find(e => e.binding === binding);
        return Array.from(new Float32Array(executor.uniformStaging.buffer,
            entry.resource.offset, count * 2).subarray(count, count + 4));
    };
    assert.deepStrictEqual(localOf(1), [7, 7, 7, 7]);
    assert.deepStrictEqual(localOf(2), [0.25, 0.5, 0.75, 1]);
});

test("glProgramEnvParameter4fv lands in env, not in local", () => {
    const { executor } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM)
        .programParameterARB(ARB_PARAMETER_ENV, GL.VERTEX_PROGRAM_ARB, 3,
            [0.25, 0.5, 0.75, 1])
        .programParameterARB(ARB_PARAMETER_LOCAL, GL.VERTEX_PROGRAM_ARB, 2,
            [9, 8, 7, 6]);
    run(executor, stream);

    const program = executor.current.shareGroup.arbPrograms.get(1);
    assert.deepStrictEqual([...program.env.subarray(12, 16)],
        [0.25, 0.5, 0.75, 1]);
    assert.deepStrictEqual([...program.local.subarray(8, 12)], [9, 8, 7, 6]);
    assert.deepStrictEqual([...program.local.subarray(12, 16)], [0, 0, 0, 0],
        "the env write must not have gone to local as well");
});

test("glProgramEnvParameters4fv writes the whole run", () => {
    const { executor } = newExecutor();
    const values = [];
    for (let i = 0; i < 12; ++i) values.push(i + 1);
    run(executor, boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM)
        .programParameterARB(ARB_PARAMETER_ENV, GL.VERTEX_PROGRAM_ARB, 1, values));
    const program = executor.current.shareGroup.arbPrograms.get(1);
    assert.deepStrictEqual([...program.env.subarray(4, 16)], values,
        "the count word is honoured, not ignored after the first parameter");
});

test("an env parameter set before the program compiles is accepted", () => {
    const { executor, log } = newExecutor();
    run(executor, boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programParameterARB(ARB_PARAMETER_ENV, GL.VERTEX_PROGRAM_ARB, 0,
            [1, 2, 3, 4]));
    assert.deepStrictEqual(log.refusals || [], [],
        "ARB environment parameters are context state, not program state");
    assert.deepStrictEqual(
        [...executor.current.shareGroup.arbPrograms.get(1).env.subarray(0, 4)],
        [1, 2, 3, 4]);
});

test("glGetProgramEnvParameterfv reads back what was written", () => {
    const { executor } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM)
        .programParameterARB(ARB_PARAMETER_ENV, GL.VERTEX_PROGRAM_ARB, 5,
            [1.5, 2.5, 3.5, 4.5]);
    const answer = stream.queryProgramParameterARB(ARB_PARAMETER_ENV,
        GL.VERTEX_PROGRAM_ARB, 5);
    run(executor, stream);

    assert.strictEqual(answer.view.getUint32(12, true), 1, "status is OK");
    assert.deepStrictEqual([24, 28, 32, 36].map(at =>
        answer.view.getFloat32(at, true)), [1.5, 2.5, 3.5, 4.5]);
});

test("glGetProgramStringARB returns the text at the offset the guest reads", () => {
    const { executor } = newExecutor();
    const stream = boundARBProgram(executor,
        new GLStream().makeCurrent(1, 0, 0, 64, 64))
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM);
    const answer = stream.queryProgramStringARB(GL.VERTEX_PROGRAM_ARB,
        ARB_VERTEX_PROGRAM.length);
    run(executor, stream);

    assert.strictEqual(answer.view.getUint32(8, true), 1, "status is OK");
    assert.strictEqual(answer.view.getUint32(12, true), ARB_VERTEX_PROGRAM.length);
    let text = "";
    for (let i = 0; i < ARB_VERTEX_PROGRAM.length; ++i)
        text += String.fromCharCode(answer.bytes[24 + i]);
    assert.strictEqual(text, ARB_VERTEX_PROGRAM);
});

test("glGetShaderInfoLog reaches the guest's buffer whole", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_SHADER", 1, GL.FRAGMENT_SHADER)
        .shaderSource(1, "void main() { this is not GLSL }")
        .call("COMPILE_SHADER", 1);
    const status = stream.queryObjectiv(1, 1, GL.COMPILE_STATUS);
    const answer = stream.queryObjectLog(1, 1, 256);
    run(executor, stream);

    assert.strictEqual(status.view.getUint32(12, true), 1, "the query answered");
    assert.strictEqual(status.view.getUint32(16, true), 0, "it did not compile");
    assert.strictEqual(answer.view.getUint32(12, true), 1, "log status is OK");
    const length = answer.view.getUint32(16, true);
    let text = "";
    for (let i = 0; i < length; ++i)
        text += String.fromCharCode(answer.bytes[24 + i]);
    assert.strictEqual(text,
        executor.current.shareGroup.shaders.get(1).compiled.log,
        "the log starts at 24 -- four bytes earlier and its first word is lost");
    assert.ok(length > 0, "a failed compile says why");
    assert.strictEqual(answer.bytes[24 + length], 0, "and is NUL-terminated");
});

test("glGetActiveUniform's name, size and type land in the right words", () => {
    const { executor } = newExecutor();
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("CREATE_PROGRAM", 1)
        .call("CREATE_SHADER", 2, GL.VERTEX_SHADER)
        .shaderSource(2, "uniform vec4 tint;\nvoid main() {\n" +
            "  gl_Position = gl_Vertex + tint;\n}\n")
        .call("COMPILE_SHADER", 2)
        .call("CREATE_SHADER", 3, GL.FRAGMENT_SHADER)
        .shaderSource(3, "void main() { gl_FragColor = vec4(1.0); }\n")
        .call("COMPILE_SHADER", 3)
        .call("ATTACH_SHADER", 1, 2)
        .call("ATTACH_SHADER", 1, 3)
        .call("LINK_PROGRAM", 1);
    const answer = stream.queryActive(1, 1, 0, 64);
    run(executor, stream);

    assert.strictEqual(answer.view.getUint32(16, true), 1, "status is OK");
    const length = answer.view.getUint32(20, true);
    let name = "";
    for (let i = 0; i < length; ++i)
        name += String.fromCharCode(answer.bytes[40 + i]);
    assert.strictEqual(name, "tint");
    assert.strictEqual(answer.view.getUint32(24, true), 1, "size is one element");
    assert.strictEqual(answer.view.getUint32(28, true), GL.FLOAT_VEC4);
});

test("variable payloads survive a non-zero batch byteOffset", () => {
    const { executor } = newExecutor();
    const bufferBytes = Uint8Array.from([11, 22, 33, 44, 55, 66, 77, 88]);
    const stream = new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .names(GLFN.GEN_BUFFERS, [4])
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 4)
        .bufferData(GL.ARRAY_BUFFER, bufferBytes.length, GL.STATIC_DRAW,
            bufferBytes)
        .names(GLFN.GEN_TEXTURES, [5])
        .call("BIND_TEXTURE", GL.TEXTURE_2D, 5)
        .texImage2D(GL.TEXTURE_2D, 0, GL.RGBA, 1, 1, GL.RGBA,
            GL.UNSIGNED_BYTE, Uint8Array.from([1, 2, 3, 4]))
        .call("CREATE_SHADER", 7, GL.VERTEX_SHADER)
        .shaderSource(7, "void main() { gl_Position = gl_Vertex; }")
        .call("COMPILE_SHADER", 7);
    boundARBProgram(executor, stream)
        .programStringARB(GL.VERTEX_PROGRAM_ARB, ARB_VERTEX_PROGRAM);
    stream.queryString(GL.RENDERER, 64);

    // PCI batches are views into the emulator's RAM and therefore normally
    // have a non-zero byteOffset.  Unit fixtures used to start at byte zero,
    // hiding a double-subtraction that corrupted every variable-size payload.
    const encoded = stream.bytes();
    const guestRAM = new Uint8Array(encoded.byteLength + 160);
    guestRAM.set(encoded, 96);
    executor.submit(guestRAM.subarray(96, 96 + encoded.byteLength), {});

    const group = executor.current.shareGroup;
    assert.deepStrictEqual([...group.buffers.get(4).shadow], [...bufferBytes]);
    assert.deepStrictEqual([...group.textures.get(5).levels[0][0].pixels],
        [1, 2, 3, 4]);
    assert.ok(group.shaders.get(7).compiled.ok,
        group.shaders.get(7).compiled.log);
    assert.strictEqual(group.arbPrograms.get(1).source, ARB_VERTEX_PROGRAM);
    assert.ok(group.arbPrograms.get(1).compiled.ok);
    assert.ok(new TextDecoder().decode(guestRAM).includes("v86 WebGPU bridge"),
        "variable-size query output is written into the submitted view");
});

/* ---- draw hot-path regressions ---- */

test("packed indexed draws discard unused prefixes and sparse gaps", () => {
    for (const source of [[60002, 60000, 60001], [60000, 2, 40000, 60000]]) {
        const { executor, log } = newExecutor();
        run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64));
        const values = new Float32Array(60003 * 3);
        for (const i of source) values.set([i, i + 1, i + 2], i * 3);
        let request;
        executor.issueDraw = r => { request = r; };
        executor.drawPacked(GL.TRIANGLES, source.length, {
            vertex: { size: 3, type: GL.FLOAT, data: new Uint8Array(values.buffer) },
        }, { type: GL.UNSIGNED_INT, data: new Uint8Array(new Uint32Array(source).buffer) });
        assert.ok(request);
        const vb = request.buffers[0];
        const uploaded = new Float32Array(vb.gpuBuffer.storage, vb.baseOffset);
        assert.deepStrictEqual(Array.from(request.index.source, i =>
            uploaded[i * vb.stride / 4]), source);
        assert.ok(log.bufferWrites.reduce((n, w) => n + w.size, 0) < 256,
            "a three-vertex draw must not upload 60003 vertices");
    }
});

function normalVBOScene(executor) {
    const data = new Uint8Array(3 * 16);
    const view = new DataView(data.buffer);
    for (let i = 0; i < 3; ++i) {
        view.setFloat32(i * 16, i - 1, true);
        data.set([127, 0, 129], i * 16 + 12);
    }
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64)
        .call("BIND_BUFFER", GL.ARRAY_BUFFER, 10)
        .bufferData(GL.ARRAY_BUFFER, data.length, GL.STATIC_DRAW, data)
        .pointerVBO("VERTEX_POINTER_VBO", 3, GL.FLOAT, 16, 0)
        .pointerVBO("NORMAL_POINTER_VBO", 3, GL.BYTE, 16, 12)
        .call("ENABLE_CLIENT_STATE", GL.VERTEX_ARRAY)
        .call("ENABLE_CLIENT_STATE", GL.NORMAL_ARRAY)
        .call("ENABLE", GL.LIGHTING)
        .call("BIND_BUFFER", GL.ELEMENT_ARRAY_BUFFER, 11)
        .bufferData(GL.ELEMENT_ARRAY_BUFFER, 8, GL.STATIC_DRAW,
            new Uint8Array(new Uint16Array([99, 0, 1, 2]).buffer)));
}

test("only incompatible VBO attributes are converted and unchanged draws reuse them", () => {
    const { executor, log } = newExecutor();
    normalVBOScene(executor);
    const s = executor.current;
    const vb = s.shareGroup.buffers.get(10);
    const eb = s.shareGroup.buffers.get(11);
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3,
        { type: GL.UNSIGNED_SHORT, bufferOffset: 2 });
    const converted = [...vb.convertedAttributes.values()][0].gpuBuffer;
    assert.deepStrictEqual(Array.from(new Float32Array(converted.storage, 0, 3)),
        [1, 0, -1], "conventional signed-byte normals are normalized");
    const calls = log.draws[0].pass.calls;
    assert.ok(calls.some(c => c[0] === "vertexBuffer" && c[2] === vb.gpuBuffer));
    assert.ok(calls.some(c => c[0] === "vertexBuffer" && c[2] === converted));
    assert.ok(calls.some(c => c[0] === "indexBuffer" && c[1] === eb.gpuBuffer &&
        c[2] === "uint16" && c[3] === 2));
    const writes = log.bufferWrites.length;
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3,
        { type: GL.UNSIGNED_SHORT, bufferOffset: 2 });
    // Constant current attributes still have a tiny per-draw instance upload.
    assert.ok(log.bufferWrites.slice(writes).every(w =>
        w.buffer === executor.vertexRing && w.size <= 32));
    assert.strictEqual(log.bufferWrites.filter(w => w.buffer === converted).length, 1);
});

test("VBO and direct EBO mutations preserve already encoded draw snapshots", () => {
    const { executor } = newExecutor();
    normalVBOScene(executor);
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3,
        { type: GL.UNSIGNED_SHORT, bufferOffset: 2 });
    const vb = executor.current.shareGroup.buffers.get(10);
    const eb = executor.current.shareGroup.buffers.get(11);
    const oldNormal = [...vb.convertedAttributes.values()][0].gpuBuffer;
    const oldIndex = eb.gpuBuffer;
    executor.bufferSubData(GL.ARRAY_BUFFER, 12, new Uint8Array([0, 127, 0]));
    executor.bufferSubData(GL.ELEMENT_ARRAY_BUFFER, 2,
        new Uint8Array(new Uint16Array([2, 1, 0]).buffer));
    assert.notStrictEqual(oldIndex, eb.gpuBuffer);
    assert.deepStrictEqual(Array.from(new Uint16Array(oldIndex.storage, 2, 3)), [0, 1, 2]);
    assert.strictEqual(oldNormal.destroyed, false);
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3,
        { type: GL.UNSIGNED_SHORT, bufferOffset: 2 });
    const nextNormal = [...vb.convertedAttributes.values()][0].gpuBuffer;
    assert.notStrictEqual(oldNormal, nextNormal);
    assert.deepStrictEqual(Array.from(new Float32Array(nextNormal.storage, 0, 3)), [0, 1, 0]);
    executor.flushFrame();
    assert.strictEqual(oldNormal.destroyed, true);
    assert.strictEqual(oldIndex.destroyed, true);
    run(executor, new GLStream().names(GLFN.DELETE_BUFFERS, [10]));
    assert.strictEqual(executor.convertedVertexBytes, 0);
    assert.strictEqual(executor.convertedVertexCache.size, 0);
});

test("conversion keys include normalization and component defaults and reset releases them", () => {
    const { executor } = newExecutor();
    normalVBOScene(executor);
    const buffer = executor.current.shareGroup.buffers.get(10);
    const array = executor.current.arrays.normal;
    const a = executor.convertVertexAttribute(buffer, array, 4, true);
    const b = executor.convertVertexAttribute(buffer, array, 3, false);
    assert.deepStrictEqual(Array.from(new Float32Array(a.storage, 0, 4)), [1, 0, -1, 1]);
    assert.deepStrictEqual(Array.from(new Float32Array(b.storage, 0, 3)), [127, 0, -127]);
    executor.resetForReplay();
    assert.strictEqual(a.destroyed, true);
    assert.strictEqual(b.destroyed, true);
    assert.strictEqual(executor.convertedVertexBytes, 0);
});

test("uint32 list indices are direct while strips and polygon edges retain CPU indices", () => {
    const { executor } = newExecutor();
    normalVBOScene(executor);
    executor.bufferData(GL.ELEMENT_ARRAY_BUFFER, 12, GL.STATIC_DRAW,
        new Uint8Array(new Uint32Array([0, 1, 2]).buffer));
    let request;
    executor.issueDraw = r => { request = r; };
    const info = { type: GL.UNSIGNED_INT, bufferOffset: 0 };
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3, info);
    assert.strictEqual(request.index.buffer, executor.bufferFor(GL.ELEMENT_ARRAY_BUFFER).gpuBuffer);
    assert.strictEqual(request.index.format, "uint32");
    executor.drawFromBuffers(GL.TRIANGLE_STRIP, 0, 3, info);
    assert.deepStrictEqual(Array.from(request.index.source), [0, 1, 2]);
    run(executor, new GLStream().call("POLYGON_MODE", GL.FRONT_AND_BACK, GL.LINE));
    executor.drawFromBuffers(GL.TRIANGLES, 0, 3, info);
    assert.deepStrictEqual(Array.from(request.index.source), [0, 1, 2]);
});

test("uniform snapshots reuse unchanged bytes without overwriting earlier draws", () => {
    const { executor, log } = newExecutor();
    const values = new Uint8Array([1, 2, 3, 4]);
    const a = executor.writeUniformSnapshot("test", values);
    assert.strictEqual(executor.writeUniformSnapshot("test", values), a);
    assert.strictEqual(log.bufferWrites.length, 1);
    values[0] = 9;
    const b = executor.writeUniformSnapshot("test", values);
    assert.notStrictEqual(a.offset, b.offset);
    assert.strictEqual(new Uint8Array(a.buffer.storage)[a.offset], 1);
    assert.strictEqual(new Uint8Array(b.buffer.storage)[b.offset], 9);
    executor.finishFrame(false);
    const writes = log.bufferWrites.length;
    executor.writeUniformSnapshot("test", values);
    assert.strictEqual(log.bufferWrites.length, writes + 1,
        "rewound ring slices are never reused from the preceding frame");
});

test("bind group cache keys include pipeline, resource identity and buffer offset", () => {
    const { executor, log, host } = newExecutor();
    const descriptor = { vertex: { module: { code: "@group(1) @binding(0)" } } };
    const a = host.device.createRenderPipeline(descriptor);
    const b = host.device.createRenderPipeline(descriptor);
    const entries = offset => [{ binding: 0, resource: {
        buffer: executor.uniformRing, offset, size: 256,
    } }];
    const first = executor.cachedBindGroup(a, 1, entries(0));
    assert.strictEqual(executor.cachedBindGroup(a, 1, entries(0)), first);
    assert.notStrictEqual(executor.cachedBindGroup(a, 1, entries(256)), first);
    assert.notStrictEqual(executor.cachedBindGroup(b, 1, entries(0)), first);
    assert.strictEqual(log.bindGroups.length, 3);
});

test("inactive uniform blocks allocate and upload nothing", () => {
    const { executor, log, host } = newExecutor();
    run(executor, new GLStream().makeCurrent(1, 0, 0, 64, 64));
    const pipeline = host.device.createRenderPipeline({});
    pipeline.glActiveBindings = new Set();
    const shaders = { kind: "ff", stateFields: [], textures: [],
        wgslVertex: "", wgslFragment: "" };
    const writes = log.bufferWrites.length;
    assert.deepStrictEqual(executor.buildBindGroups(pipeline, shaders), []);
    assert.strictEqual(log.bufferWrites.length, writes);
    assert.strictEqual(executor.uniformCursor, 0);
});

/* ---- report ---- */

(async () => {
    for (const [name, fn] of asyncTests) {
        try {
            await fn();
            ++passed;
        } catch (error) {
            failures.push([name, error]);
        }
    }
    for (const [name, error] of failures)
        console.error("FAIL: " + name + "\n    " + (error && error.message));
    console.log(passed + " passed, " + failures.length + " failed");
    process.exit(failures.length ? 1 : 0);
})();
