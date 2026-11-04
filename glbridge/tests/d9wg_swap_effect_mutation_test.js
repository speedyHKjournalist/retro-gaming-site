"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-swap-effect-"));
function fail(label, env) {
    const r = spawnSync(process.execPath, [__dirname + "/d9wg_swap_effect_entry_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, ...env}}), output = r.stdout + r.stderr;
    assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(/Parameter preflight|Assertion|signal=/.test(output), output);
}
function mutate(source, fn, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + fn + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, fn); let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    const part = source.slice(m.index, end); assert(part.includes(old), fn + ": " + old); return source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end);
}
try {
    const header = fs.readFileSync(root + "/runtime/d9wg_swap_effect.h", "utf8"); let killed = 0;
    for (const [label, old, replacement] of [
        ["unknown API accepted", "if (api != 8 && api != 9)", "if (0 && api != 8 && api != 9)"],
        ["zero effect accepted", "if (!effect || effect >", "if (effect >"],
        ["Ex effects accepted", "effect > (api == 8 ? 4u : 3u)", "effect > 4u"],
        ["COPY_VSYNC refused", "effect > (api == 8 ? 4u : 3u)", "effect > 3u"],
        ["COPY count ignored", "if (effect == 3 || (api == 8 && effect == 4))", "if (0 && (effect == 3 || (api == 8 && effect == 4)))"],
        ["COPY_VSYNC count ignored", "if (effect == 3 || (api == 8 && effect == 4))", "if (effect == 3)"],
        ["zero count refused", "return count <= 1;", "return count == 1;"],
        ["two COPY buffers accepted", "return count <= 1;", "return count <= 2;"],
    ]) {assert(header.includes(old), label); const file = dir + "/header.h"; fs.writeFileSync(file, header.replace(old, replacement)); fail(label, {D9WG_SWAP_EFFECT_HEADER: file}); killed++;}
    for (const api of [8, 9]) {
        const source = fs.readFileSync(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        for (const fn of ["d3d_create_device", "device_reset", api === 8 ? "device_create_swapchain" : "device_create_additional_swap_chain"]) {
            for (const [label, old, replacement] of [["guard omitted", "if (!d9wg_swap_effect_valid(", "if (0 && !d9wg_swap_effect_valid("], ["wrong version", "d9wg_swap_effect_valid(" + (api===9&&fn==="d3d_create_device"?"api":api) + ",", "d9wg_swap_effect_valid(" + (api === 8 ? 9 : 8) + ","]]) {
                const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement)); fail("API " + api + " " + fn + " " + label, {["D" + api + "WG_SWAP_EFFECT_FRONTEND"]: file}); killed++;
            }
        }
    }
    console.log("PASS swap-effect mutations: " + killed + "/20; enum versions, COPY/COPY_VSYNC count policy and all six actual preflight routes");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
