"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-declaration-fvf-mut-"));
function mutate(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} const mutationStart = m.index; const body = source.slice(mutationStart, end); assert(body.includes(before), name + " " + before); return source.slice(0, mutationStart) + body.replace(before, after) + source.slice(end);}
const cases = [
    ["XYZ maps to RHW", "declaration_fvf", "return D3DFVF_XYZ;", "return D3DFVF_XYZRHW;"],
    ["POSITIONT maps to XYZ", "declaration_fvf", "return D3DFVF_XYZRHW;", "return D3DFVF_XYZ;"],
    ["user constructor omits association", "device_create_vertex_declaration", "decl->fvf = declaration_fvf(elements, count);", "decl->fvf = 0;"],
    ["multiple elements inferred as position only", "declaration_fvf", "count != 1", "count == 0"],
    ["nonzero usage index inferred", "declaration_fvf", "|| e->UsageIndex", "|| FALSE"],
    ["nonzero stream inferred", "declaration_fvf", "e->Stream || e->Offset", "FALSE || e->Offset"],
    ["nonzero offset inferred", "declaration_fvf", "e->Stream || e->Offset", "e->Stream || FALSE"],
    ["nondefault method inferred", "declaration_fvf", "e->Method != D3DDECLMETHOD_DEFAULT", "FALSE"],
    ["user aliases converted cache", "device_fvf_declaration", "if (!decl->fvf_converted || decl->fvf != fvf) continue;", "if (decl->fvf != fvf) continue;"],
    ["user tagged as converted", "device_create_vertex_declaration", "decl->fvf = declaration_fvf(elements, count);", "decl->fvf = declaration_fvf(elements, count);decl->fvf_converted = TRUE;"],
    ["cache origin omitted", "device_fvf_declaration", "decl->fvf_converted = TRUE;", "decl->fvf_converted = FALSE;"],
];

try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, mutate(name, before, after)); const r = spawnSync(process.execPath, [__dirname + "/d9wg_declaration_fvf_test.js"], {env: {...process.env, D9WG_DECLARATION_FVF_SOURCE: file}, encoding: "utf8", timeout: 30000}), output = r.stdout + r.stderr; assert(!r.error, label); assert.notEqual(r.status, 0, "survived " + label); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert.match(output, /Target binding line|Assertion.*failed|Assertion failed/, label + " must fail a runtime assertion: " + output); console.log("KILL " + label);} console.log("PASS D3D9 user declaration FVF mutations: 11/11, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
