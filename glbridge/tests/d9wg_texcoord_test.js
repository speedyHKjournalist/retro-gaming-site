"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-texcoord-"));
try {
    const input = path.join(directory, "texcoord.c"), binary = path.join(directory, "texcoord");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_texcoord_cases.h"
int main(void) {int error=texcoord_cases(100000);if(error){fprintf(stderr,"texcoord mismatch header line %d\\n",error);return 1;}
puts("PASS C texture coordinates: input routing, COUNT1-4, raw projected q, camera position/normal/reflection/sphere, normalization, missing sets and 100000 analytic transforms");return 0;}
`);
    const sources = ["clip", "blend", "vertex", "fvf", "math", "lighting", "tnl_math"].map(name => name === "vertex" && process.env.D9WG_VERTEX_SOURCE || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root,"runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
