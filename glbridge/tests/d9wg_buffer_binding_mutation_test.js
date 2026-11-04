"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-buffer-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert.equal(body.split(before).length, 2, name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [];
for (const [kind, iface, setter, getter, slot, up] of [["vb", "IDirect3DVertexBuffer9", "device_set_stream_binding", "device_get_stream_source", "device->streams[stream].buffer", "clear_stream_zero_after_up"], ["ib", "IDirect3DIndexBuffer9", "device_set_indices", "device_get_indices", "device->index_buffer", "clear_indices_after_indexed_up"]]) {
    cases.push(
        [kind + " binding exposes public ref", setter, "if (buffer) " + kind + "_binding_add_ref(buffer);", "if (buffer) " + iface + "_AddRef(&buffer->iface);"],
        [kind + " zero leaks device pin", kind + "_release", "device_child_release(device);", "if (FALSE) device_child_release(device);"],
        [kind + " revival omits device pin", kind + "_add_ref", "if (refs == 1)", "if (FALSE && refs == 1)"],
        [kind + " each public ref pins device", kind + "_add_ref", "if (refs == 1)", "if (refs > 0)"],
        [kind + " zero Release underflows", kind + "_release", "if (!InterlockedCompareExchange(&buffer->refcount, 0, 0))", "if (FALSE && !InterlockedCompareExchange(&buffer->refcount, 0, 0))"],
        [kind + " private retain doubled", kind + "_binding_add_ref", "InterlockedIncrement(&buffer->binding_refs);", "InterlockedIncrement(&buffer->binding_refs);InterlockedIncrement(&buffer->binding_refs);"],
        [kind + " private release omitted", kind + "_binding_release", "if (!InterlockedDecrement(&buffer->binding_refs)", "if (FALSE && !InterlockedDecrement(&buffer->binding_refs)"],
        [kind + " Reset discounts private refs", "reset_buffer_owned_refs", "return 0;", kind === "ib" ? "return index_buffer ? 1u : 0u;" : "return index_buffer ? 0u : 1u;"],
        [kind + " getter fails to revive", getter, "if (*buffer_out)", "if (FALSE && *buffer_out)"],
        [kind + " cleanup uses public release", "device_clear_bindings", kind + "_binding_release(buffer);", iface + "_Release(&buffer->iface);"],
        [kind + " UP cleanup uses public release", up, "if (old) " + kind + "_binding_release(old);", "if (old) " + iface + "_Release(&old->iface);"],
        [kind + " shadow leaked", kind + "_destroy_storage", "HeapFree(GetProcessHeap(), 0, buffer->shadow);", "(void)buffer->shadow;"],
        [kind + " live binding dropped", setter, slot + " = buffer;", slot + " = NULL;"],
    );
}
cases.push(["VB clip records leaked", "vb_destroy_storage", "d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL);", "(void)&d9wg_clip_buffer_free;(void)&d9wg_vertex_windows_clip_release;"]);

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_buffer_binding_test.js"], {env: {...process.env, D9WG_BUFFER_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 buffer binding mutations: 27/27, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
