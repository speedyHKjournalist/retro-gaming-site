"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const GLFN_COMPRESSED_TEX_IMAGE_2D = 104;
const GLFN_TEX_IMAGE_3D = 105;
const GLFN_PROGRAM_STRING_ARB = 185;
const GLFN_QUERY_PROGRAM_IV_ARB = 188;
const GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
const GL_TEXTURE_3D = 0x806F;
const GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;
const GL_RGBA = 0x1908;
const GL_UNSIGNED_BYTE = 0x1401;
const GL_VERTEX_PROGRAM_ARB = 0x8620;
const GL_PROGRAM_FORMAT_ASCII_ARB = 0x8875;
const GL_MAX_PROGRAM_ENV_PARAMETERS_ARB = 0x88B5;

function makeModule() {
    let nextPtr = 1024;
    const calls = [];
    return {
        calls,
        HEAPU8: new Uint8Array(64 * 1024),
        _malloc(size) {
            const ptr = nextPtr;
            nextPtr += Math.max(size, 4);
            return ptr;
        },
        _free() {},
        _v86glResize() {},
        _v86gl_glCompressedTexImage2D(target, level, internalFormat,
                                      width, height, border, imageSize, ptr) {
            calls.push(["dxt", target, level, internalFormat, width, height,
                border, Array.from(this.HEAPU8.subarray(ptr, ptr + imageSize))]);
        },
        _v86gl_glTexImage3D(target, level, internalFormat, width, height,
                            depth, border, format, type, ptr) {
            calls.push(["volume", target, level, internalFormat, width, height,
                depth, border, format, type,
                Array.from(this.HEAPU8.subarray(ptr, ptr + 32))]);
        },
        _v86gl_glProgramStringARB(target, format, length, ptr) {
            calls.push(["program", target, format,
                Buffer.from(this.HEAPU8.subarray(ptr, ptr + length)).toString("ascii")]);
        },
        _v86gl_glGetProgramivARB(target, pname, ptr) {
            assert.equal(target, GL_VERTEX_PROGRAM_ARB);
            assert.equal(pname, GL_MAX_PROGRAM_ENV_PARAMETERS_ARB);
            new DataView(this.HEAPU8.buffer).setInt32(ptr, 96, true);
            calls.push(["queryProgram"]);
            return 1;
        },
    };
}

function makeBridge(module) {
    const canvas = {
        width: 640,
        height: 480,
        style: {},
        parentElement: { getElementsByTagName() { return [canvas]; } },
    };
    return globalThis.installV86GLNetworkBridge(
        { add_listener() {} }, canvas, { gl4es: module });
}

function compressedCubePayload(bytes) {
    const payload = Buffer.alloc(28 + bytes.length);
    payload.writeUInt32LE(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0);
    payload.writeInt32LE(0, 4);
    payload.writeUInt32LE(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 8);
    payload.writeInt32LE(4, 12);
    payload.writeInt32LE(4, 16);
    payload.writeInt32LE(0, 20);
    payload.writeUInt32LE(bytes.length, 24);
    bytes.copy(payload, 28);
    return payload;
}

function volumePayload(bytes) {
    const payload = Buffer.alloc(40 + bytes.length);
    payload.writeUInt32LE(GL_TEXTURE_3D, 0);
    payload.writeInt32LE(0, 4);
    payload.writeInt32LE(GL_RGBA, 8);
    payload.writeInt32LE(2, 12);
    payload.writeInt32LE(2, 16);
    payload.writeInt32LE(2, 20);
    payload.writeInt32LE(0, 24);
    payload.writeUInt32LE(GL_RGBA, 28);
    payload.writeUInt32LE(GL_UNSIGNED_BYTE, 32);
    payload.writeUInt32LE(bytes.length, 36);
    bytes.copy(payload, 40);
    return payload;
}

function programPayload(source) {
    const bytes = Buffer.from(source, "ascii");
    const payload = Buffer.alloc(16 + bytes.length);
    payload.writeUInt32LE(GL_VERTEX_PROGRAM_ARB, 0);
    payload.writeUInt32LE(GL_PROGRAM_FORMAT_ASCII_ARB, 4);
    payload.writeInt32LE(bytes.length, 8);
    bytes.copy(payload, 16);
    return payload;
}

function main() {
    const module = makeModule();
    const bridge = makeBridge(module);
    const dxt = Buffer.from(Array.from({ length: 16 }, (_, i) => i + 1));
    const volume = Buffer.from(Array.from({ length: 32 }, (_, i) => 0x80 + i));
    const arb = "!!ARBvp1.0\nMOV result.position, vertex.position;\nEND";

    bridge.renderer.glCall(GLFN_COMPRESSED_TEX_IMAGE_2D, compressedCubePayload(dxt));
    bridge.renderer.glCall(GLFN_TEX_IMAGE_3D, volumePayload(volume));
    bridge.renderer.glCall(GLFN_PROGRAM_STRING_ARB, programPayload(arb));

    const query = Buffer.alloc(16);
    query.writeUInt32LE(GL_VERTEX_PROGRAM_ARB, 0);
    query.writeUInt32LE(GL_MAX_PROGRAM_ENV_PARAMETERS_ARB, 4);
    bridge.renderer.glCall(GLFN_QUERY_PROGRAM_IV_ARB, query);
    assert.equal(query.readUInt32LE(8), 1, "synchronous query must complete");
    assert.equal(query.readInt32LE(12), 96, "shader cap query must copy its result");

    assert.deepEqual(module.calls[0].slice(1, 7), [
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
        4, 4, 0,
    ]);
    assert.deepEqual(module.calls[0][7], Array.from(dxt));
    assert.deepEqual(module.calls[1].at(-1), Array.from(volume));
    assert.equal(module.calls[2][3], arb);
    assert.equal(module.calls[3][0], "queryProgram");
    console.log("v86_network_bridge_extended_features_test: ok");
}

main();
