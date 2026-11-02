"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/d3d7_proxy.inc"), "utf8"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-state-block-mutants-"));
function replaceInFunction(name, before, after) {
    const match = new RegExp("static (?:HRESULT WINAPI|BOOL|void) " + name + "\\([^;]*?\\)\\s*\\{").exec(source);
    assert(match, name);
    const start = match.index;
    const body = source.indexOf("{", start);
    let end = body + 1, depth = 1;
    for (; depth; ++end) {
        assert(end < source.length);
        if (source[end] === "{") ++depth;
        if (source[end] === "}") --depth;
    }
    const original = source.slice(start, end);
    assert.equal(original.split(before).length, 2, name + ": " + before);
    return source.slice(0, start) + original.replace(before, after) + source.slice(end);
}
const cases = [
    ["recorded matrix mutates live", () => replaceInFunction("d3d7_device_SetTransform", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["Multiply ignores preceding recorded transform", () => replaceInFunction("d3d7_device_MultiplyTransform", "device->recording_state_block &&", "0 && device->recording_state_block &&")],
    ["recorded viewport mutates live", () => replaceInFunction("d3d7_device_SetViewport", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["recorded material mutates live", () => replaceInFunction("d3d7_device_SetMaterial", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["recorded render state mutates live", () => replaceInFunction("d3d7_set_render_state", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["recorded stage state mutates live", () => replaceInFunction("d3d7_device_SetTextureStageState", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["recorded clip plane mutates live", () => replaceInFunction("d3d7_device_SetClipPlane", "if (device->recording_state_block)", "if (0 && device->recording_state_block)")],
    ["recorded texture uploads prematurely", () => replaceInFunction("d3d7_device_SetTexture", "D3D7StateBlock *block = device->recording_state_block;", "D3D7StateBlock *block = device->recording_state_block; if (surface) prepare_surface_for_host(surface);")],
    ["texture overwrite leaks old reference", () => replaceInFunction("d3d7_device_SetTexture", "if (block->textures[stage]) surface_Release", "if (0 && block->textures[stage]) surface_Release")],
    ["End replaces recorded values with live", () => replaceInFunction("d3d7_device_EndStateBlock", "/* Recorded values and references are complete; End only publishes. */", "d3d7_state_block_capture(device, block); /* Recorded values and references are complete; End only publishes. */")],
    ["vertex block omits fog color", () => {
        const before = "case 9: case 22: case 28: case 29: case 34: case 35: case 36:";
        assert.equal(source.split(before).length, 2);
        return source.replace(before, "case 9: case 22: case 28: case 29: case 35: case 36:");
    }],
    ["pixel block loses shared fog start", () => {
        const before = "case 27: case 33: case 36: case 37: case 38:";
        assert.equal(source.split(before).length, 2);
        return source.replace(before, "case 27: case 33: case 37: case 38:");
    }],
    ["ALL captures explicitly recorded-only state", () => replaceInFunction("d3d7_state_block_mark_type", "UINT state, stage;", "UINT state, stage; if(type == D3DSBT_ALL) block->render_mask[30] = 1;")],
    ["rejected duplicate cache omitted", () => replaceInFunction("d3d7_device_SetRenderState", "if (device->rejected_render_set[state] &&", "if (0 && device->rejected_render_set[state] &&")],
    ["Begin changes rejected duplicate cache", () => replaceInFunction("d3d7_device_SetRenderState", "!device->recording_state_block && d3d_legacy_render_state_valid(state)", "d3d_legacy_render_state_valid(state)")],
    ["legacy namespace leaks into Device7", () => replaceInFunction("d3d7_device_SetRenderState", "!d3d7_render_state_valid(state)", "!d3d_legacy_render_state_valid(state)")],
    ["pixel block selects texture binding", () => {
        const before = "if (type == D3DSBT_ALL) block->texture_mask[stage] = 1;";
        assert.equal(source.split(before).length, 2);
        return source.replace(before, "block->texture_mask[stage] = 1;");
    }],
];
try {
    for (const [label, mutate] of cases) {
        const file = path.join(directory, "state.inc");
        fs.writeFileSync(file, mutate());
        const run = spawnSync(process.execPath, [path.join(__dirname, "d7wg_state_block_test.js")], { env: { ...process.env, D7WG_STATE_BLOCK_SOURCE: file }, encoding: "utf8" });
        assert.notEqual(run.status, 0, "survived " + label);
        assert.match(run.stderr, /D3D7 state block line/, "runtime assertion required: " + label);
    }
    const legacy = fs.readFileSync(path.join(__dirname, "../ddrawproxy/d3d_legacy_proxy.inc"), "utf8");
    for (const [label, before, after] of [
        ["legacy getter inherits Device7 rejection", "!d3d_legacy_render_state_valid(state)", "!d3d7_render_state_valid(state)"],
        ["legacy setter inherits Device7 rejection", "return d3d7_set_render_state((IDirect3DDevice7 *)device, state,", "return d3d7_device_SetRenderState((IDirect3DDevice7 *)device, state,"],
    ]) {
        assert.equal(legacy.split(before).length, 2, label);
        const file = path.join(directory, "legacy.inc");
        fs.writeFileSync(file, legacy.replace(before, after));
        const run = spawnSync(process.execPath, [path.join(__dirname, "d7wg_state_block_test.js")], { env: { ...process.env, D7WG_LEGACY_STATE_SOURCE: file }, encoding: "utf8" });
        assert.notEqual(run.status, 0, "survived " + label);
        assert.match(run.stderr, /D3D7 state block line/, "runtime assertion required: " + label);
    }
    console.log("PASS D3D7 ordinary state-block mutations: 19/19; live isolation, ordered Multiply, zero early uploads, texture ownership, End and native predefined scope");
} finally {
    fs.rmSync(directory, { recursive: true, force: true });
}
