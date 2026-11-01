"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_vertex.c"), "utf8");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-vertex-mutants-"));
const mutations = [
    ["vertex fog is never generated", "if (vertex_state && vertex_state->fog_mode && d->specular >= 0)", "if (0 && vertex_state && vertex_state->fog_mode && d->specular >= 0)"],
    ["DONOTCOPYDATA suppresses lighting", "(copy_data || lighting->flags.lighting)", "(copy_data)"],
    ["DONOTCOPYDATA still copies textures", "if (!copy_data) continue;", "if (0) continue;"],
    ["extra texture components overwritten", "s->texcoord_floats[i] < d->texcoord_floats[i]", "s->texcoord_floats[i] > d->texcoord_floats[i]"],
    ["source index wraps before validation", "if (start > available / stride) return 0;", "/* missing index guard */"],
];
try {
    for (const [label, before, after] of mutations) {
        assert.equal(original.split(before).length, 2, "unique C mutation: " + label);
        const target = path.join(directory, "vertex.c");fs.writeFileSync(target, original.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, "d9wg_vertex_test.js")],
            { encoding: "utf8", env: { ...process.env, D9WG_VERTEX_SOURCE: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /vertex mismatch header line/, "must fail an executed assertion, not compilation: " + label);
    }
    console.log("PASS shared C vertex mutations: 5/5 semantic errors detected; production sources untouched");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
