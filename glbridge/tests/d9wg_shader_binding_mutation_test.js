"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-shader-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [];
for (const [kind, iface, setter, getter, slot] of [["decl", "IDirect3DVertexDeclaration9", "device_set_vertex_declaration", "device_get_vertex_declaration", "vertex_declaration"], ["vs", "IDirect3DVertexShader9", "device_set_vertex_shader", "device_get_vertex_shader", "vertex_shader"], ["ps", "IDirect3DPixelShader9", "device_set_pixel_shader", "device_get_pixel_shader", "pixel_shader"]]) {
    const object = kind === "decl" ? "decl" : "shader", helper = kind === "decl" ? "decl" : "shader", member = kind === "decl" ? "iface" : "iface." + (kind === "vs" ? "vertex" : "pixel");
    cases.push(
        [kind + " binding exposes public ref", setter, "if (" + object + ") " + helper + "_binding_add_ref(" + object + ");", "if (" + object + ") " + iface + "_AddRef(" + object + "_iface);"],
        [kind + " getter omits AddRef", getter, "if (*" + (kind === "decl" ? "decl" : "shader") + "_out)", "if (FALSE && *" + (kind === "decl" ? "decl" : "shader") + "_out)"],
        [kind + " cleanup uses public release", "device_clear_bindings", "device->" + slot + " = NULL;\n        " + helper + "_binding_release(" + object + ");", "device->" + slot + " = NULL;\n        " + iface + "_Release(&" + object + "->" + member + ");"],
        [kind + " live binding dropped", setter, "device->" + slot + " = " + object + ";", "device->" + slot + " = NULL;"],
        [kind + " getter returns null", getter, "? &device->" + slot + "->" + member, "? NULL /* &device->" + slot + "->" + member + " */"],
    );
}
for (const [kind, object, pub] of [["decl", "decl", "decl"], ["shader", "shader", "shader_public"]]) cases.push(
    [kind + " zero leaks device pin", pub + "_release", "device_child_release(device);", "if (FALSE) device_child_release(device);"],
    [kind + " revival omits device pin", pub + "_add_ref", "if (refs == 1)", "if (FALSE && refs == 1)"],
    [kind + " each public ref pins device", pub + "_add_ref", "if (refs == 1)", "if (refs > 0)"],
    [kind + " zero Release underflows", pub + "_release", "if (!InterlockedCompareExchange(&" + object + "->refcount, 0, 0))", "if (FALSE && !InterlockedCompareExchange(&" + object + "->refcount, 0, 0))"],
    [kind + " private retain doubled", kind + "_binding_add_ref", "InterlockedIncrement(&object->binding_refs);", "InterlockedIncrement(&object->binding_refs);InterlockedIncrement(&object->binding_refs);"],
    [kind + " private release omitted", kind + "_binding_release", "if (!InterlockedDecrement(&object->binding_refs)", "if (FALSE && !InterlockedDecrement(&object->binding_refs)"],
);
cases.push(
    ["shader QI omits revival device pin", "shader_query_interface", "shader_public_add_ref(shader);", "InterlockedIncrement(&shader->refcount);"],
    ["FVF cleanup uses public declaration release", "device_set_fvf", "if (old_decl) decl_binding_release(old_decl);", "if (old_decl) IDirect3DVertexDeclaration9_Release(&old_decl->iface);"],
    ["shader bytecode storage leaked", "shader_destroy_storage", "HeapFree(GetProcessHeap(), 0, shader->code);", "(void)shader->code;"],
);

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_shader_binding_test.js"], {env: {...process.env, D9WG_SHADER_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 shader binding mutations: 30/30, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
