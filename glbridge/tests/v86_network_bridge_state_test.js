"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const GLFN_GEN_TEXTURES = 26;
const GLFN_BIND_TEXTURE = 28;
const GLFN_TEX_IMAGE_2D = 29;
const GLFN_DRAW_ARRAYS_DIRECT = 206;
const GL_TEXTURE_2D = 0x0DE1;
const GL_RGBA = 0x1908;
const GL_UNSIGNED_BYTE = 0x1401;

function glRecord(fn, payload) {
    const record = Buffer.alloc(4 + payload.length);
    record.writeUInt16LE(fn, 0);
    record.writeUInt16LE(payload.length, 2);
    payload.copy(record, 4);
    return record;
}

function u32Payload(...values) {
    const payload = Buffer.alloc(values.length * 4);
    values.forEach((value, index) => payload.writeUInt32LE(value >>> 0, index * 4));
    return payload;
}

function textureImagePayload(pixels) {
    const payload = Buffer.alloc(36 + pixels.length);
    payload.writeUInt32LE(GL_TEXTURE_2D, 0);
    payload.writeInt32LE(0, 4);
    payload.writeInt32LE(GL_RGBA, 8);
    payload.writeInt32LE(1, 12);
    payload.writeInt32LE(1, 16);
    payload.writeInt32LE(0, 20);
    payload.writeUInt32LE(GL_RGBA, 24);
    payload.writeUInt32LE(GL_UNSIGNED_BYTE, 28);
    payload.writeUInt32LE(pixels.length, 32);
    pixels.copy(payload, 36);
    return payload;
}

function makeModule(name, modules) {
    let nextPtr = 1024;
    const calls = [];
    const module = {
        name,
        calls,
        HEAPU8: new Uint8Array(64 * 1024),
        _malloc(size) {
            const ptr = nextPtr;
            nextPtr += Math.max(size, 4);
            return ptr;
        },
        _free() {},
        _v86glResize(width, height) {
            calls.push(["resize", width, height]);
        },
        _v86glMakeCurrent(hwnd, x, y, width, height) {
            calls.push(["makeCurrent", hwnd, x, y, width, height]);
        },
        _v86glReleaseCurrent() {
            calls.push(["releaseCurrent"]);
        },
        _v86glDestroyRenderer() {
            calls.push(["destroy"]);
        },
        _v86gl_glGenTextures(count, ptr) {
            const view = new DataView(this.HEAPU8.buffer, ptr, count * 4);
            const ids = [];
            for (let i = 0; i < count; i++) {
                ids.push(view.getUint32(i * 4, true));
            }
            calls.push(["genTextures", ids]);
        },
        _v86gl_glBindTexture(target, texture) {
            calls.push(["bindTexture", target, texture]);
        },
        _v86gl_glTexImage2D(target, level, internalFormat, width, height,
                            border, format, type, ptr) {
            calls.push([
                "texImage2D", target, level, internalFormat, width, height,
                border, format, type, Array.from(this.HEAPU8.subarray(ptr, ptr + 4)),
            ]);
        },
        _v86gl_glDrawArraysDirect(mode, first, count) {
            calls.push(["drawArraysDirect", mode, first, count]);
        },
    };
    modules.push(module);
    return module;
}

async function main() {
    const modules = [];
    const listeners = Object.create(null);
    let resetCount = 0;
    let restoreGate = null;
    const pci = {
        registers: [0, 0, 0, 1, 0, 100, 0, 7],
        get_state() {
            return this.registers.slice();
        },
        set_state(state) {
            this.registers = state.slice(0, 8);
        },
    };
    const emulator = {
        v86: { cpu: { devices: { v86gl_pci: pci } } },
        add_listener(name, callback) {
            listeners[name] = callback;
        },
    };
    const style = {
        setProperty(name, value) {
            this[name] = value;
        },
    };
    const canvas = { width: 800, height: 600, style, parentElement: null };
    const bridge = globalThis.installV86GLNetworkBridge(emulator, canvas, {
        gl4es: makeModule("initial", modules),
        resetGL4ESRenderer() {
            resetCount++;
            const module = makeModule("fresh-" + modules.length, modules);
            if (resetCount === 2) {
                return new Promise(resolve => {
                    restoreGate = { module, resolve };
                });
            }
            return module;
        },
    });

    const surface = Buffer.alloc(20);
    surface.writeUInt32LE(0x1234, 0);
    surface.writeInt32LE(12, 4);
    surface.writeInt32LE(34, 8);
    surface.writeUInt32LE(800, 12);
    surface.writeUInt32LE(600, 16);
    bridge.makeCurrent(surface);

    const pixels = Buffer.from([0x11, 0x22, 0x33, 0x44]);
    bridge.executeGLCommands(Buffer.concat([
        glRecord(GLFN_GEN_TEXTURES, u32Payload(1, 77)),
        glRecord(GLFN_BIND_TEXTURE, u32Payload(GL_TEXTURE_2D, 77)),
        glRecord(GLFN_TEX_IMAGE_2D, textureImagePayload(pixels)),
        glRecord(GLFN_DRAW_ARRAYS_DIRECT, u32Payload(4, 0, 3)),
    ]), "test", 100);
    bridge.lastPresentedFrameId = 100;

    bridge.prepareSaveState();
    const savedPCIState = pci.get_state();
    assert.ok(savedPCIState[8] instanceof Uint8Array,
              "the GL journal must be embedded in the v86 PCI state");
    assert.ok(savedPCIState[8].byteLength > 48, "the journal must contain GL resources");

    bridge.destroyContext();
    assert.equal(bridge.stateJournal.length, 0,
                 "destroying the live context must not retain deleted resources");
    bridge.pendingPCIBatches.push({ bytes: Uint8Array.of(1, 2, 3), frameId: 999 });

    bridge.beginStateRestore();
    pci.set_state(savedPCIState);
    assert.ok(restoreGate, "state restore must wait for the fresh async gl4es module");
    listeners["v86gl-pci-frame"]({
        bytes: glRecord(GLFN_DRAW_ARRAYS_DIRECT, u32Payload(4, 0, 3)),
        frameId: 10,
        submitCount: 1,
        commandCount: 1,
        flags: 0,
    });
    assert.equal(bridge.pendingPCIBatches.length, 1,
                 "new guest work must wait until GL resources are replayed");
    restoreGate.resolve(restoreGate.module);
    const restored = await bridge.finishStateRestore();
    assert.equal(restored.hasGLState, true);
    assert.equal(bridge.lastPresentedFrameId, 0,
                 "restored guest frame ids must not be rejected as stale");
    assert.equal(bridge.pendingPCIBatches.length, 0,
                 "batches from the abandoned timeline must be discarded");

    const restoredModule = modules.at(-1);
    const replayed = restoredModule.calls.map(call => call[0]);
    assert.ok(replayed.includes("genTextures"), "texture names must be recreated");
    assert.ok(replayed.includes("bindTexture"), "texture binding must be recreated");
    assert.ok(replayed.includes("texImage2D"), "texture pixels must be uploaded again");
    assert.equal(bridge.stateJournal.some(entry => entry.fn === GLFN_DRAW_ARRAYS_DIRECT), false,
                 "per-frame draw payloads must not enter the save-state journal");
    const upload = restoredModule.calls.find(call => call[0] === "texImage2D");
    assert.deepEqual(upload.at(-1), Array.from(pixels));
    assert.equal(
        restoredModule.calls.filter(call => call[0] === "drawArraysDirect").length,
        1,
        "a lower post-restore frame id must reach the fresh renderer"
    );
    assert.ok(
        replayed.indexOf("texImage2D") < replayed.indexOf("drawArraysDirect"),
        "queued guest draws must execute only after texture replay"
    );

    console.log("v86_network_bridge_state_test: ok");
}

main().catch(err => {
    console.error(err);
    process.exitCode = 1;
});
