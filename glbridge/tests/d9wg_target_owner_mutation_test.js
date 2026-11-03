"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-owner-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"); let killed = 0;
    for (const [label, name, old, replacement] of [
        ["foreign owner accepted", "device_set_render_target", "surface->device != device", "0"],
        ["local owner rejected", "device_set_render_target", "surface->device != device", "surface->device == device"],
        ["foreign vtable accepted", "device_set_render_target", "surface_iface->lpVtbl != &g_surface_vtbl", "0"],
        ["guard accepts either valid field", "device_set_render_target", "|| surface->device != device", "&& surface->device != device"],
        ["foreign reference retained before refusal", "device_set_render_target", "if (surface_iface && (surface_iface->lpVtbl", "if (surface_iface) IDirect3DSurface9_AddRef(surface_iface);\n    if (surface_iface && (surface_iface->lpVtbl"],
        ["old reference released before refusal", "device_set_render_target", "if (surface_iface && (surface_iface->lpVtbl", "if (device->render_target_surfaces[index]) IDirect3DSurface9_Release(&device->render_target_surfaces[index]->iface);\n    if (surface_iface && (surface_iface->lpVtbl"],
        ["slot changed before refusal", "device_set_render_target", "if (surface_iface && (surface_iface->lpVtbl", "device->render_target_surfaces[index] = surface;\n    if (surface_iface && (surface_iface->lpVtbl"],
        ["foreign owner success without binding", "device_set_render_target", "surface->device != device))\n        return TRACE_REFUSE(D3DERR_INVALIDCALL);", "surface->device != device))\n        return D3D_OK;"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, name, old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_target_owner_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_TARGET_OWNER_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Target owner|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 target-owner mutations: " + killed + "/8; foreign ownership/vtable, local controls, reference/slot rejection atomicity and failure HRESULT");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
