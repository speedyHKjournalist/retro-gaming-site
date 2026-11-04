"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-up-failure-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} const mutationStart = m.index; const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["failed UP keeps stream0 bound", "clear_bindings_after_up_missing_format", "device->streams[0].buffer = NULL;", "(void)device->streams[0].buffer;"],
    ["failed UP zeroes offset", "clear_bindings_after_up_missing_format", "if (old) vb_binding_release(old);", "device->streams[0].offset = 0;if (old) vb_binding_release(old);"],
    ["failed UP zeroes stride", "clear_bindings_after_up_missing_format", "if (old) vb_binding_release(old);", "device->streams[0].stride = 0;if (old) vb_binding_release(old);"],
    ["failed UP leaks private vertex hold", "clear_bindings_after_up_missing_format", "if (old) vb_binding_release(old);", "if (FALSE && old) vb_binding_release(old);"],
    ["failed UP releases public vertex view", "clear_bindings_after_up_missing_format", "vb_binding_release(old)", "IDirect3DVertexBuffer9_Release(&old->iface)"],
    ["nonindexed failure clears indices", "clear_bindings_after_up_missing_format", "if (indexed) clear_indices_after_indexed_up(device);", "if (TRUE || indexed) clear_indices_after_indexed_up(device);"],
    ["indexed failure keeps indices", "clear_bindings_after_up_missing_format", "if (indexed) clear_indices_after_indexed_up(device);", "if (FALSE && indexed) clear_indices_after_indexed_up(device);"],
    ["nonindexed missing format skips cleanup", "device_draw_primitive_up", "clear_bindings_after_up_missing_format(device, FALSE);", "(void)&clear_bindings_after_up_missing_format;"],
    ["indexed missing format skips cleanup", "draw_indexed_up", "if (public_up) clear_bindings_after_up_missing_format(device, TRUE);", "if (FALSE && public_up) clear_bindings_after_up_missing_format(device, TRUE);"],
    ["patch missing format clears public bindings", "draw_indexed_up", "if (public_up) clear_bindings_after_up_missing_format(device, TRUE);", "if (TRUE || public_up) clear_bindings_after_up_missing_format(device, TRUE);"],
    ["successful nonindexed loses cleanup", "device_draw_primitive_up", "clear_stream_zero_after_up(device);", "(void)&clear_stream_zero_after_up;"],
    ["successful indexed loses cleanup", "device_draw_indexed_primitive_up", "clear_indices_after_indexed_up(device);", "(void)&clear_indices_after_indexed_up;"],
];

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_up_failure_test.js"], {env: {...process.env, D9WG_UP_FAILURE_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 missing-format UP mutations: 12/12, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
