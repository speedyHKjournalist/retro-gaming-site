"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-cube-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert.equal(body.split(before).length, 2, name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["sampler exposes public reference", "device_set_texture", "if (cube) cube_binding_add_ref(cube);", "if (cube) IDirect3DCubeTexture9_AddRef(&cube->iface);"],
    ["target exposes public parent reference", "surface_binding_add_ref", "cube_binding_add_ref(surface->cube_texture);", "IDirect3DCubeTexture9_AddRef(&surface->cube_texture->iface);"],
    ["public-zero leaks device pin", "cube_release", "device_child_release(device);", "if (FALSE) device_child_release(device);"],
    ["revival omits device pin", "cube_add_ref", "if (refs == 1)", "if (FALSE && refs == 1)"],
    ["every public ref adds device pin", "cube_add_ref", "if (refs == 1)", "if (refs > 0)"],
    ["zero public Release underflows", "cube_release", "if (!InterlockedCompareExchange(&texture->refcount, 0, 0))", "if (FALSE && !InterlockedCompareExchange(&texture->refcount, 0, 0))"],
    ["private retain counts twice", "cube_binding_add_ref", "InterlockedIncrement(&texture->binding_refs);", "InterlockedIncrement(&texture->binding_refs);InterlockedIncrement(&texture->binding_refs);"],
    ["private release omitted", "cube_binding_release", "if (!InterlockedDecrement(&texture->binding_refs)", "if (FALSE && !InterlockedDecrement(&texture->binding_refs)"],
    ["Reset discounts sampler private refs", "reset_texture_owned_refs", "    UINT refs = 0;", "    UINT refs = 0;\n    if (kind == D9WG_RESOURCE_TEXTURE_CUBE) refs++;"],
    ["Reset discounts target private ref", "reset_texture_owned_refs", "if (kind == D9WG_RESOURCE_TEXTURE_2D && surface->texture", "if (kind == D9WG_RESOURCE_TEXTURE_CUBE && surface->cube_texture) refs++;\n        if (kind == D9WG_RESOURCE_TEXTURE_2D && surface->texture"],
    ["GetTexture fails to revive", "device_get_texture", "if (*texture_out)", "if (FALSE && *texture_out)"],
    ["level query adds duplicate public ref", "cube_get_cube_map_surface", "IDirect3DCubeTexture9_AddRef(iface);", "IDirect3DCubeTexture9_AddRef(iface);IDirect3DCubeTexture9_AddRef(iface);"],
    ["device cleanup uses public release", "device_clear_bindings", "if (cube) cube_binding_release(cube);", "if (cube) IDirect3DCubeTexture9_Release(&cube->iface);"],
];
try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_cube_binding_test.js"], {env: {...process.env, D9WG_CUBE_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 cube binding mutations: 13/13, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
