"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-target-binding-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = readComparisonSource(root + "/d3d8proxy/d3d8_proxy.c", "utf8"); let killed = 0;
    for (const [label, name, old, replacement, harness] of [
        ["RT public count not initialized", "device_create_render_target", "surface_from_iface(*surface_out)->refcount = 1;", "surface_from_iface(*surface_out)->refcount = 0;", "container"],
        ["depth public count not initialized", "device_create_depth_surface", "surface_from_iface(*surface_out)->refcount = 1;", "surface_from_iface(*surface_out)->refcount = 0;", "container"],
        ["RT storage retains device pin", "device_create_render_target", "texture_from_iface(texture_iface)->private_back_buffer = TRUE;", "texture_from_iface(texture_iface)->private_back_buffer = 0;", "container"],
        ["depth storage retains device pin", "device_create_depth_surface", "texture->private_back_buffer = TRUE;", "texture->private_back_buffer = 0;", "container"],
        ["standalone AddRef forwards texture count", "surface_add_ref", "if (surface->additional_back_buffer || surface->device_container)", "if (surface->additional_back_buffer)", "private"],
        ["standalone Release forwards texture count", "surface_release", "if (surface->additional_back_buffer || surface->device_container)", "if (surface->additional_back_buffer)", "private"],
        ["revival omits storage pin", "surface_add_ref", "IDirect3DTexture8_AddRef(&surface->texture->iface);", "(void)surface->texture;", "private"],
        ["revival omits public device pin", "surface_add_ref", "device_child_add_ref(surface->device);", "(void)surface->device;", "private"],
        ["every AddRef adds device and storage pins", "surface_add_ref", "if (refs == 1) {", "if (1) {", "private"],
        ["public zero leaks storage", "surface_release", "IDirect3DTexture8_Release(&surface->texture->iface);", "(void)surface->texture;", "private"],
        ["public zero leaks device pin", "surface_release", "device_child_release(device);", "(void)device;", "private"],
        ["depth binding increments public counter", "surface_binding_add_ref", "IDirect3DTexture8_AddRef(&surface->texture->iface);", "IDirect3DSurface8_AddRef(&surface->iface);", "private"],
        ["depth binding releases public count", "surface_binding_release", "IDirect3DTexture8_Release(&surface->texture->iface);", "IDirect3DSurface8_Release(&surface->iface);", "private"],
        ["final cleanup lacks self pin", "device_release", "InterlockedIncrement(&device->refcount);", "(void)device;", "private"],
        ["final cleanup leaks self pin", "device_release", "refs = (ULONG)InterlockedDecrement(&device->refcount);\n        device->releasing_owned_refs = FALSE;", "refs = (ULONG)InterlockedCompareExchange(&device->refcount, 0, 0);\n        device->releasing_owned_refs = FALSE;", "private"],
        ["zero public Release underflows", "surface_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0))", "if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0))", "private"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, name, old, replacement));
        const r = spawnSync(process.execPath, [__dirname + (harness === "container" ? "/d8wg_standalone_container_test.js" : "/d9wg_private_surface_test.js")], {encoding: "utf8", timeout: 30000, env: {...process.env, ...(harness === "container" ? {D8WG_STANDALONE_CONTAINER_SOURCE: file} : {D8WG_PRIVATE_SURFACE_SOURCE: file})}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/D8 standalone container|D8 standalone lifetime|Private surface|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D8 target-binding mutations: " + killed + "/16; actual constructor promotion, separate public/storage counts, parent-pin edges, private depth holds and protected final cleanup");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
