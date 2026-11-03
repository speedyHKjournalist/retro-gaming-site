"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-lifetime-mut-"));
function mutate(s, name, old, replacement, last) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name); let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end), at = last ? body.lastIndexOf(old) : body.indexOf(old); assert(at >= 0, name + ": " + old);
    return s.slice(0, m.index) + body.slice(0, at) + replacement + body.slice(at + old.length) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement, last = false] of [
        ["private storage returns a nonexistent parent pin", "create_target_texture", "texture->private_back_buffer = TRUE;", "texture->private_back_buffer = 0;"],
        ["storage adds a second parent pin", "create_target_texture", "    if (!emit_texture_create(device, texture))", "    device_child_add_ref(device);\n    if (!emit_texture_create(device, texture))"],
        ["public surface omits parent pin", "create_target_texture", "device_child_add_ref(device);", "(void)device;"],
        ["public AddRef pins per reference", "surface_add_ref", "refs = (ULONG)InterlockedIncrement(&surface->refcount);", "device_child_add_ref(surface->device); refs = (ULONG)InterlockedIncrement(&surface->refcount);", true],
        ["every Release frees the target", "surface_release", "if (!refs) {", "if (1) {", true],
        ["private texture returns transferred parent pin", "texture_release", "if (!texture->private_back_buffer)", "if (1)"],
        ["surface leaks its parent pin", "surface_release", "if (!surface->device_pin_transferred)", "if (0 && !surface->device_pin_transferred)"],
        ["parent dies before storage cleanup", "surface_release", "d9wg_private_clear(&surface->private_data, &d9wg_private_ops);", "if (!surface->device_pin_transferred) device_child_release(device); surface->device_pin_transferred = TRUE;\n        d9wg_private_clear(&surface->private_data, &d9wg_private_ops);"],
        ["retired storage stays linked", "texture_release", "*link = texture->next_device_resource;", "*link = texture;"],
        ["failed emission returns nonexistent pin", "create_target_texture", "if (!emit_texture_create(device, texture)) {", "if (!emit_texture_create(device, texture)) {\n        device_child_release(device);"],
        ["surface allocation failure leaks storage", "create_target_texture", "IDirect3DTexture9_Release(&texture->iface);", "(void)texture;"],
        ["standalone pin incorrectly transferred", "create_target_texture", "surface->refcount = 1;", "surface->refcount = 1; surface->device_pin_transferred = TRUE;"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement, last));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_target_lifetime_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_TARGET_LIFETIME_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Target lifetime|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 target lifetime mutations: " + killed + "/12; one public parent pin, private storage ownership, AddRef/Release, cleanup order, resource unlink and allocation/emit failure atomicity");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
