"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-root-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} let mutationStart = m.index;
    if (name.startsWith("device_set_")) {
        const recording = source.indexOf("    if (device->recording_result) {", m.index);
        if (recording >= 0 && recording < end) {let at = source.indexOf("{", recording) + 1, nesting = 1; for (; nesting; at++) {if (source[at] === "{") nesting++; if (source[at] === "}") nesting--;} mutationStart = at;}
    }
    const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["external children trigger premature state clear", "device_release", "if (!refs) {\n        TRACE(\"Device.Release final-state", "if ((LONG)refs == device->child_parent_refs) {\n        TRACE(\"Device.Release final-state"],
    ["Begin creates a public device pin", "device_begin_state_block", "state_block_alloc(device, FALSE)", "state_block_alloc(device, TRUE)"],
    ["internal allocation ignores private flag", "state_block_alloc", "if (parent_ref) device_child_add_ref(device);", "device_child_add_ref(device);"],
    ["End omits parent pin", "device_end_state_block", "device_child_add_ref(device);", "if (FALSE) device_child_add_ref(device);"],
    ["End doubles parent pin", "device_end_state_block", "device_child_add_ref(device);", "device_child_add_ref(device);device_child_add_ref(device);"],
    ["End leaves private ownership flag", "device_end_state_block", "block->device_parent_ref = TRUE;", "block->device_parent_ref = FALSE;"],
    ["public block release leaks parent pin", "state_block_release", "if (parent_ref) device_child_release(device);", "if (FALSE && parent_ref) device_child_release(device);"],
    ["final release fails to cancel recording", "device_release_owned_references", "device_cancel_recording(device);", "(void)&device_cancel_recording;"],
    ["final release fails to drop live storage", "device_release_owned_references", "device_clear_bindings(device);", "(void)&device_clear_bindings;"],
];

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_root_binding_test.js"], {env: {...process.env, D9WG_ROOT_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 external-child root mutations: 9/9, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
