"use strict";

const assert = require("node:assert/strict");
const { performance } = require("node:perf_hooks");

require("../v86_network_bridge.js");

const GLFN_QUERY_OBJECT_BATCH = 216;
const QUERY_COUNT = 2048; // Cube 2 MAXQUERY
const ITERATIONS = 100;
const VISIBLE_SAMPLES = 0x7FFFFFFF;

function makePayload() {
    const payload = Buffer.alloc(16 + QUERY_COUNT * 12);
    payload.writeUInt32LE(QUERY_COUNT, 0);
    payload.writeUInt32LE(3, 4); // V86GL_OBJECT_KIND_QUERY
    for (let i = 0; i < QUERY_COUNT; i++) {
        payload.writeUInt32LE(i + 1, 16 + i * 12);
    }
    return payload;
}

function makeModule() {
    let calls = 0;
    return {
        HEAPU8: new Uint8Array(128 * 1024),
        _malloc() { return 1024; },
        _free() {},
        _v86glResize() {},
        _v86gl_glQueryObjectsMapped(count, namesPtr, availablePtr, resultsPtr) {
            assert.equal(count, QUERY_COUNT);
            const view = new DataView(this.HEAPU8.buffer);
            for (let i = 0; i < count; i++) {
                assert.equal(view.getUint32(namesPtr + i * 4, true), i + 1);
                const visible = (i & 7) !== 0;
                view.setUint32(availablePtr + i * 4, 1, true);
                view.setUint32(resultsPtr + i * 4,
                    visible ? VISIBLE_SAMPLES : 0, true);
            }
            calls++;
            return 1;
        },
        get calls() { return calls; },
    };
}

function main() {
    const module = makeModule();
    const canvas = {
        width: 640,
        height: 480,
        style: {},
        parentElement: { getElementsByTagName() { return [canvas]; } },
    };
    const bridge = globalThis.installV86GLNetworkBridge(
        { add_listener() {} }, canvas, { gl4es: module });
    const payload = makePayload();

    for (let i = 0; i < 5; i++) {
        bridge.renderer.glCall(GLFN_QUERY_OBJECT_BATCH, payload);
    }
    const started = performance.now();
    for (let i = 0; i < ITERATIONS; i++) {
        bridge.renderer.glCall(GLFN_QUERY_OBJECT_BATCH, payload);
    }
    const elapsedMs = performance.now() - started;

    assert.equal(module.calls, ITERATIONS + 5,
                 "one bridge/WASM call must service an entire Cube query pool");
    assert.equal(payload.readUInt32LE(8), 1);
    assert.equal(payload.readUInt32LE(20), 1);
    assert.equal(payload.readUInt32LE(24), 0);
    assert.equal(payload.readUInt32LE(32), 1);
    assert.equal(payload.readUInt32LE(36), VISIBLE_SAMPLES);
    /* This is deliberately generous and catches accidental O(N^2) parsing,
     * not normal machine-to-machine timing variation. */
    assert.ok(elapsedMs < 5000,
              `batched query dispatch regressed: ${elapsedMs.toFixed(1)}ms`);

    const queriesPerSecond = Math.round(
        QUERY_COUNT * ITERATIONS * 1000 / Math.max(elapsedMs, 0.001));
    console.log(
        `cube2_query_batch_performance_test: ok ` +
        `(${QUERY_COUNT} queries/call, ${queriesPerSecond} queries/s, ` +
        `${elapsedMs.toFixed(1)}ms)`
    );
}

main();
