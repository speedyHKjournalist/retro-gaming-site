"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_vertex.c"), "utf8");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-vertex-mutants-"));
const mutations = [
    ["generated texture data suppressed by DONOTCOPYDATA", "if (!copy_data && !generated && !transformed) continue;", "if (!copy_data) continue;", "d9wg_texcoord_test.js"],
    ["COUNT1 skips matrix", "if (transformed) {", "if (transformed > 1) {", "d9wg_texcoord_test.js"],
    ["projection divides before rasterization", "floats = transformed;", "if(flags & 256u)for(unsigned c=0;c+1<transformed;c++)coordinate[c]/=coordinate[transformed-1];floats = transformed;", "d9wg_texcoord_test.js"],
    ["source set forced to zero", "source_set = index & 0xffffu;", "source_set = 0;", "d9wg_texcoord_test.js"],
    ["vertex fog is never generated", "if (vertex_state && vertex_state->fog_mode && d->specular >= 0 &&", "if (0 && vertex_state && vertex_state->fog_mode && d->specular >= 0 &&"],
    ["DONOTCOPYDATA suppresses lighting", "(copy_data || lighting->flags.lighting)", "(copy_data)"],
    ["DONOTCOPYDATA still copies textures", "if (!copy_data && !generated && !transformed) continue;", "if (0) continue;"],
    ["extra texture components overwritten", "floats < d->texcoord_floats[i]", "floats > d->texcoord_floats[i]"],
    ["source index wraps before validation", "if (start > available / stride) return 0;", "/* missing index guard */"],
];
try {
    for (const [label, before, after, detector = "d9wg_vertex_test.js"] of mutations) {
        assert.equal(original.split(before).length, 2, "unique C mutation: " + label);
        const target = path.join(directory, "vertex.c");fs.writeFileSync(target, original.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, detector)],
            { encoding: "utf8", env: { ...process.env, D9WG_VERTEX_SOURCE: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /(vertex|texcoord) mismatch header line/, "must fail an executed assertion, not compilation: " + label);
    }
    console.log("PASS shared C vertex mutations: 9/9 semantic errors detected; production sources untouched");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
