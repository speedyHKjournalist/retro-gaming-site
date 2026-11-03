"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-additional-binding-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    let killed = 0;
    for (const api of [9]) {
        const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8");
        const changes = [
            ["chain binding uses public count", "surface_uses_private_bindings", "return !surface->texture_child", "return !surface->swap_chain && !surface->texture_child"],
            ["chain add leaks public parent pin", "surface_binding_add_ref", "if (surface_uses_private_bindings(surface))", "if (surface_uses_private_bindings(surface) && !surface->swap_chain)"],
            ["private counter not returned", "surface_binding_release", "!InterlockedDecrement(&surface->binding_refs)", "0"],
            ["last unbind destroys public chain", "surface_binding_release", "if (!InterlockedCompareExchange(&chain->refcount, 0, 0))", "if (1)"],
            ["last unbind frees view outside live chain", "surface_binding_release", "if (surface->swap_chain)", "if (0 && surface->swap_chain)"],
            ["public surface zero ignores internal binds", "surface_release", "&& !InterlockedCompareExchange(&surface->binding_refs, 0, 0)", "&& 1"],
            ["public chain zero ignores internal binds", "swap_chain_release", "&& !InterlockedCompareExchange(&surface->binding_refs, 0, 0)", "&& 1"],
            ["physical chain remains linked", "swap_chain_destroy_storage", "*link = chain->next_device_chain;", "*link = chain;"],
            ["physical view storage leaked", "swap_chain_destroy_storage", "surface_destroy_storage(surface);", "(void)surface;"],
            ["chain public pin leaked", "swap_chain_release", "device_child_release(device);", "(void)device;"],
            ["surface public pin leaked", "surface_release", "device_child_release(device);", "(void)device;"],
            ["Reset discounts private chain slots", "reset_surface_owned_refs", "if (surface && surface_uses_private_bindings(surface))", "if (0 && surface && surface_uses_private_bindings(surface))", "reset"],
        ];
        for (const [label, fn, old, replacement, harness = "private"] of changes) {
            const changed = mutate(source, fn, old, replacement);
            const file = dir + "/frontend.c"; fs.writeFileSync(file, changed);
            const script = harness === "reset" ? "d9wg_reset_resources_test.js" : "d9wg_private_surface_test.js";
            const env = harness === "reset" ? {D9WG_RESET_FRONTEND: file} : {D9WG_PRIVATE_API: String(api), D9WG_PRIVATE_SURFACE_SOURCE: file};
            const r = spawnSync(process.execPath, [__dirname + "/" + script], {encoding: "utf8", timeout: 30000, env: {...process.env, ...env}}), output = r.stdout + r.stderr;
            assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Private surface|Reset resource API|Assertion|signal=/.test(output), output); killed++;
        }
    }
    console.log("PASS additional binding mutations: " + killed + "/12; independent public/internal counts, both public release guards, physical storage ownership, last-unbind, parent pins and Reset accounting");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
