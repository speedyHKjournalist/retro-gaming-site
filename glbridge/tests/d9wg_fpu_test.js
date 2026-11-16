"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-fpu-"));
try {
    const input = path.join(directory, "fpu.c"), binary = path.join(directory, "fpu");
    fs.writeFileSync(input, `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "runtime/d9wg_fpu.h"
int main(void) {
    for(uint32_t word=0;word<65536u;word++) {
        uint16_t now=d9wg_fpu_control((uint16_t)word,0);
        assert(d9wg_fpu_control((uint16_t)word,1)==word);
        assert((now&0xf00u)==0&&(now&0x3fu)==0x3fu);
        assert((now&~0xf3fu)==(word&~0xf3fu));
    }
    assert(d9wg_fpu_control(0xf60,0)==0x7f&&d9wg_fpu_control(0x37f,0)==0x7f);
    puts("PASS pure x87 policy: all 65536 control words, preserve/default and unrelated bits; instruction behavior is tested in XP");return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", root,
        input, path.join(root, "runtime/d9wg_fpu.c"), "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));
    process.stdout.write(run.stdout);

} finally { fs.rmSync(directory, { recursive: true, force: true }); }
