"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-vertex-"));
try {
    const input = path.join(directory, "vertex.c"), binary = path.join(directory, "vertex");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_vertex_cases.h"
int main(void) { int error=vertex_cases(200000);if(error){fprintf(stderr,"vertex mismatch header line %d\\n",error);return 1;}
puts("PASS shared C vertex processing: 200000 independent range probes, unaligned streams, viewport/XYZW, generated colors with DONOTCOPYDATA, passthrough guards and retained texture components");return 0;}
`);
    const sources = ["blend", "vertex", "fvf", "math", "lighting", "tnl_math"].map(name => name === "vertex" && process.env.D9WG_VERTEX_SOURCE || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
