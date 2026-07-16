"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const GLFN_QUERY_INTEGER = 193;
const V86GL_QUERY_HOST_CAPABILITIES = 0x76380001;
const V86GL_HOST_CAP_WEBGL2 = 0x00000001;
const V86GL_HOST_CAP_COLOR_BUFFER_FLOAT = 0x00000002;
const V86GL_HOST_CAP_TEXTURE_FLOAT_LINEAR = 0x00000004;
const V86GL_HOST_CAP_ANISOTROPY = 0x00000008;

const GL_MAX_TEXTURE_SIZE = 0x0D33;
const GL_MAX_3D_TEXTURE_SIZE = 0x8073;
const GL_MAX_CUBE_MAP_TEXTURE_SIZE = 0x851C;
const GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FF;
const GL_MAX_RENDERBUFFER_SIZE = 0x84E8;
const GL_MAX_DRAW_BUFFERS = 0x8824;
const GL_MAX_VERTEX_ATTRIBS = 0x8869;
const GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872;
const GL_MAX_FRAGMENT_UNIFORM_COMPONENTS = 0x8B49;
const GL_MAX_VERTEX_UNIFORM_COMPONENTS = 0x8B4A;
const GL_MAX_VARYING_FLOATS = 0x8B4B;
const GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
const GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
const GL_MAX_COLOR_ATTACHMENTS = 0x8CDF;
const GL_PROGRAM_ERROR_POSITION_ARB = 0x864B;

const WEBGL_MAX_VERTEX_UNIFORM_VECTORS = 0x8DFB;
const WEBGL_MAX_VARYING_VECTORS = 0x8DFC;
const WEBGL_MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD;

function makeModule(fallbackValue = -1) {
    let nextPtr = 1024;
    const calls = [];
    return {
        calls,
        HEAPU8: new Uint8Array(16 * 1024),
        _malloc(size) {
            const ptr = nextPtr;
            nextPtr += Math.max(4, size);
            return ptr;
        },
        _free() {},
        _v86glResize() {},
        _v86gl_glQueryInteger(pname, ptr) {
            calls.push(pname);
            new DataView(this.HEAPU8.buffer).setInt32(ptr, fallbackValue, true);
            return 1;
        },
    };
}

function makeWebGL2(optionalExtensions = {}) {
    const anisotropy = optionalExtensions.anisotropy ? {
        MAX_TEXTURE_MAX_ANISOTROPY_EXT: GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,
    } : null;
    const extensions = {
        EXT_color_buffer_float: optionalExtensions.colorBufferFloat ? {} : null,
        OES_texture_float_linear: optionalExtensions.floatLinear ? {} : null,
        EXT_texture_filter_anisotropic: anisotropy,
        WEBKIT_EXT_texture_filter_anisotropic: null,
        MOZ_EXT_texture_filter_anisotropic: null,
    };
    const values = new Map([
        [GL_MAX_TEXTURE_SIZE, 8192],
        [GL_MAX_3D_TEXTURE_SIZE, 2048],
        [GL_MAX_CUBE_MAP_TEXTURE_SIZE, 4096],
        [GL_MAX_RENDERBUFFER_SIZE, 8192],
        [GL_MAX_DRAW_BUFFERS, 8],
        [GL_MAX_COLOR_ATTACHMENTS, 8],
        [GL_MAX_VERTEX_ATTRIBS, 16],
        [GL_MAX_TEXTURE_IMAGE_UNITS, 16],
        [GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 16],
        [GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 32],
        [WEBGL_MAX_VERTEX_UNIFORM_VECTORS, 256],
        [WEBGL_MAX_FRAGMENT_UNIFORM_VECTORS, 224],
        [WEBGL_MAX_VARYING_VECTORS, 15],
        [GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, 16],
    ]);
    return {
        texImage3D() {},
        drawBuffers() {},
        getExtension(name) {
            return extensions[name] || null;
        },
        getParameter(pname) {
            if (!values.has(pname)) {
                throw new Error(`unexpected WebGL pname 0x${pname.toString(16)}`);
            }
            return values.get(pname);
        },
    };
}

function makeBridge(module, webglContext) {
    const canvas = {
        width: 640,
        height: 480,
        style: {},
        parentElement: { getElementsByTagName() { return [canvas]; } },
    };
    return globalThis.installV86GLNetworkBridge(
        { add_listener() {} }, canvas, { gl4es: module, webglContext });
}

function query(renderer, pname) {
    const payload = Buffer.alloc(16);
    payload.writeUInt32LE(pname >>> 0, 0);
    renderer.glCall(GLFN_QUERY_INTEGER, payload);
    assert.equal(payload.readUInt32LE(4), 1,
        `query 0x${pname.toString(16)} must complete synchronously`);
    return payload.readInt32LE(8);
}

function testFullHostProfile() {
    const module = makeModule();
    const gl = makeWebGL2({
        colorBufferFloat: true,
        floatLinear: true,
        anisotropy: true,
    });
    const bridge = makeBridge(module, gl);
    const expectedBits = V86GL_HOST_CAP_WEBGL2 |
        V86GL_HOST_CAP_COLOR_BUFFER_FLOAT |
        V86GL_HOST_CAP_TEXTURE_FLOAT_LINEAR |
        V86GL_HOST_CAP_ANISOTROPY;

    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES), expectedBits);
    assert.equal(query(bridge.renderer, GL_MAX_TEXTURE_SIZE), 8192);
    assert.equal(query(bridge.renderer, GL_MAX_3D_TEXTURE_SIZE), 2048);
    assert.equal(query(bridge.renderer, GL_MAX_CUBE_MAP_TEXTURE_SIZE), 4096);
    assert.equal(query(bridge.renderer, GL_MAX_TEXTURE_IMAGE_UNITS), 8);
    assert.equal(query(bridge.renderer, GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS), 8);
    assert.equal(query(bridge.renderer, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS), 8);
    assert.equal(query(bridge.renderer, GL_MAX_VERTEX_ATTRIBS), 16);
    assert.equal(query(bridge.renderer, GL_MAX_DRAW_BUFFERS), 4);
    assert.equal(query(bridge.renderer, GL_MAX_COLOR_ATTACHMENTS), 4);
    assert.equal(query(bridge.renderer, GL_MAX_VERTEX_UNIFORM_COMPONENTS), 1024);
    assert.equal(query(bridge.renderer, GL_MAX_FRAGMENT_UNIFORM_COMPONENTS), 896);
    assert.equal(query(bridge.renderer, GL_MAX_VARYING_FLOATS), 60);
    assert.equal(query(bridge.renderer, GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT), 16);
    assert.deepEqual(module.calls, [], "host capability queries must bypass gl4es guesses");

    assert.equal(query(bridge.renderer, GL_PROGRAM_ERROR_POSITION_ARB), -1);
    assert.deepEqual(module.calls, [GL_PROGRAM_ERROR_POSITION_ARB],
        "non-capability state still uses the gl4es synchronous query path");
}

function testConservativeOptionalProfile() {
    const module = makeModule();
    const bridge = makeBridge(module, makeWebGL2());

    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES),
        V86GL_HOST_CAP_WEBGL2,
        "missing optional WebGL extensions must not be advertised");
    assert.equal(query(bridge.renderer, GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT), 1,
        "anisotropy falls back to the non-anisotropic baseline");
    assert.deepEqual(module.calls, []);
}

function testPartialProfileNeedsCompleteFloatBacking() {
    const module = makeModule();
    const bridge = makeBridge(module, makeWebGL2({ colorBufferFloat: true }));

    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES),
        V86GL_HOST_CAP_WEBGL2 | V86GL_HOST_CAP_COLOR_BUFFER_FLOAT,
        "partial float backing stays distinguishable from the complete profile");
}

function testRendererRecreationRefreshesCapabilities() {
    let context = makeWebGL2({
        colorBufferFloat: true,
        floatLinear: true,
        anisotropy: true,
    });
    const module = makeModule();
    const canvas = {
        width: 640,
        height: 480,
        style: {},
        parentElement: { getElementsByTagName() { return [canvas]; } },
    };
    const bridge = globalThis.installV86GLNetworkBridge(
        { add_listener() {} }, canvas, {
            gl4es: module,
            webglContext() { return context; },
        });

    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES), 0xF);
    context = makeWebGL2();
    bridge.setGL4ES(makeModule(), bridge.rendererGeneration);
    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES),
        V86GL_HOST_CAP_WEBGL2,
        "a fresh renderer must not reuse the destroyed context's extension cache");
}

function testNoContextFallsBackToGl4es() {
    const module = makeModule(777);
    const bridge = makeBridge(module, null);

    assert.equal(query(bridge.renderer, V86GL_QUERY_HOST_CAPABILITIES), 0,
        "an unavailable renderer produces an empty optional capability profile");
    assert.equal(query(bridge.renderer, GL_MAX_TEXTURE_SIZE), 777);
    assert.deepEqual(module.calls, [GL_MAX_TEXTURE_SIZE]);
}

testFullHostProfile();
testConservativeOptionalProfile();
testPartialProfileNeedsCompleteFloatBacking();
testRendererRecreationRefreshesCapabilities();
testNoContextFallsBackToGl4es();
console.log("v86_network_bridge_capabilities_test: ok");
