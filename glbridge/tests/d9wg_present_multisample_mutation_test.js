"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-swap-effect-"));
function fail(label, env) {
    const r = spawnSync(process.execPath, [__dirname + "/d9wg_present_multisample_entry_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, ...env}}), output = r.stdout + r.stderr;
    assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(/Parameter preflight|Assertion|signal=/.test(output), output);
}
function mutate(source, fn, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + fn + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, fn); let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    const part = source.slice(m.index, end); assert(part.includes(old), fn + ": " + old); return source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end);
}
try {
    const header = readComparisonSource(root + "/runtime/d9wg_present_multisample.h", "utf8"); let killed = 0;
    for (const [label, old, replacement] of [
        ["unknown API accepted", "if (api != 8 && api != 9)", "if (0 && api != 8 && api != 9)"],
        ["MSAA accepts non-DISCARD", "if (samples && effect != 1)", "if (0 && samples && effect != 1)"],
        ["MSAA requires FLIP", "if (samples && effect != 1)", "if (samples && effect != 2)"],
        ["single sample also requires DISCARD", "if (samples && effect != 1)", "if (effect != 1)"],
        ["D3D9 accepts lockable MSAA", "(samples && (flags & 1u))", "(0u * samples && (flags & 1u))"],
        ["D3D8 loses lockable exception", "if (api == 9 &&", "if ((api == 8 || api == 9) &&"],
        ["quality ignored", "(quality ||", "(0u * quality ||"],
        ["quality one accepted", "(quality ||", "(quality > 1 ||"],
    ]) {assert(header.includes(old), label); const file = dir + "/header.h"; fs.writeFileSync(file, header.replace(old, replacement)); fail(label, {D9WG_MSAA_HEADER: file}); killed++;}
    for (const api of [8, 9]) {
        const source = readComparisonSource(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        for (const fn of ["d3d_create_device", "device_reset", api === 8 ? "device_create_swapchain" : "device_create_additional_swap_chain"]) {
            for (const [label, old, replacement] of [["guard omitted", "if (!d9wg_present_multisample_valid(", "if (0 && !d9wg_present_multisample_valid("], ["wrong version", "d9wg_present_multisample_valid(" + (api===9?"api":api) + ",", "d9wg_present_multisample_valid(" + (api === 8 ? 9 : 8) + ","]]) {
                const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement)); fail("API " + api + " " + fn + " " + label, {["D" + api + "WG_MSAA_FRONTEND"]: file}); killed++;
            }
        }
    }
    const source8 = readComparisonSource(root + "/d3d8proxy/d3d8_proxy.c", "utf8");
    for (const [label, old, replacement] of [
        ["MSAA lock allowed", "|| texture->multisample_type != D3DMULTISAMPLE_NONE", "|| (0 && texture->multisample_type != D3DMULTISAMPLE_NONE)"],
        ["only four-sample locks refused", "texture->multisample_type != D3DMULTISAMPLE_NONE", "texture->multisample_type == 4"],
        ["lockable flag bypasses MSAA guard", "texture->multisample_type != D3DMULTISAMPLE_NONE", "(texture->multisample_type != D3DMULTISAMPLE_NONE && !texture->lockable_render_target)"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source8, "texture_lock_level", old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d8wg_surface_copy_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_SURFACE_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), output); assert(/surface|Assertion|signal=/.test(output), output); killed++;
    }
    {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source8, "device_create_swapchain", "&& parameters->MultiSampleType == D3DMULTISAMPLE_NONE", "&& 1"));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_additional_parameters_entry_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_ADDITIONAL_PARAMETERS_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error); assert.notEqual(r.status, 0, "MSAA chain lockable storage survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), output); assert(/Additional parameters|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS presentation MSAA mutations: " + killed + "/24; DISCARD-only sampling, versioned lockable flags/quality, all six entry guards and actual D3D8 lock refusal before readback and lockable-flag storage translation");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
