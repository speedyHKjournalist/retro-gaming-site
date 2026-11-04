"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-state-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [];
for (const [kind, field, object, iface, member] of [["texture", "textures[stage]", "texture", "IDirect3DTexture9", "iface"], ["cube", "cube_textures[stage]", "cube", "IDirect3DCubeTexture9", "iface"], ["volume_texture", "volume_textures[stage]", "volume", "IDirect3DVolumeTexture9", "iface"], ["vb", "streams[index].buffer", "buffer", "IDirect3DVertexBuffer9", "iface"], ["ib", "index_buffer", "indices", "IDirect3DIndexBuffer9", "iface"], ["decl", "vertex_declaration", "decl", "IDirect3DVertexDeclaration9", "iface"], ["shader", "vertex_shader", "vertex_shader", "IDirect3DVertexShader9", "iface.vertex"], ["shader", "pixel_shader", "pixel_shader", "IDirect3DPixelShader9", "iface.pixel"]]) {
    cases.push(
        [field + " capture uses public refs", "state_block_capture", kind + "_binding_add_ref(block->" + field + ");", iface + "_AddRef(&block->" + field + "->" + member + ");"],
        [field + " capture omits storage retain", "state_block_capture", kind + "_binding_add_ref(block->" + field + ");", "(void)block->" + field + ";"],
        [field + " cleanup uses public release", "state_block_release_references", "if (" + object + ") " + kind + "_binding_release(" + object + ");", "if (" + object + ") " + iface + "_Release(&" + object + "->" + member + ");"],
        [field + " cleanup leaks storage", "state_block_release_references", "if (" + object + ") " + kind + "_binding_release(" + object + ");", "(void)" + object + ";"],
    );
}
cases.push(["Capture accepts recording", "state_block_capture_method", "if (block->device->recording_result)", "if (FALSE && block->device->recording_result)"], ["Apply accepts recording", "state_block_apply_method", "if (block->device->recording_result)", "if (FALSE && block->device->recording_result)"], ["End loses completed block holds", "device_end_state_block", "device->recording_result = NULL;", "device->recording_result = NULL;state_block_release_references(block);"]);

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_state_binding_test.js"], {env: {...process.env, D9WG_STATE_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 stateblock binding mutations: 35/35, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
