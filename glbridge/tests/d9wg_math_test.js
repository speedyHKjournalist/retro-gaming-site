"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process"), { source } = require("./d9wg_math_harness.js");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-math-"));
try {
    const input = path.join(directory, "math.c"), binary = path.join(directory, "math");
    fs.writeFileSync(input, "#include <stdio.h>\n" + source() + `
int main(void) {
    int error=math_cases(100000);if(error) { fprintf(stderr,"math mismatch at generated line %d\\n",error);return 1; }
    printf("PASS actual C software math: %u frozen operation comparisons, row-vector order, colors, edge roots and unaligned SDK matrices\\n",math_comparisons);return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root,
        input, path.join(root, "runtime/d9wg_math.c"), "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1500) || String(run.error));
    process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
