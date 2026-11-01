"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs");
const { testPath } = require("./host_paths.js"), { buildFixture } = require("./d9wg_lighting_harness.js");
const caseFile = "fixtures/d3d_software_texcoord_cases.json", target = testPath("fixtures/d3d_software_texcoord_cpu.json");
const current = buildFixture({ caseFile }), input = JSON.parse(fs.readFileSync(testPath(caseFile), "utf8"));
if (process.argv.includes("--write-fixture")) fs.writeFileSync(target, JSON.stringify(current, null, 2) + "\n");
const saved = JSON.parse(fs.readFileSync(target, "utf8"));assert.equal(saved.caseSha256, current.caseSha256);
assert.equal(saved.cases.length, current.cases.length);
for (let i = 0; i < current.cases.length; i++) {
    assert.equal(current.cases[i].name, input.cases[i].name);
    for (let v = 0; v < 3; v++) for (let set = 0; set < 8; set++) for (let component = 0; component < 4; component++) {
        const actual = current.cases[i].textureCoordinates[v][set][component];
        assert.ok(Math.abs(saved.cases[i].textureCoordinates[v][set][component] - actual) <= 2e-6,
            current.cases[i].name + " stale texture fixture");
        if (set === 0 && input.cases[i].expectedTexture)
            assert.ok(Math.abs(actual - input.cases[i].expectedTexture[component]) <= 2e-6,
                current.cases[i].name + " analytic texture component " + component + ": " + actual);
    }
}
console.log("PASS C texture differential fixture: " + current.cases.length + " scenes and independent coordinate equations");
