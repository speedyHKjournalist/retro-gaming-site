"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-texture-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert.equal(body.split(before).length, 2, name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["sampler exposes public reference", "device_set_texture", "if (texture) texture_binding_add_ref(texture);", "if (texture) IDirect3DTexture9_AddRef(&texture->iface);"],
    ["target exposes public parent reference", "surface_binding_add_ref", "texture_binding_add_ref(surface->texture);", "IDirect3DTexture9_AddRef(&surface->texture->iface);"],
    ["public-zero leaks device pin", "texture_release", "if (!private_storage)", "if (FALSE && !private_storage)"],
    ["revival omits device pin", "texture_add_ref", "if (refs == 1 && !texture->private_back_buffer)", "if (FALSE && refs == 1 && !texture->private_back_buffer)"],
    ["every public ref adds device pin", "texture_add_ref", "if (refs == 1 && !texture->private_back_buffer)", "if (refs > 0 && !texture->private_back_buffer)"],
    ["zero public Release underflows", "texture_release", "if (!InterlockedCompareExchange(&texture->refcount, 0, 0))", "if (FALSE && !InterlockedCompareExchange(&texture->refcount, 0, 0))"],
    ["private retain counts twice", "texture_binding_add_ref", "InterlockedIncrement(&texture->binding_refs);", "InterlockedIncrement(&texture->binding_refs);InterlockedIncrement(&texture->binding_refs);"],
    ["private release omitted", "texture_binding_release", "if (!InterlockedDecrement(&texture->binding_refs)", "if (FALSE && !InterlockedDecrement(&texture->binding_refs)"],
    ["Reset discounts sampler private refs", "reset_texture_owned_refs", "    UINT refs = 0;", "    UINT refs = 0;\n    if (kind == D9WG_RESOURCE_TEXTURE_2D) refs++;"],
    ["Reset discounts target private ref", "reset_texture_owned_refs", "if (!surface->texture_child) {", "if (surface->texture_child) refs++;\n            if (!surface->texture_child) {"],
    ["GetTexture fails to revive", "device_get_texture", "if (*texture_out)", "if (FALSE && *texture_out)"],
    ["level query adds duplicate public ref", "texture_get_surface_level", "IDirect3DTexture9_AddRef(iface);", "IDirect3DTexture9_AddRef(iface);IDirect3DTexture9_AddRef(iface);"],
    ["device cleanup uses public release", "device_clear_bindings", "if (texture) texture_binding_release(texture);", "if (texture) IDirect3DTexture9_Release(&texture->iface);"],
];
try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_texture_binding_test.js"], {env: {...process.env, D9WG_TEXTURE_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 texture binding mutations: 13/13, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
