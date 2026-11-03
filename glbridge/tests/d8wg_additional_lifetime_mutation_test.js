"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-additional-lifetime-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d8proxy/d3d8_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement] of [
        ["storage pin not transferred", "device_create_swapchain", "surface->texture->private_back_buffer = TRUE;", "surface->texture->private_back_buffer = 0;"],
        ["internal surface reference exposed", "device_create_swapchain", "surface->refcount = 0;", "surface->refcount = 1;"],
        ["duplicate chain device pin", "device_create_swapchain", "++device->additional_swap_chain_count;", "device_child_add_ref(device); ++device->additional_swap_chain_count;"],
        ["surface forwards public parent count", "surface_add_ref", "if (surface->additional_back_buffer)", "if (0 && surface->additional_back_buffer)"],
        ["pin acquired on every AddRef", "surface_add_ref", "if (refs == 1)", "if (refs > 0)"],
        ["public edge loses storage", "surface_add_ref", "IDirect3DTexture8_AddRef(&surface->texture->iface);", "(void)surface->texture;"],
        ["public edge loses device", "surface_add_ref", "device_child_add_ref(surface->device);", "(void)surface->device;"],
        ["zero Release underflows", "surface_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0))", "if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0))"],
        ["last public release leaks device", "surface_release", "device_child_release(device);", "(void)device;"],
        ["last public release leaks storage", "surface_release", "IDirect3DTexture8_Release(&surface->texture->iface);", "(void)surface->texture;"],
        ["storage returns transferred pin twice", "texture_release", "if (!texture->private_back_buffer)", "if (1)"],
        ["chain releases public count", "swapchain_release", "IDirect3DTexture8_Release(&surface->texture->iface);", "IDirect3DSurface8_Release((IDirect3DSurface8*)surface);"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_private_surface_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_PRIVATE_SURFACE_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(/Private surface|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D8 additional-lifetime mutations: " + killed + "/12; public/internal counts, zero edges, transferred device/storage pins, cached revival and final cleanup");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
