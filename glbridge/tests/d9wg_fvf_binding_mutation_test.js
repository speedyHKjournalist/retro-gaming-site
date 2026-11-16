"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-fvf-binding-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} const mutationStart = m.index; const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["zero FVF is no longer a no-op", "device_set_fvf", "if (!fvf) return D3D_OK;", "if (FALSE && !fvf) return D3D_OK;"],
    ["cache lookup misses exact identity", "device_fvf_declaration", "if (!decl->fvf_converted || decl->fvf != fvf) continue;", "if (TRUE || !decl->fvf_converted || decl->fvf != fvf) continue;"],
    ["cache aliases distinct FVFs", "device_fvf_declaration", "if (!decl->fvf_converted || decl->fvf != fvf) continue;", "if (!decl->fvf_converted) continue;"],
    ["cached declaration starts public", "device_fvf_declaration", "decl->binding_refs = 1;", "decl->binding_refs = 1;decl->refcount = 1;"],
    ["cache omits storage hold", "device_fvf_declaration", "decl->binding_refs = 1;", "decl->binding_refs = 0;"],
    ["cache adds a parent pin", "device_fvf_declaration", "decl->device = device;", "decl->device = device;device_child_add_ref(device);"],
    ["converted elements are empty", "device_fvf_declaration", "CopyMemory(decl->elements, wire, count * sizeof(D3DVERTEXELEMENT9));", "(void)wire;"],
    ["cache ownership flag omitted", "device_fvf_declaration", "decl->fvf_cache_ref = TRUE;\n    decl->binding_refs = 1;", "decl->fvf_cache_ref = FALSE;\n    decl->binding_refs = 1;"],
    ["cache teardown leaks holds", "device_clear_fvf_cache", "decl_binding_release(decl);", "if (FALSE) decl_binding_release(decl);"],
    ["cache teardown is not idempotent", "device_clear_fvf_cache", "decl->fvf_cache_ref = FALSE;", "(void)decl->fvf_cache_ref;"],
    ["detached public cache view gets duplicate storage", "device_fvf_declaration", "if (!decl->fvf_cache_ref) {", "if (!decl->fvf_cache_ref) continue;\n        if (!decl->fvf_cache_ref) {"],
    ["cached declaration loses FVF when directly rebound", "device_set_vertex_declaration", "device->fvf = decl ? decl->fvf : 0;", "device->fvf = 0;"],
    ["FVF does not bind the converted declaration", "device_set_fvf", "device->vertex_declaration = decl;", "device->vertex_declaration = NULL;"],
    ["final device release omits cache cleanup", "device_release_owned_references", "device_clear_fvf_cache(device);", "(void)&device_clear_fvf_cache;"],
];

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_fvf_binding_test.js"], {env: {...process.env, D9WG_FVF_BINDING_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 FVF cache mutations: 14/14, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
