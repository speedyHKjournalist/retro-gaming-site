"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-clip-"));
try {
    const reference = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures/d9wg_d3d7_native_process_reference.json")));
    assert.equal(reference.results.length, 252);
    const number = n => Number.isInteger(n) ? n + ".0f" : Number(n).toPrecision(9) + "f";
    const vector = a => "{" + a.map(number).join(",") + "}";
    const native = reference.results.map((r, i) => `{
        const float point[4]=${vector([...reference.inputs[r.vertex],reference.projectionW[r.projection]])};
        const float expected[4]=${vector(r.position)};D9WGClipVertex result;
        d9wg_clip_vertex(point,&viewport,guard,${r.operationClip}u,&result);
        for(unsigned c=0;c<4;c++)if(!clip_near(result.stored[c],expected[c])){
            fprintf(stderr,"clip mismatch native case ${i} component %u: %.9g != %.9g\\n",c,result.stored[c],expected[c]);return 1;}
    }`).join("\n");
    const input = path.join(directory, "clip.c"), binary = path.join(directory, "clip");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_clip_cases.h"
int main(void){int error=clip_cases(100000);if(error){fprintf(stderr,"clip mismatch header line %d\\n",error);return 1;}
const D9WGProcessViewport viewport={7,11,100,80,.25f,.75f};const float guard[4]=${vector(reference.guardBand)};
${native}
puts("PASS C clipping: 252 frozen Microsoft DDraw7 results, six outcodes, guard bands and 100000 analytic cases");return 0;}
`);
    const source = process.env.D9WG_CLIP_SOURCE || path.join(root, "runtime/d9wg_clip.c");
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root, "runtime"),
        input, source, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const result = spawnSync(binary, [], { encoding: "utf8" });assert.equal(result.status, 0, result.stderr.slice(0, 1200) || String(result.error));
    process.stdout.write(result.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
