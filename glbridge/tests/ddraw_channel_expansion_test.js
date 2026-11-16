#!/usr/bin/env node
// Proves the guest DLL and the host executor expand a narrow colour channel to
// eight bits by the identical rule.
//
// This is one comparison with two implementations on opposite sides of a DMA
// arena, and nothing else forces them to agree. A DirectDraw colour key is a
// value in the surface's own format -- 5 bits of red in RGB565 -- while the
// GPU compares 8-bit texels the host produced on upload. If the guest widens
// the key by one rule and the host widens the texels by another, the key
// misses on exactly the values where the rules differ, and the sprite that
// should have been keyed blits as a solid rectangle. Bit replication and
// truncated scaling differ at 24/31 (198 against 197), which is an ordinary
// colour, so this is not a corner case.
//
// The guest side is compiled and run for real rather than reimplemented here:
// a copy of the rule in the test would only prove the copy agrees with itself.
// Needs any host C compiler; skipped when there is none.

"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

function findCompiler() {
    for (const candidate of ["cc", "gcc", "clang"]) {
        const probe = spawnSync(candidate, ["--version"], { encoding: "utf8" });
        if (probe.status === 0) return candidate;
    }
    return null;
}

const compiler = findCompiler();
if (!compiler) {
    console.log("SKIP: no host C compiler to build ddraw_protocol.h against");
    process.exit(0);
}

const header = path.resolve(__dirname, "../ddrawproxy/ddraw_protocol.h");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-expand-"));
const source = path.join(directory, "expand.c");
const binary = path.join(directory, "expand");

fs.writeFileSync(source, `#include "${header}"
#include <stdio.h>
int main(void) {
    for (unsigned bits = 1; bits <= 8; ++bits) {
        unsigned max = (1u << bits) - 1u;
        for (unsigned v = 0; v <= max; ++v)
            printf("%u %u %u\\n", bits, v, ddraw_expand_channel(v, bits));
    }
    return 0;
}
`);

const build = spawnSync(compiler,
    ["-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", binary, source],
    { encoding: "utf8" });
assert.equal(build.status, 0,
    "ddraw_protocol.h did not compile:\n" + (build.stderr || ""));

const run = spawnSync(binary, [], { encoding: "utf8" });
assert.equal(run.status, 0, "the expansion probe did not run");

// Exercise the real host helper, rather than copying an old source formula.
const { expandUNorm8: hostExpand } = require(require("./host_paths.js").hostPath("d3d9-webgpu/d9wg_formats.js"));

let checked = 0;
const mismatches = [];
for (const line of run.stdout.trim().split("\n")) {
    const [bits, value, guest] = line.trim().split(/\s+/).map(Number);
    const max = (1 << bits) - 1;
    const host = bits >= 8 ? value : hostExpand(value, bits);
    ++checked;
    if (guest !== host)
        mismatches.push({ bits, value, guest, host });
}

if (mismatches.length) {
    for (const mismatch of mismatches.slice(0, 16))
        console.error(`FAIL ${mismatch.bits}-bit value ${mismatch.value}: ` +
            `guest ${mismatch.guest}, host ${mismatch.host}`);
    console.error(mismatches.length + " of " + checked +
        " channel values disagree between ddraw_protocol.h and " +
        "d3d9_executor.js");
    process.exit(1);
}

// The endpoints matter on their own: a key of pure black or pure white is what
// most sprite sheets actually use.
for (const bits of [1, 4, 5, 6, 8]) {
    const max = (1 << bits) - 1;
    assert.equal(hostExpand(0, bits), 0, bits + "-bit zero must stay zero");
    assert.equal(hostExpand(max, bits), 255, bits + "-bit max must reach 255");
}

assert.ok(checked >= 500, "the sweep should cover every value of every width");
console.log(checked + " channel expansions agree between the guest DLL and " +
    "the executor");
