"use strict";
// A newly advertised texture format/op must have an actual READBACK pixel
// fixture in the host suite. Rendering that fixture is the CI GPU job's job.
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { hostPath, testPath } = require("./host_paths.js");
require(hostPath("d3d9-webgpu/d3d9_shader_pipeline.js"));
require(hostPath("d3d9-webgpu/d9wg_protocol.js"));
require(hostPath("d3d9-webgpu/d9wg_formats.js"));
require(testPath("d3d9_conformance.js"));require(testPath("d3d9_feature_cases.js"));
const suite = globalThis.D3D9Conformance, source = fs.readFileSync(path.join(__dirname, "../d3d9proxy/d3d9_proxy.c"), "utf8");
const predicate = source.match(/static BOOL supported_texture_format\(D3DFORMAT format\)\s*\{([\s\S]*?)\n\}/)[1];
const formats = new Set(suite.FORMAT_CASES.map(c => c[0]));
for (const [, name] of predicate.matchAll(/(?:D3DFMT_|D9FMT_)(\w+)/g)) assert(formats.has(name), name + " is advertised without a pixel fixture");
const caps = source.match(/caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE([\s\S]*?);/)[1].replace(/\/\*[\s\S]*?\*\//g, "");
const names = ["DISABLE", "SELECTARG1", "SELECTARG2", "MODULATE", "MODULATE2X", "MODULATE4X", "ADD", "ADDSIGNED", "ADDSIGNED2X", "SUBTRACT", "ADDSMOOTH",
    "BLENDDIFFUSEALPHA", "BLENDTEXTUREALPHA", "BLENDFACTORALPHA", "BLENDTEXTUREALPHAPM", "BLENDCURRENTALPHA", "PREMODULATE", "MODULATEALPHA_ADDCOLOR",
    "MODULATECOLOR_ADDALPHA", "MODULATEINVALPHA_ADDCOLOR", "MODULATEINVCOLOR_ADDALPHA", "BUMPENVMAP", "BUMPENVMAPLUMINANCE", "DOTPRODUCT3", "MULTIPLYADD", "LERP"];
const operations = new Set(suite.FEATURE_GROUPS["texture-ops"].map(c => names[c.textureOp - 1]));
for (const name of ["DISABLE", ...[...caps.matchAll(/D3DTEXOPCAPS_(\w+)/g)].map(m => m[1])]) assert(operations.has(name), name + " is advertised without a pixel fixture");
const unique = new Set();
for (const [i, c] of suite.CASES.entries()) {
    assert(!unique.has(c.name), "duplicate case " + c.name);unique.add(c.name);
    assert(c.probes.length && c.probes.every(p => p.expect || p.someRedBetween), "missing oracle " + c.name);
    const { bytes } = suite.caseBatch(i, c, {hostFeatures: 15}), view = new DataView(bytes.buffer);
    assert.equal(view.getUint32(20, true) + 32, bytes.length);
    for (let at = 32; at < bytes.length;) {
        const op = view.getUint16(at, true), size = view.getUint32(at + 4, true);
        assert(globalThis.D9WGProtocol.commandStruct(op), c.name + " unknown opcode");
        assert(size >= 16 && !(size & 7) && at + size <= bytes.length, c.name + " invalid packet");at += size;
    }
}
console.log("D3D advertised texture coverage: " + formats.size + " formats, " + operations.size + " ops, " + unique.size + " pixel fixtures");
