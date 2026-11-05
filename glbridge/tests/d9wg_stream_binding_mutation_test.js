"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-stream-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} const mutationStart = m.index; const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["NULL loses live offset", "device_set_stream_source", "offset = device->streams[stream].offset;", "offset = 0;"],
    ["NULL loses live stride", "device_set_stream_source", "stride = device->streams[stream].stride;", "stride = 0;"],
    ["recorded NULL skips normalization", "device_set_stream_source", "if (!buffer) {", "if (!buffer && !device->recording_result) {"],
    ["recorded NULL reads prior recording offset", "device_set_stream_source", "offset = device->streams[stream].offset;", "offset = device->recording_result ? device->recording_result->streams[stream].offset : device->streams[stream].offset;"],
    ["recorded NULL reads prior recording stride", "device_set_stream_source", "stride = device->streams[stream].stride;", "stride = device->recording_result ? device->recording_result->streams[stream].stride : device->streams[stream].stride;"],
    ["internal NULL loses captured offset", "device_set_stream_binding", "device->streams[stream].offset = offset;", "device->streams[stream].offset = buffer ? offset : 0;"],
    ["internal NULL loses captured stride", "device_set_stream_binding", "device->streams[stream].stride = stride;", "device->streams[stream].stride = buffer ? stride : 0;"],
    ["successful UP keeps old offset", "clear_stream_zero_after_up", "device->streams[0].offset = 0;", "(void)device->streams[0].offset;"],
    ["successful UP keeps old stride", "clear_stream_zero_after_up", "device->streams[0].stride = 0;", "(void)device->streams[0].stride;"],
    ["NULL getter hides retained offset", "device_get_stream_source", "*offset_out = device->streams[stream].offset;", "*offset_out = device->streams[stream].buffer ? device->streams[stream].offset : 0;"],
    ["NULL getter hides retained stride", "device_get_stream_source", "*stride_out = device->streams[stream].stride;", "*stride_out = device->streams[stream].buffer ? device->streams[stream].stride : 0;"],
    ["Apply routes NULL through public normalization", "state_block_apply_shared", "device_set_stream_binding(device, index,\n                    block->streams[index].buffer,", "IDirect3DDevice9_SetStreamSource(iface, index,\n                    block->streams[index].buffer ? &block->streams[index].buffer->iface : NULL,"],
];

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_stream_binding_test.js"], {env: {...process.env, D9WG_STREAM_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 stream metadata mutations: 12/12, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
