"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-user-planes-"));
try {
    const input = path.join(directory, "planes.c"), binary = path.join(directory, "planes");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_clip_user_cases.h"
int main(void){int error=clip_user_cases(100000);if(error){fprintf(stderr,"clip user mismatch header line %d\\n",error);return 1;}
puts("PASS C user clipping: 100000 twelve-plane triangle/segment cases, barycentric interpolation, invalid data, indexed world blending and singular projection");return 0;}
`);
    const sources = ["math", "clip", "blend", "vertex", "fvf", "lighting", "tnl_math"].map(name => process.env["D9WG_" + name.toUpperCase() + "_SOURCE"] || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1500));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
