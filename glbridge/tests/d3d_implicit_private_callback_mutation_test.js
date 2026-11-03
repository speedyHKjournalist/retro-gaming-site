"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d3d-implicit-private-callback-mut-"));
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
        const source = fs.readFileSync(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        const ref = "InterlockedCompareExchange(&device->refcount, 0, 0) == 1";
        const back = api === 8 ? "device->back_buffer_private_data" : "device->implicit_back_buffer->private_data";
        const depth = api === 8 ? "device->depth_surface_private_data" : "device->implicit_depth_stencil->private_data";
        const changes = [
            ["temporary self pin missing", "InterlockedIncrement(&device->refcount);", "(void)device;"],
            ["temporary self pin leaked", "refs = (ULONG)InterlockedDecrement(&device->refcount);\n        device->releasing_owned_refs = FALSE;", "refs = (ULONG)InterlockedCompareExchange(&device->refcount, 0, 0);\n        device->releasing_owned_refs = FALSE;"],
            ["reentrant insertion drained only once", "while (" + ref, "if (" + ref],
            ["live public parent/private stores drained", ref, "InterlockedCompareExchange(&device->refcount, 0, 0) >= 1", true],
            ["retained parent loses pending depth data", "if (" + ref, "if (1"],
            ["back entries detached without release", "d9wg_private_clear(&" + back + ", &d9wg_private_ops);", "ZeroMemory(&" + back + ", sizeof(" + back + "));"],
            ["depth entries detached without release", "d9wg_private_clear(&" + depth + ", &d9wg_private_ops);", "ZeroMemory(&" + depth + ", sizeof(" + depth + "));"],
        ];
        for (const [label, old, replacement, all = false] of changes) {
            let changed = mutate(source, "device_release", old, replacement);
            if (all) changed = changed.replaceAll(old, replacement);
            const file = dir + "/frontend.c"; fs.writeFileSync(file, changed);
            const r = spawnSync(process.execPath, [__dirname + "/d9wg_private_surface_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_PRIVATE_API: String(api), ["D" + api + "WG_PRIVATE_SURFACE_SOURCE"]: file}}), output = r.stdout + r.stderr;
            assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Private surface|Assertion|signal=/.test(output), output); killed++;
        }
    }
    console.log("PASS implicit private-callback mutations: " + killed + "/14; both device finalizers, temporary pins, reentrant drains, public-parent/store preservation and exact private releases");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
