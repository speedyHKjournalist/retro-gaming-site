"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs");
const { testPath } = require("./host_paths.js"), { buildFixture } = require("./d9wg_lighting_harness.js");
const current = buildFixture(), target = testPath("fixtures/d3d_software_lighting_cpu.json");
if (process.argv.includes("--write-fixture")) fs.writeFileSync(target, JSON.stringify(current, null, 2) + "\n");
const saved = JSON.parse(fs.readFileSync(target, "utf8"));
assert.equal(saved.caseSha256, current.caseSha256);assert.equal(saved.cases.length, current.cases.length);
for (let i = 0; i < current.cases.length; i++) {
    assert.equal(saved.cases[i].name, current.cases[i].name);
    for (let vertex = 0; vertex < 3; vertex++) for (const key of ["position", "diffuse", "specular"])
        for (let component = 0; component < current.cases[i].vertices[vertex][key].length; component++)
            assert.ok(Math.abs(saved.cases[i].vertices[vertex][key][component] - current.cases[i].vertices[vertex][key][component]) <= 2e-6,
                current.cases[i].name + " stale CPU differential fixture " + key);
}
console.log("PASS shared C lighting: " + current.cases.length + " scenes, independent equation probes and CPU differential fixture");
