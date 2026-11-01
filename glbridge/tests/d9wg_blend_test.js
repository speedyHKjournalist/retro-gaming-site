"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-blend-"));
try {
    const input = path.join(directory, "blend.c"), binary = path.join(directory, "blend");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_blend_cases.h"
int main(void) {int error=blend_cases(100000);if(error){fprintf(stderr,"blend mismatch header line %d\\n",error);return 1;}
puts("PASS C vertex blending: 1-3 weights, inverse normal matrices, UBYTE4/D3DCOLOR index order, entry255, D3D7 slots, lazy palette and 100000 analytic blends");return 0;}
`);
    const sources = ["blend", "vertex", "fvf", "math", "lighting", "tnl_math"].map(name => name === "blend" && process.env.D9WG_BLEND_SOURCE || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root,"runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
