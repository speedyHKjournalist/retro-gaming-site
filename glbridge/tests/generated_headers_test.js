#!/usr/bin/env node
// The protocol headers are generated from v86's one description of each
// protocol: d3d9proxy/d3d9_protocol.h from
// src/browser/glbridge/d3d9-webgpu/d9wg_protocol.js (tools/gen_d9wg_header.mjs),
// openglproxy/gl_protocol.h and gl_opcode_names.h from
// src/browser/glbridge/gl-webgpu/gl_protocol.js (tools/gen_gl_protocol_header.mjs).
// A hand edit here, or a protocol change in v86 that was not regenerated here,
// fails this test. To regenerate:
//
//   node <v86>/tools/gen_d9wg_header.mjs glbridge/d3d9proxy/d3d9_protocol.h
//   node <v86>/tools/gen_gl_protocol_header.mjs glbridge/openglproxy
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { toolPath } = require("./host_paths.js");

function run(tool, args) {
    const result = spawnSync(process.execPath, [toolPath(tool), ...args], { encoding: "utf8" });
    assert.equal(result.status, 0, tool + ": " + (result.stderr || result.error));
    return result.stdout;
}

function same(file, generated, tool) {
    const text = fs.readFileSync(path.resolve(__dirname, "..", file), "utf8");
    assert.ok(text === generated, file + " is not what " + tool + " writes; regenerate it from v86");
}

same("d3d9proxy/d3d9_protocol.h", run("gen_d9wg_header.mjs", []), "gen_d9wg_header.mjs");
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-protocol-"));
try {
    run("gen_gl_protocol_header.mjs", [dir]);
    for (const file of ["gl_protocol.h", "gl_opcode_names.h"])
        same("openglproxy/" + file, fs.readFileSync(path.join(dir, file), "utf8"), "gen_gl_protocol_header.mjs");
} finally {
    fs.rmSync(dir, { recursive: true, force: true });
}
console.log("generated_headers_test: ok (d3d9_protocol.h, gl_protocol.h, gl_opcode_names.h as generated)");
