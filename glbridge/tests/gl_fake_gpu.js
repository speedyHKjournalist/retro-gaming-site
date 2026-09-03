// A minimal fake WebGPU device for the OpenGL executor's Node tests.
//
// It records what was asked of it rather than rendering: the point of the
// executor suite is to check that a GL command stream produces the right
// pipeline state, the right vertex layout and the right bind groups, none of
// which needs a GPU. The browser suites cover what pixels come out.

"use strict";

class FakeBuffer {
    constructor(descriptor) {
        this.descriptor = descriptor;
        this.size = descriptor.size;
        this.destroyed = false;
        this.writes = [];
        // Persistent, so a test can seed what a readback will find -- a fresh
        // buffer per getMappedRange would make every mapped result zero and
        // hide the difference between "read a zero" and "read nothing".
        this.storage = new ArrayBuffer(descriptor.size);
    }
    destroy() { this.destroyed = true; }
    mapAsync() { return Promise.resolve(); }
    getMappedRange() { return this.storage; }
    unmap() {}
}

class FakeTexture {
    constructor(descriptor) {
        this.descriptor = descriptor;
        this.destroyed = false;
        this.views = [];
    }
    createView(options) {
        const view = { texture: this, options: options || {} };
        this.views.push(view);
        return view;
    }
    destroy() { this.destroyed = true; }
}

class FakeRenderPass {
    constructor(descriptor, log) {
        this.descriptor = descriptor;
        this.log = log;
        this.ended = false;
        log.passes.push(this);
        this.calls = [];
    }
    setPipeline(pipeline) { this.pipeline = pipeline; this.calls.push(["pipeline", pipeline]); }
    setBindGroup(index, group) { this.calls.push(["bindGroup", index, group]); }
    setVertexBuffer(slot, buffer, offset) {
        this.calls.push(["vertexBuffer", slot, buffer, offset]);
    }
    setIndexBuffer(buffer, format, offset) {
        this.calls.push(["indexBuffer", buffer, format, offset]);
    }
    setViewport(...args) { this.viewport = args; }
    setScissorRect(...args) { this.scissor = args; }
    setStencilReference(value) { this.stencilReference = value; }
    setBlendConstant(value) { this.blendConstant = value; }
    beginOcclusionQuery(index) { this.calls.push(["beginQuery", index]); }
    endOcclusionQuery() { this.calls.push(["endQuery"]); }
    draw(count, instances, first) {
        this.log.draws.push({ pass: this, count, instances, first,
                              pipeline: this.pipeline });
        this.calls.push(["draw", count]);
    }
    drawIndexed(count, instances, firstIndex, baseVertex) {
        this.log.draws.push({ pass: this, count, instances, firstIndex,
                              baseVertex, indexed: true, pipeline: this.pipeline });
        this.calls.push(["drawIndexed", count]);
    }
    end() { this.ended = true; }
}

class FakeEncoder {
    constructor(log) { this.log = log; this.copies = []; }
    beginRenderPass(descriptor) { return new FakeRenderPass(descriptor, this.log); }
    copyTextureToTexture(...args) { this.copies.push(["t2t", ...args]); }
    copyTextureToBuffer(...args) { this.copies.push(["t2b", ...args]); }
    copyBufferToBuffer(...args) { this.copies.push(["b2b", ...args]); }
    resolveQuerySet(querySet, first, count, destination, offset) {
        this.log.queryResolves.push({ querySet, first, count, destination, offset });
        this.copies.push(["resolve", querySet, first, count]);
    }
    finish() { return { encoder: this }; }
}

class FakeDevice {
    constructor(log) {
        this.log = log;
        this.features = new Set(["texture-compression-bc"]);
        this.limits = {
            maxTextureDimension2D: 8192, maxTextureDimension3D: 2048,
            maxColorAttachments: 8, maxVertexBuffers: 8,
            maxVertexAttributes: 16, maxBindGroups: 4,
        };
        this.lost = new Promise(() => {});
        this.queue = {
            writeBuffer: (buffer, offset, data, dataOffset, size) => {
                log.bufferWrites.push({ buffer, offset, size });
            },
            writeTexture: (destination, data, layout, size) => {
                log.textureWrites.push({ destination, layout, size,
                                         byteLength: data.byteLength });
            },
            submit: buffers => { log.submits.push(buffers); },
            onSubmittedWorkDone: () => Promise.resolve(),
        };
    }
    createBuffer(descriptor) {
        const buffer = new FakeBuffer(descriptor);
        this.log.buffers.push(buffer);
        return buffer;
    }
    createTexture(descriptor) {
        const texture = new FakeTexture(descriptor);
        this.log.textures.push(texture);
        return texture;
    }
    createSampler(descriptor) {
        const sampler = { descriptor };
        this.log.samplers.push(sampler);
        return sampler;
    }
    createShaderModule(descriptor) {
        const module = { code: descriptor.code };
        this.log.modules.push(module);
        return module;
    }
    createRenderPipeline(descriptor) {
        const pipeline = {
            descriptor,
            getBindGroupLayout: index => {
                const code = (descriptor.vertex && descriptor.vertex.module.code || "") +
                    "\n" + (descriptor.fragment &&
                        descriptor.fragment.module.code || "");
                const bindings = new Set();
                const expression = /@group\((\d+)\)\s+@binding\((\d+)\)/g;
                for (const match of code.matchAll(expression))
                    if (Number(match[1]) === index) bindings.add(Number(match[2]));
                if (!bindings.size)
                    throw new Error("no bind group layout at index " + index);
                return { index, bindings };
            },
        };
        this.log.pipelines.push(pipeline);
        return pipeline;
    }
    createBindGroup(descriptor) {
        if (descriptor.layout && descriptor.layout.bindings) {
            for (const entry of descriptor.entries)
                if (!descriptor.layout.bindings.has(entry.binding))
                    throw new Error("binding " + entry.binding +
                        " is not declared by group " + descriptor.layout.index);
        }
        const group = { descriptor };
        this.log.bindGroups.push(group);
        return group;
    }
    createCommandEncoder() {
        const encoder = new FakeEncoder(this.log);
        this.log.encoders.push(encoder);
        return encoder;
    }
    createQuerySet(descriptor) {
        const set = { descriptor };
        this.log.querySets.push(set);
        return set;
    }
}

function createFakeHost(canvas) {
    const log = {
        buffers: [], textures: [], samplers: [], modules: [], pipelines: [],
        bindGroups: [], encoders: [], passes: [], draws: [],
        bufferWrites: [], textureWrites: [], submits: [],
        querySets: [], queryResolves: [],
        presented: 0,
    };
    const device = new FakeDevice(log);
    const host = {
        canvas: canvas || null,
        device,
        format: "bgra8unorm",
        limits: device.limits,
        deviceFeatures: { bc: true, float32Filterable: true,
                          float32Blendable: false, timestampQuery: false,
                          depthClipControl: false, clipDistances: false },
        context: {
            configure() {},
            getCurrentTexture() {
                ++log.presented;
                return new FakeTexture({ label: "swapchain" });
            },
        },
        initialize() { return Promise.resolve(host); },
        onDeviceLost() { return () => {}; },
        claimPresenter() { return 1; },
        canPresent(token) { return token === 1; },
        releasePresenter() {},
        resizeCanvas() { return false; },
    };
    return { host, log, device };
}

module.exports = { createFakeHost, FakeDevice, FakeTexture, FakeBuffer };
