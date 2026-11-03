"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-binding-mut-"));
function mutate(s, name, old, replacement, last) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name); let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end), at = last ? body.lastIndexOf(old) : body.indexOf(old); assert(at >= 0, name + ": " + old);
    return s.slice(0, m.index) + body.slice(0, at) + replacement + body.slice(at + old.length) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement, last = false] of [
        ["private binding not retained", "surface_binding_add_ref", "InterlockedIncrement(&surface->binding_refs);", "IDirect3DSurface9_AddRef(&surface->iface);"] ,
        ["private binding not released", "surface_binding_release", "!InterlockedDecrement(&surface->binding_refs)", "0"] ,
        ["storage freed with public references", "surface_binding_release", "&& !InterlockedCompareExchange(&surface->refcount, 0, 0)", "&& 1"] ,
        ["public zero destroys bound storage", "surface_release", "if (!InterlockedCompareExchange(&surface->binding_refs, 0, 0))", "if (1)"] ,
        ["revival omits device pin", "surface_add_ref", "if (refs == 1)\n        device_child_add_ref(surface->device);", "if (0 && refs == 1)\n        device_child_add_ref(surface->device);", true] ,
        ["extra public references add parent pins", "surface_add_ref", "if (refs == 1)\n        device_child_add_ref(surface->device);", "if (1)\n        device_child_add_ref(surface->device);", true] ,
        ["public zero leaks device pin", "surface_release", "if (release_device)", "if (0 && release_device)"] ,
        ["zero public Release underflows", "surface_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0))", "if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0))", true] ,
        ["cleanup lacks temporary device pin", "device_release", "InterlockedIncrement(&device->refcount);", "(void)device;"] ,
        ["cleanup leaks temporary device pin", "device_release", "refs = (ULONG)InterlockedDecrement(&device->refcount);", "refs = (ULONG)InterlockedCompareExchange(&device->refcount, 0, 0);", true] ,
        ["RT binding changes public refs", "device_set_render_target", "surface_binding_add_ref(surface);", "IDirect3DSurface9_AddRef(surface_iface);"] ,
        ["depth binding changes public refs", "device_set_depth_stencil_surface", "surface_binding_add_ref(surface);", "IDirect3DSurface9_AddRef(surface_iface);"] ,
        ["RT cleanup returns a nonexistent public ref", "device_clear_bindings", "if (surface) surface_binding_release(surface);", "if (surface) IDirect3DSurface9_Release(&surface->iface);"] ,
        ["depth cleanup returns a nonexistent public ref", "device_clear_bindings", "        surface_binding_release(surface);", "        IDirect3DSurface9_Release(&surface->iface);"] ,
        ["Reset subtracts internal slots from public refs", "reset_texture_owned_refs", "(surface->swap_chain ? reset_surface_owned_refs(device, surface) : 0u)", "reset_surface_owned_refs(device, surface)"] ,
        ["texture child uses private counter", "surface_uses_private_bindings", "!surface->texture_child", "surface->texture_child"] ,
        ["cube child uses private counter", "surface_uses_private_bindings", "!surface->cube_texture", "1"] ,
        ["chain buffer uses private counter", "surface_uses_private_bindings", "!surface->swap_chain", "1"] ,
        ["implicit depth incorrectly uses public counter", "surface_uses_private_bindings", "return !surface->texture_child", "return !surface->auto_depth_stencil && !surface->texture_child"] ,
        ["same target released before retaining new binding", "device_set_render_target", "if (surface)\n        surface_binding_add_ref(surface);\n    device->render_target_surfaces[index] = surface;\n    if (old_surface)\n        surface_binding_release(old_surface);", "if (old_surface)\n        surface_binding_release(old_surface);\n    if (surface)\n        surface_binding_add_ref(surface);\n    device->render_target_surfaces[index] = surface;"] ,
        ["public zero Release lacks exit marker", "surface_release", "TRACE_MARK_EXIT(\"Surface.Release\", 0, iface);", "(void)iface;", true],
        ["private destruction creates a public Release exit", "surface_destroy_storage", "TRACE_SURFACE_FREED(iface);", "TRACE_MARK_EXIT(\"Surface.Release\", 0, iface); TRACE_SURFACE_FREED(iface);"],
        ["repeated zero Release lacks exit marker", "surface_release", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0)) {\n        TRACE_MARK_EXIT(\"Surface.Release\", 0, iface);", "if (!InterlockedCompareExchange(&surface->refcount, 0, 0)) {\n        (void)iface;"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement, last));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_target_binding_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_TARGET_BINDING_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Target binding|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 target-binding mutations: " + killed + "/23; balanced public-only release markers, internal/public counts, zero revival and parent pins, Reset blockers, safe final cleanup, target classes and same-object binding order");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
