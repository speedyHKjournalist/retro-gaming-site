"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_vertex.c"), "utf8");
const mutations = [
    ["XYZ output refused", "layout->legacy_xyz_output = 1;", "layout->legacy_xyz_output = 0;"],
    ["nominal XYZ cursor used", "uint32_t cursor = 16;", "uint32_t cursor = 12;"],
    ["destination normals copied", "layout->normal = layout->psize = -1;", "layout->psize = -1;"],
    ["destination dimension truncates legacy coordinates", "d->legacy_output || floats < d->texcoord_floats[i]", "floats < d->texcoord_floats[i]"],
    ["RHW spill suppressed", "for (unsigned i = 0; i < 4; ++i)\n            write_output_word", "for (unsigned i = 0; i < 3; ++i)\n            write_output_word"],
    ["bounded spill removed", "if (offset <= length && length - offset >= 4)", "if (offset <= length + 4ull)"],
];
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-legacy-mutants-"));
try {
    for(const [label, before, after] of mutations) {
        assert.equal(original.split(before).length, 2, "unique mutation " + label);
        const target = path.join(directory, "vertex.c");fs.writeFileSync(target, original.replace(before, after));
        const run = spawnSync(process.execPath, [path.join(__dirname, "d9wg_legacy_output_test.js")], { encoding: "utf8", env: { ...process.env, D9WG_VERTEX_SOURCE: target } });
        assert.notEqual(run.status, 0, "survived " + label);
        assert.match(run.stderr, /legacy output mismatch header line/, "must execute C assertion: " + label);
    }
    console.log("PASS native legacy output mutations: 6/6; production sources untouched");
} finally { fs.rmSync(directory, {recursive:true,force:true}); }
