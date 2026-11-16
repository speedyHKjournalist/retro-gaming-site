"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-tnl-math-"));
try {
    const input = path.join(directory, "lighting_math.c"), binary = path.join(directory, "lighting_math");
    fs.writeFileSync(input, `
#include <stdio.h>
#include "tests/native/d9wg_tnl_math_cases.h"
int main(void) {
    double errors[2];int error=tnl_math_cases(200000,errors);
    if(error) { fprintf(stderr,"Lighting math mismatch at header line %d\\n",error);return 1; }
    printf("PASS CRT-free lighting math: 200000 powers, cone angles and square roots each vs libm; max absolute errors %.9g / %.9g\\n",errors[0],errors[1]);return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root,
        input, path.join(root, "runtime/d9wg_tnl_math.c"), "-lm", "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
