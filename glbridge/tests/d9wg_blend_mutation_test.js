"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_blend.c"), "utf8");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-blend-mutants-"));
const mutations = [
    ["normal matrices use world-view directly", "d9wg_normal_matrix(matrix->world_view, matrix->normal);", "for(unsigned i=0;i<16;i++)matrix->normal[i]=matrix->world_view[i];"],
    ["packed colors lose RGBA order", "color_order[4] = {2,1,0,3}", "color_order[4] = {0,1,2,3}"],
    ["last weight exceeds the remainder", "weights[blend->matrix_count - 1u] = remainder;", "weights[blend->matrix_count - 1u] = remainder + .25f;"],
    ["D3D7 world gap ignored", "(index + (index ? blend->world_gap : 0u))*64u", "index*64u"],
    ["FLOAT1 implicit w defaults to zero", "float index = i == 3 ? 1.0f : 0.0f;", "float index = 0.0f;"],
    ["invalid indices silently clamped", "if (indices[i] >= blend->world_count) return 0;", "if (indices[i] >= blend->world_count) indices[i]=blend->world_count-1u;"],
];
try {
    for (const [label, before, after] of mutations) {
        assert.equal(original.split(before).length, 2, "unique C mutation: " + label);
        const target = path.join(directory, "blend.c");fs.writeFileSync(target, original.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, "d9wg_blend_test.js")],
            { encoding: "utf8", env: { ...process.env, D9WG_BLEND_SOURCE: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /blend mismatch header line/, "must fail an executed assertion: " + label);
    }
    console.log("PASS C vertex blend mutations: 6/6 semantic errors detected; production sources untouched");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
