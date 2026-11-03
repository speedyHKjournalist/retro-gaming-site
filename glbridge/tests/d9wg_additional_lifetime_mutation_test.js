"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-additional-lifetime-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement, harness = "private"] of [
        ["private storage device ownership lost", "create_target_texture", "texture->private_back_buffer = TRUE;", "texture->private_back_buffer = 0;", "target"],
        ["surface device pin not transferred", "device_create_additional_swap_chain", "surface->device_pin_transferred = TRUE;", "surface->device_pin_transferred = 0;", "parameters"],
        ["chain container missing", "device_create_additional_swap_chain", "surface->swap_chain = chain;", "surface->swap_chain = NULL;", "parameters"],
        ["internal reference exposed", "device_create_additional_swap_chain", "surface->refcount = 0;", "surface->refcount = 1;", "parameters"],
        ["extra texture device pin retained", "create_target_texture", "    if (!emit_texture_create(device, texture))", "    device_child_add_ref(device);\n    if (!emit_texture_create(device, texture))", "target"],
        ["surface loses public device pin", "surface_add_ref", "device_child_add_ref(surface->device);", "(void)surface->device;"],
        ["surface pins device per AddRef", "surface_add_ref", "if (refs == 1)", "if (refs > 0)"],
        ["zero surface Release underflows", "surface_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0))", "if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0))"],
        ["surface leaks final device pin", "surface_release", "device_child_release(device);", "(void)device;"],
        ["zero surface destroys live chain", "surface_release", "if (!InterlockedCompareExchange(&chain->refcount, 0, 0))", "if (1)"],
        ["zero chain destroys held surface", "swap_chain_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0))", "if ((void)surface, 1)"],
        ["surface returns transferred pin twice", "surface_release", "BOOL release_device = !surface->device_pin_transferred;", "BOOL release_device = TRUE;"],
        ["texture returns transferred pin twice", "texture_release", "if (!texture->private_back_buffer)", "if (1)"],
        ["zero chain blocks binding-only Reset", "device_has_reset_blockers", "if (InterlockedCompareExchange(&chain->refcount, 0, 0))", "if (1)", "resources"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement));
        const script = harness === "target" ? "d9wg_target_lifetime_test.js" : harness === "parameters" ? "d9wg_additional_parameters_entry_test.js" : harness === "resources" ? "d9wg_reset_resources_test.js" : "d9wg_private_surface_test.js";
        const envKey = harness === "target" ? "D9WG_TARGET_LIFETIME_SOURCE" : harness === "parameters" ? "D9WG_ADDITIONAL_PARAMETERS_SOURCE" : harness === "resources" ? "D9WG_RESET_FRONTEND" : "D9WG_PRIVATE_SURFACE_SOURCE";
        const r = spawnSync(process.execPath, [__dirname + "/" + script], {encoding: "utf8", timeout: 30000, env: {...process.env, [envKey]: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(/Private surface|Additional parameters|Target lifetime|Reset graph|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 additional-lifetime mutations: " + killed + "/14; independent public counters, container association, physical chain retention, transferred device pins and zero-chain Reset policy");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
