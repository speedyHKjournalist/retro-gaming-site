"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-volume-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert.equal(body.split(before).length, 2, name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["sampler exposes public reference", "device_set_texture", "if (volume) volume_texture_binding_add_ref(volume);", "if (volume) IDirect3DVolumeTexture9_AddRef(&volume->iface);"],
    ["public-zero leaks device pin", "volume_texture_release", "device_child_release(device);", "if (FALSE) device_child_release(device);"],
    ["revival omits device pin", "volume_texture_add_ref", "if (refs == 1)", "if (FALSE && refs == 1)"],
    ["every public ref adds device pin", "volume_texture_add_ref", "if (refs == 1)", "if (refs > 0)"],
    ["zero public Release underflows", "volume_texture_release", "if (!InterlockedCompareExchange(&texture->refcount, 0, 0))", "if (FALSE && !InterlockedCompareExchange(&texture->refcount, 0, 0))"],
    ["private retain counts twice", "volume_texture_binding_add_ref", "InterlockedIncrement(&texture->binding_refs);", "InterlockedIncrement(&texture->binding_refs);InterlockedIncrement(&texture->binding_refs);"],
    ["private release omitted", "volume_texture_binding_release", "if (!InterlockedDecrement(&texture->binding_refs)", "if (FALSE && !InterlockedDecrement(&texture->binding_refs)"],
    ["Reset discounts sampler private refs", "reset_texture_owned_refs", "    UINT refs = 0;", "    UINT refs = 0;\n    if (kind == D9WG_RESOURCE_TEXTURE_VOLUME) refs++;"],
    ["GetTexture fails to revive", "device_get_texture", "if (*texture_out)", "if (FALSE && *texture_out)"],
    ["level query adds duplicate public ref", "volume_texture_get_volume_level", "IDirect3DVolume9_AddRef(*out);", "IDirect3DVolume9_AddRef(*out);IDirect3DVolume9_AddRef(*out);"],
    ["device cleanup uses public release", "device_clear_bindings", "if (volume) volume_texture_binding_release(volume);", "if (volume) IDirect3DVolumeTexture9_Release(&volume->iface);"],
    ["cached volume level aliases zero", "volume_texture_get_volume_level", "&texture->levels[level].level_volume->iface", "&texture->levels[0].level_volume->iface"],
    ["cached volume views leaked", "volume_texture_destroy_storage", "HeapFree(GetProcessHeap(), 0, texture->levels[level].level_volume);", "(void)texture->levels[level].level_volume;"],
    ["public-zero cleanup omitted", "volume_texture_release", "if (!refs) {", "if (FALSE && !refs) {"],
];
try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_volume_binding_test.js"], {env: {...process.env, D9WG_VOLUME_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 volume binding mutations: 14/14, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
