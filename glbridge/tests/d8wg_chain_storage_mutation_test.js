"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-chain-storage-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = readComparisonSource(root + "/d3d8proxy/d3d8_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement, constructor = false] of [
        ["storage forgets chain", "device_create_swapchain", "surface->texture->swap_chain = chain;", "surface->texture->swap_chain = NULL;", true],
        ["storage never frees chain", "texture_release", "swapchain_destroy_storage(texture->swap_chain);", "(void)swapchain_destroy_storage;"],
        ["revival loses storage pin", "swapchain_add_ref", "IDirect3DTexture8_AddRef(&surface->texture->iface);", "(void)surface;"],
        ["revival loses device pin", "swapchain_add_ref", "device_child_add_ref(chain->device);", "(void)chain->device;"],
        ["every chain AddRef pins storage", "swapchain_add_ref", "if (refs == 1)", "if (refs > 0)"],
        ["revival loses Reset blocker", "swapchain_add_ref", "++chain->device->additional_swap_chain_count;", "(void)chain->device;"],
        ["zero chain keeps Reset blocker", "swapchain_release", "--device->additional_swap_chain_count;", "(void)device;"],
        ["zero chain frees held storage", "swapchain_release", "IDirect3DTexture8_Release(&surface->texture->iface);", "swapchain_destroy_storage(chain); IDirect3DTexture8_Release(&surface->texture->iface);"],
        ["storage destroy names wrong device", "swapchain_destroy_storage", "destroy.device_handle = chain->device->handle;", "destroy.device_handle = 0;"],
        ["storage returns pending device pin", "swapchain_destroy_storage", "HeapFree(GetProcessHeap(), 0, chain);", "device_child_release(chain->device); HeapFree(GetProcessHeap(), 0, chain);"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement));
        const script = constructor ? "d9wg_additional_parameters_entry_test.js" : "d9wg_private_surface_test.js", envKey = constructor ? "D8WG_ADDITIONAL_PARAMETERS_SOURCE" : "D8WG_PRIVATE_SURFACE_SOURCE";
        const r = spawnSync(process.execPath, [__dirname + "/" + script], {encoding: "utf8", timeout: 30000, env: {...process.env, [envKey]: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(/Private surface|Additional parameters|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D8 physical-chain mutations: " + killed + "/10; chain/storage association, zero-edge pins and Reset counts, physical lifetime and final device cleanup");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
