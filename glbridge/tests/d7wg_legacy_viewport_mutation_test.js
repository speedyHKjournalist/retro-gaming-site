"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/d3d_legacy_proxy.inc"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-viewport-mutants-"));
const cases = [
    ["reactivation compounds projection", "&device->legacy_projection, &clip, &projection", "&device->transforms[D3DTRANSFORMSTATE_PROJECTION], &clip, &projection"],
    ["logical query exposes clip matrix", "*matrix = device->legacy_projection;", "*matrix = device->transforms[D3DTRANSFORMSTATE_PROJECTION];"],
    ["legacy Multiply order reversed", "d3d7_multiply_matrix(matrix, &device->legacy_projection, &projection);", "d3d7_multiply_matrix(&device->legacy_projection, matrix, &projection);"],
    ["viewport depth restored instead of clip depth", "viewport7->dvMaxZ = 1.0f;", "viewport7->dvMaxZ = 2.0f;", 2],
    ["V1 scale ignored", "matrix->_11 = clip.scale[0]; matrix->_22 = clip.scale[1];", "matrix->_11 = 1.0f; matrix->_22 = clip.scale[1];"],
    ["V2 clip offset ignored", "matrix->_42 = clip.offset[1]; matrix->_43 = clip.offset[2];", "matrix->_42 = 0.0f; matrix->_43 = clip.offset[2];"],
    ["prelit diffuse white", "destination->color = prelit->color;", "destination->color = 0xffffffffu; (void)prelit;"],
    ["prelit specular lost", "destination->specular = prelit->specular;", "destination->specular = 0;"],
    ["NOCOLOR loses specular", "destination->tv = source->tv;", "destination->tv = source->tv; destination->specular = 0;"],
    ["unlit transform prepares lighting", "if (operation == D3DPROCESSVERTICES_TRANSFORMLIGHT &&\n            !(process->dwFlags & D3DPROCESSVERTICES_NOCOLOR) &&", "if ((operation == D3DPROCESSVERTICES_TRANSFORMLIGHT || operation == D3DPROCESSVERTICES_TRANSFORM) &&\n            !(process->dwFlags & D3DPROCESSVERTICES_NOCOLOR) &&"],
    ["payload truncated to one DWORD", "CopyMemory(output + 16u, input + 16u, 16u);", "CopyMemory(output + 16u, input + 16u, 4u);"],
    ["homogeneous output uses viewport clip space", "data->lpHOut[index].hx = raw[0];", "data->lpHOut[index].hx = clip[0];"],
    ["offscreen is BOOL instead of clip intersection", "*offscreen = clipped ? clip_intersection : 0;", "*offscreen = clipped ? !!clip_intersection : 0;"],
    ["unclipped overwrites HOut", "if (clipped && data->lpHOut)", "if (data->lpHOut)"],
    ["accepted failed logical prefix", "if (SUCCEEDED(hr) && !device->recording_state_block)\n        device->legacy_projection = *matrix;", "if (!device->recording_state_block)\n        device->legacy_projection = *matrix;"],
    ["recording changes logical projection", "if (SUCCEEDED(hr) && !device->recording_state_block)\n        device->legacy_projection = *matrix;", "if (SUCCEEDED(hr))\n        device->legacy_projection = *matrix;"],
];
try {for (const [label, before, after, count = 1] of cases) {assert.equal(source.split(before).length - 1, count, label); const file = dir + "/legacy.inc"; fs.writeFileSync(file, source.replaceAll(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "d7wg_legacy_viewport_test.js")], {env: {...process.env, D7WG_LEGACY_VIEWPORT_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /legacy viewport line/, "strict compilation then runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS legacy viewport/execute mutations: 16/16, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
