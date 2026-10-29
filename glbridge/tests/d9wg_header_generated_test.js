#!/usr/bin/env node
// d3d9proxy/d3d9_protocol.h is generated from v86's
// src/browser/glbridge/d3d9-webgpu/d9wg_protocol.js by
// tools/gen_d9wg_header.mjs: a hand edit here, or a protocol change in v86
// that was not regenerated here, fails this test. To regenerate:
//
//   node <v86>/tools/gen_d9wg_header.mjs glbridge/d3d9proxy/d3d9_protocol.h
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

const generator = require("./host_paths.js").toolPath("gen_d9wg_header.mjs");
const result = spawnSync(process.execPath, [generator], { encoding: "utf8" });
assert.equal(result.status, 0, generator + ": " + (result.stderr || result.error));
const header = fs.readFileSync(path.resolve(__dirname, "../d3d9proxy/d3d9_protocol.h"), "utf8");
assert.ok(header === result.stdout,
    "d3d9proxy/d3d9_protocol.h is not what " + generator + " writes; regenerate it from v86");
console.log("d9wg_header_generated_test: ok (" + header.split("\n").length + " lines, as generated)");
