"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const OP_VIEWPORT = 2;
const GLFN_VIEWPORT = 1;

function viewportPayload(x, y, width, height) {
    const payload = Buffer.alloc(16);
    payload.writeInt32LE(x, 0);
    payload.writeInt32LE(y, 4);
    payload.writeInt32LE(width, 8);
    payload.writeInt32LE(height, 12);
    return payload;
}

function glRecord(fn, payload) {
    const record = Buffer.alloc(4 + payload.length);
    record.writeUInt16LE(fn, 0);
    record.writeUInt16LE(payload.length, 2);
    payload.copy(record, 4);
    return record;
}

function main() {
    const resizeCalls = [];
    const viewportCalls = [];
    const listeners = Object.create(null);
    const module = {
        _v86glResize(width, height) {
            resizeCalls.push([width, height]);
        },
        _v86glMakeCurrent() {},
        _v86gl_glViewport(x, y, width, height) {
            viewportCalls.push([x, y, width, height]);
        },
    };
    const style = {
        setProperty(name, value) {
            this[name] = value;
        },
    };
    const canvas = {
        width: 800,
        height: 600,
        style,
        parentElement: null,
    };
    const emulator = {
        add_listener(name, callback) {
            listeners[name] = callback;
        },
    };
    const bridge = globalThis.installV86GLNetworkBridge(emulator, canvas, { gl4es: module });

    assert.deepEqual(resizeCalls, [[800, 600]], "renderer initialization sets the surface size once");

    const legacyViewport = viewportPayload(0, 0, 800, 450);
    bridge.dispatch(OP_VIEWPORT, legacyViewport, 1);

    const directViewport = viewportPayload(0, 450, 800, 150);
    bridge.dispatchGLCall(Buffer.concat([
        Buffer.from([GLFN_VIEWPORT, 0]),
        directViewport,
    ]));

    const pciViewport = viewportPayload(100, 50, 640, 400);
    listeners["v86gl-pci-frame"]({
        bytes: glRecord(GLFN_VIEWPORT, pciViewport),
        frameId: 1,
        submitCount: 1,
        commandCount: 1,
        flags: 0,
    });

    assert.deepEqual(viewportCalls, [
        [0, 0, 800, 450],
        [0, 450, 800, 150],
        [100, 50, 640, 400],
    ]);
    assert.equal(canvas.width, 800, "sub-viewports must not resize the drawing buffer");
    assert.equal(canvas.height, 600, "sub-viewports must not clear the drawing buffer");
    assert.deepEqual(resizeCalls, [[800, 600]], "glViewport must never call renderer.resize");

    const surface = Buffer.alloc(20);
    surface.writeUInt32LE(1, 0);
    surface.writeInt32LE(0, 4);
    surface.writeInt32LE(0, 8);
    surface.writeUInt32LE(1024, 12);
    surface.writeUInt32LE(768, 16);
    bridge.makeCurrent(surface);

    assert.equal(canvas.width, 1024, "surface changes still resize the drawing buffer");
    assert.equal(canvas.height, 768, "surface changes still resize the drawing buffer");
    assert.deepEqual(resizeCalls, [[800, 600], [1024, 768]]);
    console.log("v86_network_bridge_viewport_test: ok");
}

main();
