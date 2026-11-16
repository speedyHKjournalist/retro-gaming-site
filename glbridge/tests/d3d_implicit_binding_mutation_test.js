"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d3d-implicit-binding-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    let killed = 0;
    for (const api of [8,9]) {
        const source = readComparisonSource(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        const changes = api === 8 ? [
            ["implicit binding adds public ref", "surface_binding_add_ref", "if (surface_is_implicit(surface))", "if (0 && surface_is_implicit(surface))"],
            ["implicit unbind returns public ref", "surface_binding_release", "if (surface_is_implicit(surface))", "if (0 && surface_is_implicit(surface))"],
            ["Reset discounts private depth binding", "device_has_reset_blockers", "(UINT)device->implicit_depth_stencil->refcount, 0,", "(UINT)device->implicit_depth_stencil->refcount, device->depth_surface == device->implicit_depth_stencil ? 1u : 0u,"],
        ] : [
            ["implicit back uses public binding", "surface_uses_private_bindings", "return !surface->texture_child", "return !surface->swap_chain && !surface->texture_child"],
            ["implicit depth uses public binding", "surface_uses_private_bindings", "return !surface->texture_child", "return !surface->auto_depth_stencil && !surface->texture_child"],
            ["last implicit unbind frees cache", "surface_binding_release", "&& !surface_is_implicit(surface)", "&& 1"],
            ["Reset discounts private implicit slots", "reset_surface_owned_refs", "if (surface && surface_uses_private_bindings(surface))", "if (0 && surface && surface_uses_private_bindings(surface))"],
            ["implicit add leaks public parent pin", "surface_binding_add_ref", "if (surface_uses_private_bindings(surface))", "if (surface_uses_private_bindings(surface) && !surface_is_implicit(surface))"],
            ["implicit release consumes public parent pin", "surface_binding_release", "if (!surface_uses_private_bindings(surface))", "if (!surface_uses_private_bindings(surface) || surface_is_implicit(surface))"],
            ["implicit release leaks private counter", "surface_binding_release", "!InterlockedDecrement(&surface->binding_refs)", "0"],
        ];
        for (const [label, fn, old, replacement] of changes) {
            const changed = mutate(source, fn, old, replacement);
            const file = dir + "/frontend.c"; fs.writeFileSync(file, changed);
            const r = spawnSync(process.execPath, [__dirname + "/d9wg_implicit_identity_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_IMPLICIT_API: String(api), ["D" + api + "WG_IMPLICIT_FRONTEND"]: file}}), output = r.stdout + r.stderr;
            assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Implicit identity API|Assertion|signal=/.test(output), output); killed++;
        }
    }
    console.log("PASS implicit binding mutations: " + killed + "/10; both APIs, public/internal counters, one parent pin, cache storage after unbind and public-only Reset blockers");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
