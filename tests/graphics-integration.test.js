"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

test("site attaches graphics diagnostics after v86 loads and leaves canvas lifecycle to v86", () => {
    const elements = new Map();
    const document = { getElementById(id) {
        if (!elements.has(id)) elements.set(id, { style: {}, classList: { add() {}, remove() {} } });
        return elements.get(id);
    } };
    const events = new Map();
    const adapter = { hideOverlayCanvas() { throw new Error("site must not hide a restored checkpoint"); } };
    const emulator = { graphics_adapter: undefined, add_listener(name, fn) { events.set(name, fn); } };
    const window = { addEventListener() { throw new Error("site must not install duplicate graphics observers"); } };
    const context = vm.createContext({ document, window, console,
        requestAnimationFrame(fn) { fn(); }, URLSearchParams });
    vm.runInContext(fs.readFileSync(path.join(__dirname, "../app.js"), "utf8"), context);
    context.attachEmulatorListeners(emulator, "warcraft3");
    events.get("emulator-ready")();
    assert.equal(window.v86gl, null, "graphics is not yet available while WASM initializes");
    emulator.graphics_adapter = adapter;
    events.get("emulator-loaded")();
    assert.equal(window.v86gl, adapter);
    events.get("download-progress")({ lengthComputable: true, loaded: 5, total: 10 });
    assert.equal(document.getElementById("progress_bar").style.width, "50%");
});

test("rapid launches wait for the previous canvas/device teardown", async () => {
    const context = vm.createContext({ window: {}, console,
        document: { getElementById() { return {}; } } });
    vm.runInContext(fs.readFileSync(path.join(__dirname, "../app.js"), "utf8"), context);
    const started = [];
    let release;
    context.startEmulator9xMultiDisk = game => {
        started.push(game);
        return new Promise(resolve => { release = resolve; });
    };
    context.launchGameMultiDisk("warcraft3");
    context.launchGameMultiDisk("maplestory");
    await new Promise(setImmediate);
    assert.deepEqual(started, ["warcraft3"]);
    release();
    await new Promise(setImmediate);
    assert.deepEqual(started, ["warcraft3", "maplestory"]);
    release();
});
