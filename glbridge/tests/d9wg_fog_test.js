"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-fog-"));
try {
    const input = path.join(directory, "fog.c"), binary = path.join(directory, "fog");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_fog_cases.h"
int main(void) {int error=fog_cases(200000);if(error){fprintf(stderr,"fog mismatch header line %d\\n",error);return 1;}
puts("PASS C vertex fog: analytic linear/depth/range/reversed bounds and 200000 exponential cases vs libm");return 0;}
`);
    const sources = ["blend", "vertex", "fvf", "math", "lighting", "tnl_math"].map(name => path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, input, ...sources, "-lm", "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
