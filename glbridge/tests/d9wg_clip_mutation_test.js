"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_clip.c"), "utf8");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-clip-mutants-"));
const mutations = [
    ["near plane uses OpenGL range", "clip[2] < 0.0f", "clip[2] < -clip[3]"],
    ["far boundary excluded", "clip[2] > clip[3]", "clip[2] >= clip[3]"],
    ["clipping operation ignored", "clipping &&", "(clipping + 1u) &&"],
    ["stored w becomes reciprocal", "vertex->stored[3] = clip[3];", "vertex->stored[3] = vertex->screen[3];"],
    ["stored y uses screen orientation", "(clip[1] + clip[3])*0.5f", "(-clip[1] + clip[3])*0.5f"],
    ["guard band replaced with frustum", "(vertex->code & 48u)", "vertex->code"],
];
try {
    for (const [label, before, after] of mutations) {
        assert.equal(original.split(before).length, 2, "unique C mutation: " + label);
        const target = path.join(directory, "clip.c");fs.writeFileSync(target, original.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, "d9wg_clip_test.js")],
            { encoding: "utf8", env: { ...process.env, D9WG_CLIP_SOURCE: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /clip mismatch (header line|native case)/, "must fail an executed assertion: " + label);
    }
    console.log("PASS C clip mutations: 6/6 semantic errors detected; production sources untouched");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
