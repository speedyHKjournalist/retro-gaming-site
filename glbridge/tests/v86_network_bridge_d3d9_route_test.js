"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const listeners = Object.create(null);
const routed = [];
let d3d9Options;
const style = () => ({
    setProperty(name, value) { this[name] = value; },
});
const screenCanvas = {
    width: 1024,
    height: 768,
    getBoundingClientRect() { return { left: 0, top: 0, width: 1024, height: 768 }; },
};
const d3d9Canvas = { width: 1, height: 1, style: style() };
const canvas = {
    width: 64,
    height: 64,
    style: style(),
    parentElement: {
        getElementsByTagName() { return [screenCanvas, canvas, d3d9Canvas]; },
        getBoundingClientRect() { return { left: 0, top: 0, width: 1024, height: 768 }; },
    },
};
/*
 * write_memory is modelled on v86's real one rather than just recording its
 * arguments, because the bug this guards against is invisible to a recorder:
 * v86 spells it write_memory(blob, address) -- the opposite order to
 * read_memory(address, length) -- and it bottoms out in mem8.set(blob, addr).
 * Called the other way round, TypedArray.set() gets a number as its source,
 * copies nothing and throws nothing. Every readback response and query answer
 * was being dropped exactly that way, and the guest could only report a
 * timeout.
 */
const guestMemory = new Uint8Array(4096);
const bridge = globalThis.installV86GLNetworkBridge({
    add_listener(name, callback) { listeners[name] = callback; },
    write_memory(blob, address) { guestMemory.set(blob, address); },
}, canvas, {
    glExecutor: { submit() {}, onSwapBuffers() {} },
    d3d9Canvas,
    installD3D9WebGPUExecutor(installedCanvas, options) {
        assert.equal(installedCanvas, d3d9Canvas);
        d3d9Options = options;
        return {
            submit(bytes, metadata) { routed.push({ bytes: Buffer.from(bytes), metadata }); },
        };
    },
});

bridge.lastPresentedFrameId = 999;
const d9wg = Buffer.alloc(32);
d9wg.writeUInt32LE(0x47573944, 0);
d9wg.writeUInt16LE(1, 4);
d9wg.writeUInt16LE(3, 6);
d9wg.writeUInt32LE(0xA0010001, 24);
d9wg.writeUInt32LE(0x20260806, 28);
const envelope = Buffer.alloc(8 + d9wg.length);
envelope.writeUInt16LE(0xFFE1, 0);
envelope.writeUInt16LE(0xFFFF, 2);
envelope.writeUInt32LE(d9wg.length, 4);
d9wg.copy(envelope, 8);

listeners["v86gl-pci-frame"]({
    bytes: envelope,
    frameId: 1,
    submitCount: 7,
    commandCount: 1,
    flags: 0,
});

assert.equal(routed.length, 1,
    "D9WG routing must not be rejected by the OpenGL stale-frame counter");
assert.deepEqual(routed[0].bytes, d9wg);
assert.equal(routed[0].metadata.pciFrameId, 1);
assert.equal(routed[0].metadata.submitCount, 7);
assert.equal(routed[0].metadata.descriptorCommandCount, 1);
assert.equal(routed[0].metadata.descriptorBase, 0);
assert.equal(typeof routed[0].metadata.writeGuestMemory, "function");

// The bytes have to actually land, at the address asked for.
routed[0].metadata.writeGuestMemory(64, Uint8Array.from([1, 2, 3, 4]));
assert.deepEqual([...guestMemory.subarray(64, 68)], [1, 2, 3, 4],
    "a host->guest write must reach guest memory at the requested offset");
assert.deepEqual([...guestMemory.subarray(0, 4)], [0, 0, 0, 0],
    "and must not land at offset 0 instead");
// The arena bound is still enforced.
assert.throws(() => routed[0].metadata.writeGuestMemory(16 * 1024 * 1024,
    Uint8Array.from([9])), RangeError);

d3d9Options.onSurface({ hwnd: 0x1234, x: 10, y: 20, width: 640,
    height: 480, displayWidth: 640, displayHeight: 480, visible: true }, "create");
d3d9Options.onPresent({ hwnd: 0x1234, x: 10, y: 20, width: 640,
    height: 480, displayWidth: 640, displayHeight: 480, visible: true }, {});
assert.equal(d3d9Canvas.style.display, "block");
assert.equal(d3d9Canvas.style.visibility, "visible");
assert.equal(canvas.style.display, "none");

d3d9Options.onSurface({ hwnd: 0x1234, x: 30, y: 40, width: 640,
    height: 480, displayWidth: 800, displayHeight: 600, visible: true }, "move");
assert.equal(d3d9Canvas.style.left, "30px");
assert.equal(d3d9Canvas.style.top, "40px");
assert.equal(d3d9Canvas.style.width, "800px");
assert.equal(d3d9Canvas.style.height, "600px");

d3d9Options.onSurface({ hwnd: 0x1234, x: 0, y: 0, width: 640,
    height: 480, displayWidth: 800, displayHeight: 600, visible: false }, "hide");
assert.equal(d3d9Canvas.style.display, "none");
assert.equal(d3d9Canvas.style.visibility, "hidden");
d3d9Options.onPresent({ sessionKey: "new-session", hwnd: 0x1234,
    x: 30, y: 40, width: 640, height: 480, displayWidth: 800,
    displayHeight: 600, visible: true }, {});
assert.equal(d3d9Canvas.style.display, "block");

const ownerLeft = d3d9Canvas.style.left;
const ownerTop = d3d9Canvas.style.top;
d3d9Options.onSurface({ sessionKey: "pending-session", hwnd: 0x5678,
    x: 500, y: 600, width: 320, height: 200, visible: true }, "create");
assert.equal(d3d9Canvas.style.display, "block",
    "a helper session's CreateDevice must not hide the presenting owner");
assert.equal(d3d9Canvas.style.left, ownerLeft,
    "a helper session must not move the presenting owner's canvas");
assert.equal(d3d9Canvas.style.top, ownerTop);
d3d9Options.onDestroy({ sessionKey: "pending-session", hwnd: 0x5678,
    x: 500, y: 600, width: 320, height: 200, visible: false }, "session-end");
assert.equal(d3d9Canvas.style.display, "block",
    "tearing down a non-owner session must leave the owner visible");

d3d9Options.onDestroy({ sessionKey: "old-session", hwnd: 0x1234,
    x: 30, y: 40, width: 640, height: 480, displayWidth: 800,
    displayHeight: 600, visible: true }, "device");
assert.equal(d3d9Canvas.style.display, "block",
    "late teardown from an old process session must not hide the new owner");
d3d9Options.onDestroy({ sessionKey: "new-session", hwnd: 0x1234,
    x: 30, y: 40, width: 640, height: 480, displayWidth: 800,
    displayHeight: 600, visible: true }, "device");
assert.equal(d3d9Canvas.style.display, "none");
assert.equal(d3d9Canvas.style.visibility, "hidden");

d3d9Options.onSurface({ sessionKey: "next-session", hwnd: 0x9999,
    x: 5, y: 6, width: 320, height: 200, visible: true }, "create");
assert.equal(d3d9Canvas.style.display, "none",
    "a new session stays hidden until its own first Present");
d3d9Options.onPresent({ sessionKey: "next-session", hwnd: 0x9999,
    x: 5, y: 6, width: 320, height: 200, visible: true }, {});
assert.equal(d3d9Canvas.style.display, "block");

bridge.showOverlayCanvas();
assert.equal(d3d9Canvas.style.display, "none");
assert.equal(canvas.style.display, "block");

// The production page installs both executors on one overlay so it can accept
// either API. Executor/context existence alone must not be diagnosed as a
// conflict; only valid traffic from both APIs makes the shared canvas unsafe.
const sharedCanvas = {};
bridge.d3d8Canvas = sharedCanvas;
bridge.d3d9Canvas = sharedCanvas;
bridge.d3d8Executor = { context: {}, getStats() { return { batches: 0 }; } };
bridge.d3d9Executor = { context: {}, getStats() { return { batches: 0 }; } };
const conflictErrors = [];
const originalConsoleError = console.error;
console.error = (...args) => conflictErrors.push(args);
try {
    bridge.warnOnSharedD3DCanvasConflict("d3d9");
    bridge.warnOnSharedD3DCanvasConflict("d3d9");
    assert.equal(conflictErrors.length, 0,
        "one active API must stay quiet even when both executors are installed");
    bridge.warnOnSharedD3DCanvasConflict("d3d8");
    assert.equal(conflictErrors.length, 1,
        "traffic from both APIs must report the real shared-canvas conflict");
    bridge.warnOnSharedD3DCanvasConflict("d3d9");
    assert.equal(conflictErrors.length, 1, "the conflict is reported only once");
} finally {
    console.error = originalConsoleError;
}

console.log("v86_network_bridge_d3d9_route_test: ok");
