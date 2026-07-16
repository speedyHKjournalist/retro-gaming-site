"use strict";

const assert = require("node:assert/strict");

require("../v86_network_bridge.js");

const GLFN_GEN_TEXTURES = 26;
const GLFN_BIND_TEXTURE = 28;
const GLFN_TEX_IMAGE_2D = 29;
const GLFN_TEX_SUB_IMAGE_2D = 30;
const GLFN_PIXEL_STOREI = 33;
const GLFN_GENERATE_MIPMAP = 99;
const GLFN_DRAW_ARRAYS_DIRECT = 206;
const GLFN_CLEAR = 3;
const GLFN_FLUSH = 8;
const GL_COLOR_BUFFER_BIT = 0x00004000;
const GL_TEXTURE_2D = 0x0DE1;
const GL_RGBA = 0x1908;
const GL_UNSIGNED_BYTE = 0x1401;
const GL_UNPACK_ALIGNMENT = 0x0CF5;

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

function textureSubImagePayload(pixels) {
    const payload = Buffer.alloc(36 + pixels.length);
    payload.writeUInt32LE(GL_TEXTURE_2D, 0);
    payload.writeInt32LE(0, 4);
    payload.writeInt32LE(0, 8);
    payload.writeInt32LE(0, 12);
    payload.writeInt32LE(1, 16);
    payload.writeInt32LE(1, 20);
    payload.writeUInt32LE(GL_RGBA, 24);
    payload.writeUInt32LE(GL_UNSIGNED_BYTE, 28);
    payload.writeUInt32LE(pixels.length, 32);
    pixels.copy(payload, 36);
    return payload;
}

function pixelStorePayload(pname, param) {
    const payload = Buffer.alloc(8);
    payload.writeUInt32LE(pname, 0);
    payload.writeInt32LE(param, 4);
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
        _v86gl_glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                               format, type, ptr) {
            calls.push([
                "texSubImage2D", target, level, xoffset, yoffset, width, height,
                format, type, Array.from(this.HEAPU8.subarray(ptr, ptr + 4)),
            ]);
        },
        _v86gl_glPixelStorei(pname, param) {
            calls.push(["pixelStorei", pname, param]);
        },
        _v86gl_glGenerateMipmap(target) {
            calls.push(["generateMipmap", target]);
        },
        _v86gl_glDrawArraysDirect(mode, first, count) {
            calls.push(["drawArraysDirect", mode, first, count]);
        },
        _v86gl_glClear(mask) {
            calls.push(["clear", mask]);
        },
        _v86gl_glFlush() {
            calls.push(["flush"]);
        },
        _v86glPresent() {
            calls.push(["present"]);
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

    bridge.pushPCIBatch({
        bytes: glRecord(GLFN_CLEAR, u32Payload(GL_COLOR_BUFFER_BIT)),
        frameId: 99,
        submitCount: 1,
        commandCount: 1,
        flags: 1,
    });
    assert.equal(bridge.overlayVisible, true,
                 "a clear-only frame must keep the WebGL overlay visible");
    assert.equal(modules[0].calls.filter(call => call[0] === "present").length, 1,
                 "a guest swap boundary must explicitly present the WebGL back buffer");
    assert.equal(modules[0].calls.some(call => call[0] === "flush"), false,
                 "an explicit-present module must not fall back to a plain flush");

    bridge.executeGLCommands(glRecord(GLFN_FLUSH, Buffer.alloc(0)),
                             "guest glFlush", 99);
    assert.equal(modules[0].calls.filter(call => call[0] === "flush").length, 1,
                 "guest glFlush must still flush the offscreen back buffer");
    assert.equal(modules[0].calls.filter(call => call[0] === "present").length, 1,
                 "guest glFlush must not expose an unfinished frame");

    const explicitPresent = modules[0]._v86glPresent;
    delete modules[0]._v86glPresent;
    bridge.renderer.present();
    modules[0]._v86glPresent = explicitPresent;
    assert.equal(modules[0].calls.filter(call => call[0] === "flush").length, 2,
                 "an older module without v86glPresent must retain the flush fallback");

    const pixels = Buffer.from([0x11, 0x22, 0x33, 0x44]);
    const copiedPixels = Buffer.from([0x51, 0x62, 0x73, 0x84]);

    const truncatedImage = textureImagePayload(pixels).subarray(0, 38);
    const truncatedSubImage = textureSubImagePayload(copiedPixels).subarray(0, 38);
    bridge.executeGLCommands(Buffer.concat([
        glRecord(GLFN_GEN_TEXTURES, u32Payload(1, 77)),
        glRecord(GLFN_BIND_TEXTURE, u32Payload(GL_TEXTURE_2D, 77)),
        glRecord(GLFN_TEX_IMAGE_2D, truncatedImage),
        glRecord(GLFN_TEX_SUB_IMAGE_2D, truncatedSubImage),
    ]), "truncated texture payloads", 100);
    assert.equal(modules[0].calls.some(call => call[0] === "texImage2D"), false,
                 "a truncated TexImage2D payload must not reach gl4es");
    assert.equal(modules[0].calls.some(call => call[0] === "texSubImage2D"), false,
                 "a truncated TexSubImage2D payload must not reach gl4es");
    assert.equal(bridge.stateJournal.some(entry =>
        entry.fn === GLFN_TEX_IMAGE_2D || entry.fn === GLFN_TEX_SUB_IMAGE_2D), false,
        "truncated texture payloads must not enter the save-state journal");

    const repeatedSubImageRecords = [
        glRecord(GLFN_TEX_IMAGE_2D, textureImagePayload(pixels)),
    ];
    let finalCopiedPixels = null;
    for (let i = 0; i < 128; i++) {
        finalCopiedPixels = Buffer.from([i, i ^ 0x55, i ^ 0xAA, 0xFF]);
        repeatedSubImageRecords.push(
            glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 1)),
            glRecord(GLFN_TEX_SUB_IMAGE_2D, textureSubImagePayload(finalCopiedPixels)),
            glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 4))
        );
    }
    repeatedSubImageRecords.push(
        glRecord(GLFN_DRAW_ARRAYS_DIRECT, u32Payload(4, 0, 3))
    );
    bridge.executeGLCommands(Buffer.concat(repeatedSubImageRecords), "test", 100);
    bridge.lastPresentedFrameId = 100;

    const journalSubImages = bridge.stateJournal.filter(
        entry => entry.fn === GLFN_TEX_SUB_IMAGE_2D
    );
    assert.equal(journalSubImages.length, 1,
                 "repeated full-level uploads with identical unpack state must compact");
    assert.deepEqual(Array.from(journalSubImages[0].payload.subarray(36)),
                     Array.from(finalCopiedPixels),
                     "the compacted upload must keep the newest framebuffer pixels");
    assert.ok(bridge.stateJournal.length < 10,
              "128 normalized full-level updates must leave a constant-size journal");

    bridge.prepareSaveState();
    const savedPCIState = pci.get_state();
    assert.ok(savedPCIState[8] instanceof Uint8Array,
              "the GL journal must be embedded in the v86 PCI state");
    assert.ok(savedPCIState[8].byteLength > 48, "the journal must contain GL resources");

    const postMipmapPixels = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);
    bridge.executeGLCommands(Buffer.concat([
        glRecord(GLFN_GENERATE_MIPMAP, u32Payload(GL_TEXTURE_2D)),
        glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 1)),
        glRecord(GLFN_TEX_SUB_IMAGE_2D, textureSubImagePayload(postMipmapPixels)),
        glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 4)),
    ]), "mipmap compaction barrier", 101);
    assert.equal(bridge.stateJournal.filter(
        entry => entry.fn === GLFN_TEX_SUB_IMAGE_2D
    ).length, 2, "GenerateMipmap must prevent moving a later upload before it");

    const repeatedImageRecords = [
        glRecord(GLFN_GEN_TEXTURES, u32Payload(1, 88)),
        glRecord(GLFN_BIND_TEXTURE, u32Payload(GL_TEXTURE_2D, 88)),
    ];
    let finalImagePixels = null;
    for (let i = 0; i < 128; i++) {
        finalImagePixels = Buffer.from([0xFF, i, i ^ 0x7F, 0xFF]);
        repeatedImageRecords.push(
            glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 1)),
            glRecord(GLFN_TEX_IMAGE_2D, textureImagePayload(finalImagePixels)),
            glRecord(GLFN_PIXEL_STOREI, pixelStorePayload(GL_UNPACK_ALIGNMENT, 4))
        );
    }
    bridge.executeGLCommands(Buffer.concat(repeatedImageRecords),
                             "repeated texture definitions", 102);
    const imageEntries = bridge.stateJournal.filter(entry => entry.fn === GLFN_TEX_IMAGE_2D);
    assert.equal(imageEntries.length, 2,
                 "repeated definitions of texture 88 must compact to one entry");
    assert.deepEqual(Array.from(imageEntries.at(-1).payload.subarray(36)),
                     Array.from(finalImagePixels));

    bridge.destroyContext();
    assert.equal(bridge.stateJournal.length, 0,
                 "destroying the live context must not retain deleted resources");
    const moduleCountAfterDestroy = modules.length;
    const freshAfterDestroy = modules.at(-1);
    bridge.destroyContext();
    assert.equal(modules.length, moduleCountAfterDestroy,
                 "a duplicate guest destroy must not create another renderer");
    assert.equal(modules[0].calls.filter(call => call[0] === "destroy").length, 1,
                 "the live renderer must be destroyed exactly once");
    assert.equal(freshAfterDestroy.calls.filter(call => call[0] === "destroy").length, 0,
                 "a duplicate destroy must leave the pre-created renderer intact");
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
    assert.ok(replayed.includes("texSubImage2D"),
              "captured framebuffer copies must replay as texture uploads");
    assert.equal(bridge.stateJournal.some(entry => entry.fn === GLFN_DRAW_ARRAYS_DIRECT), false,
                 "per-frame draw payloads must not enter the save-state journal");
    const upload = restoredModule.calls.find(call => call[0] === "texImage2D");
    assert.deepEqual(upload.at(-1), Array.from(pixels));
    const copiedUpload = restoredModule.calls.find(call => call[0] === "texSubImage2D");
    assert.deepEqual(copiedUpload.at(-1), Array.from(finalCopiedPixels));
    assert.deepEqual(restoredModule.calls.filter(call => call[0] === "pixelStorei").at(-1),
                     ["pixelStorei", GL_UNPACK_ALIGNMENT, 4],
                     "journal compaction must preserve the final unpack alignment");
    assert.equal(
        restoredModule.calls.filter(call => call[0] === "drawArraysDirect").length,
        1,
        "a lower post-restore frame id must reach the fresh renderer"
    );
    assert.ok(
        replayed.indexOf("texImage2D") < replayed.indexOf("drawArraysDirect"),
        "queued guest draws must execute only after texture replay"
    );

    bridge.beginStateRestore();
    pci.set_state(savedPCIState.slice(0, 8));
    listeners["v86gl-pci-frame"]({
        bytes: glRecord(GLFN_DRAW_ARRAYS_DIRECT, u32Payload(4, 0, 3)),
        frameId: 5,
        submitCount: 1,
        commandCount: 1,
        flags: 0,
    });
    const legacyRestored = await bridge.finishStateRestore();
    assert.equal(legacyRestored.hasGLState, false,
                 "snapshots without a journal must use the legacy restore path");
    assert.equal(bridge.lastPresentedFrameId, 0,
                 "legacy restores must also accept the restored guest's lower frame ids");
    assert.equal(bridge.pendingPCIBatches.length, 0,
                 "legacy guest work must wait for the clean renderer");
    assert.equal(modules.at(-1).calls.filter(call => call[0] === "drawArraysDirect").length, 1,
                 "legacy restores must resume guest rendering after the reset");

    console.log("v86_network_bridge_state_test: ok");
}

main().catch(err => {
    console.error(err);
    process.exitCode = 1;
});
