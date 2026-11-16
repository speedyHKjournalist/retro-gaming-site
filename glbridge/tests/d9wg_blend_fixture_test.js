"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs");
const { testPath } = require("./host_paths.js"), { buildFixture } = require("./d9wg_lighting_harness.js");
const caseFile = "fixtures/d3d_software_blend_cases.json", target = testPath("fixtures/d3d_software_blend_cpu.json");
const current = buildFixture({ caseFile }), input = JSON.parse(fs.readFileSync(testPath(caseFile), "utf8"));
if (process.argv.includes("--write-fixture")) fs.writeFileSync(target, JSON.stringify(current, null, 2) + "\n");
const saved = JSON.parse(fs.readFileSync(target, "utf8"));assert.equal(saved.caseSha256, current.caseSha256);
assert.equal(saved.cases.length, current.cases.length);
for (let i = 0; i < current.cases.length; i++) {
    assert.equal(current.cases[i].name, input.cases[i].name);
    for (let v = 0; v < 3; v++) for (const key of ["position", "diffuse", "specular"]) for (let c = 0; c < 4; c++) {
        const actual = current.cases[i].processed[v][key][c];
        assert.ok(Math.abs(saved.cases[i].processed[v][key][c] - actual) <= 2e-6, current.cases[i].name + " stale blend fixture");
        if (key === "diffuse" && input.cases[i].expectedPackedDiffuse !== undefined) {
            const shift = [16,8,0,24][c], expected = (input.cases[i].expectedPackedDiffuse >>> shift & 255) / 255;
            assert.ok(Math.abs(actual - expected) <= 2e-6, current.cases[i].name + " analytic weighted-normal color");
        }
    }
}
console.log("PASS C blend differential fixture: " + current.cases.length + " scenes and independent normal-color equations");
