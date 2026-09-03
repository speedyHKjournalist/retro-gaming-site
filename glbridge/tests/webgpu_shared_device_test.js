#!/usr/bin/env node
// One canvas, one GPUDevice -- across every guest graphics backend.
//
// A GPUCanvasContext belongs to whichever device last called configure() on
// it, and getCurrentTexture() then returns a texture only that device may use.
// The page loads the D3D8, D3D9/DDraw and OpenGL executors whether or not the
// guest uses them, and they all share #d3d_webgpu_canvas, so a backend that
// acquires its own device does not merely waste one: it makes every other
// backend's present pass fail validation with
//
//   [TextureView of Texture "...WebgpuSwapChainTexture..."] is associated with
//   [Device], and cannot be used with [Device]
//
// which is what OpenGL did in the guest while its own 45 unit tests passed,
// because each executor's tests only ever build one executor.

"use strict";

const assert = require("assert");
const gpuHost = require("../webgpu_host.js");

let passed = 0;
const failures = [];

function test(name, fn) {
    try { fn(); ++passed; } catch (error) { failures.push([name, error]); }
}

/* A fake just complete enough for three initialize() paths to run. */
function makeFake() {
    const log = { devices: [], configures: [], adapters: 0 };
    const makeDevice = features => {
        const device = {
            features: new Set(features),
            limits: { maxTextureDimension2D: 8192, maxTextureDimension3D: 2048,
                      maxColorAttachments: 8 },
            lost: new Promise(() => {}),
            queue: { writeBuffer() {}, writeTexture() {}, submit() {},
                     onSubmittedWorkDone: () => Promise.resolve() },
            createBuffer: descriptor => ({ descriptor, destroy() {},
                mapAsync: () => Promise.resolve(),
                getMappedRange: () => new ArrayBuffer(descriptor.size || 4),
                unmap() {} }),
            createTexture: descriptor => ({ descriptor, destroy() {},
                createView: () => ({ texture: descriptor }) }),
            createSampler: descriptor => ({ descriptor }),
            createShaderModule: descriptor => ({ code: descriptor.code,
                getCompilationInfo: async () => ({ messages: [] }) }),
            createCommandEncoder: () => ({
                beginRenderPass: () => ({ end() {}, setPipeline() {},
                    setBindGroup() {}, setVertexBuffer() {}, draw() {} }),
                copyTextureToBuffer() {}, copyBufferToBuffer() {},
                copyTextureToTexture() {}, resolveQuerySet() {},
                finish: () => ({}) }),
            createQuerySet: descriptor => ({ descriptor }),
            createBindGroup: descriptor => ({ descriptor }),
            createRenderPipeline: descriptor => ({ descriptor,
                getBindGroupLayout: () => ({}) }),
            pushErrorScope() {}, popErrorScope: () => Promise.resolve(null),
        };
        log.devices.push(device);
        return device;
    };
    const context = {
        configure(descriptor) { log.configures.push(descriptor); },
        getCurrentTexture: () => ({ width: 640, height: 480,
            createView: () => ({ swapchain: true }) }),
    };
    const gpu = {
        async requestAdapter() {
            ++log.adapters;
            return {
                features: new Set(["texture-compression-bc", "timestamp-query"]),
                limits: { maxTextureDimension2D: 8192 },
                async requestDevice(descriptor) {
                    return makeDevice((descriptor &&
                        descriptor.requiredFeatures) || []);
                },
            };
        },
        getPreferredCanvasFormat: () => "bgra8unorm",
    };
    const canvas = { width: 640, height: 480, getContext: () => context };
    return { gpu, canvas, context, log };
}

async function initAll() {
    const { gpu, canvas, log } = makeFake();
    gpuHost.reset(canvas);
    const built = [];
    // Load order matches game.html: D3D8, then D3D9/DDraw, then OpenGL.
    const d3d8 = require("../d3d8-webgpu/d3d8_executor.js");
    const d3d9 = require("../d3d9-webgpu/d3d9_executor.js");
    const gl = require("../gl-webgpu/gl_executor.js");
    const a = new d3d8.D3D8WebGPUExecutor(canvas, { gpu });
    const b = new d3d9.D3D9WebGPUExecutor(canvas, { gpu });
    const c = new gl.GLWebGPUExecutor(canvas, { gpu, hostOptions: { gpu } });
    for (const executor of [a, b, c]) {
        await executor.initialize();
        built.push(executor);
    }
    return { built, log, canvas };
}

const checks = [];
checks.push(initAll().then(({ built, log }) => {
    const [d3d8, d3d9, gl] = built;
    test("every backend on one canvas ends up on one GPUDevice", () => {
        assert.strictEqual(d3d9.device, d3d8.device,
            "D3D9 and D3D8 must share the device that configured the canvas");
        assert.strictEqual(gl.device, d3d8.device,
            "OpenGL's present pass uses the canvas texture; a second device " +
            "here is the swap-chain validation failure this test exists for");
    });
    test("only one device is ever requested", () => {
        assert.strictEqual(log.devices.length, 1,
            "a second requestDevice means a second owner of the canvas");
        assert.strictEqual(log.adapters, 1);
    });
    test("every canvas configure names the shared device", () => {
        assert.ok(log.configures.length >= 1, "the canvas must be configured");
        for (const descriptor of log.configures)
            assert.strictEqual(descriptor.device, d3d8.device);
    });
    test("the shared device carries the union of requested features", () => {
        // D3D8 alone used to request none, so whichever backend won the race
        // decided whether DXT textures worked for all of them.
        assert.ok(d3d8.device.features.has("texture-compression-bc"),
            "BCn must survive regardless of which backend initialised first");
    });
    test("the swap-chain usages every backend needs are configured once", () => {
        const usage = log.configures[0].usage;
        assert.ok(usage & 0x10, "RENDER_ATTACHMENT");
        assert.ok(usage & 0x01, "COPY_SRC -- D3D9 StretchRect off the back buffer");
        assert.ok(usage & 0x02, "COPY_DST");
        assert.ok(usage & 0x04, "TEXTURE_BINDING -- GL glCopyTexImage2D");
    });
}));

Promise.all(checks).catch(error => {
    failures.push(["harness", error]);
}).then(() => {
    for (const [name, error] of failures)
        console.error("FAIL: " + name + "\n    " + (error && error.message));
    console.log("webgpu_shared_device_test: " +
        (failures.length ? "FAILED" : "ok") +
        " (" + passed + " passed, " + failures.length + " failed)");
    process.exit(failures.length ? 1 : 0);
});
