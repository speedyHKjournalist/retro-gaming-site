"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const GLFN_DRAW_ELEMENTS = 40;
const CLIENT_ARRAY_MT_MAGIC = 0x544D4143;
const GL_TRIANGLES = 0x0004;
const GL_FLOAT = 0x1406;
const GL_UNSIGNED_SHORT = 0x1403;
const GL_TEXTURE0 = 0x84C0;

function clientArrayBlock(enabled, size, type, stride, bytes) {
    const data = bytes || Buffer.alloc(0);
    const block = Buffer.alloc(20 + data.length);
    block.writeUInt32LE(enabled ? 1 : 0, 0);
    block.writeInt32LE(size, 4);
    block.writeUInt32LE(type >>> 0, 8);
    block.writeInt32LE(stride, 12);
    block.writeUInt32LE(data.length, 16);
    data.copy(block, 20);
    return block;
}

function eightStageDrawElementsPayload(vertexBytes) {
    const indices = Buffer.from([0, 0, 1, 0, 2, 0]);
    const header = Buffer.alloc(28);
    header.writeUInt32LE(GL_TRIANGLES, 0);
    header.writeInt32LE(3, 4);
    header.writeUInt32LE(GL_UNSIGNED_SHORT, 8);
    header.writeUInt32LE(indices.length, 12);
    header.writeUInt32LE(CLIENT_ARRAY_MT_MAGIC, 16);
    header.writeUInt32LE(8, 20);
    header.writeUInt32LE(GL_TEXTURE0, 24);

    const textureBlocks = Array.from({ length: 8 }, (_, stage) =>
        clientArrayBlock(true, 2, GL_FLOAT, 8, Buffer.alloc(24, 0x31 + stage)));

    return Buffer.concat([
        header,
        indices,
        clientArrayBlock(true, 3, GL_FLOAT, 12, vertexBytes),
        clientArrayBlock(false, 4, GL_FLOAT, 0),
        clientArrayBlock(false, 3, GL_FLOAT, 0),
        ...textureBlocks,
    ]);
}

function main() {
    const memory = new WebAssembly.Memory({ initial: 1, maximum: 8 });
    const initialHeap = new Uint8Array(memory.buffer);
    const freed = [];
    let next = 65520;
    let growthCount = 0;
    let drawSeen = false;

    const module = {
        HEAPU8: initialHeap,
        _malloc(size) {
            const ptr = next;
            next += size;
            while (next > this.HEAPU8.length) {
                memory.grow(1);
                growthCount++;
                this.HEAPU8 = new Uint8Array(memory.buffer);
            }
            return ptr;
        },
        _free(ptr) {
            freed.push(ptr);
        },
        _v86glResize() {},
        _v86gl_glDrawElementsPackedMT(mode, count, indexType, indexPtr,
                                      texUnitCount, clientActiveTexture,
                                      hasSecondaryColor, hasFogCoord, metaPtr) {
            drawSeen = true;
            assert.equal(mode, GL_TRIANGLES);
            assert.equal(count, 3);
            assert.equal(indexType, GL_UNSIGNED_SHORT);
            assert.equal(texUnitCount, 8);
            assert.equal(clientActiveTexture, GL_TEXTURE0);
            assert.equal(hasSecondaryColor, 0);
            assert.equal(hasFogCoord, 0);
            assert.deepEqual(Array.from(this.HEAPU8.subarray(indexPtr, indexPtr + 6)),
                             [0, 0, 1, 0, 2, 0]);

            const meta = new DataView(this.HEAPU8.buffer, metaPtr, 11 * 16);
            const vertexPtr = meta.getInt32(12, true);
            assert.equal(meta.getInt32(0, true), 3);
            assert.equal(meta.getUint32(4, true), GL_FLOAT);
            assert.equal(meta.getInt32(8, true), 12);
            assert.equal(this.HEAPU8[vertexPtr], 0x5A);
            assert.equal(this.HEAPU8[vertexPtr + 69999], 0x5A);
            for (let stage = 0; stage < 8; stage++) {
                const texcoordPtr = meta.getInt32((3 + stage) * 16 + 12, true);
                assert.equal(this.HEAPU8[texcoordPtr], 0x31 + stage);
                assert.equal(this.HEAPU8[texcoordPtr + 23], 0x31 + stage);
            }
        },
    };

    const canvas = {
        width: 640,
        height: 480,
        style: {},
        parentElement: {
            getElementsByTagName() {
                return [canvas];
            },
        },
    };
    const emulator = {
        add_listener() {},
    };
    const bridge = globalThis.installV86GLNetworkBridge(emulator, canvas, { gl4es: module });
    const payload = eightStageDrawElementsPayload(Buffer.alloc(70000, 0x5A));

    bridge.renderer.glCall(GLFN_DRAW_ELEMENTS, payload);

    assert.equal(drawSeen, true, "packed draw must reach gl4es after wasm memory growth");
    assert.ok(growthCount >= 1, "test must force WebAssembly.Memory.grow");
    assert.equal(initialHeap.byteLength, 0, "the pre-growth HEAPU8 view must be detached");
    assert.equal(freed.length, 11, "all eight-stage packed arrays and metadata allocations must be freed");
    console.log("v86_network_bridge_memory_growth_test: ok");
}

main();
