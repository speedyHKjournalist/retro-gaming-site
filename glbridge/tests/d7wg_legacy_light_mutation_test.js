"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/d3d_legacy_proxy.inc"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "legacy-light-mutants-"));
const cases = [
    ["DX3 flags left inactive", "if (data->dwSize < sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE;", "if (FALSE && data->dwSize < sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE;"],
    ["full D3DLIGHT2 zero flags activated", "if (data->dwSize < sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE;", "if (data->dwSize <= sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE;"],
    ["DX3 specular implicitly suppressed", "if (data->dwSize < sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE;", "if (data->dwSize < sizeof(D3DLIGHT2))\n        light->light.dwFlags = D3DLIGHT_ACTIVE | D3DLIGHT_NO_SPECULAR;"],
    ["full NO_SPECULAR flag discarded", "light->light.dwSize = sizeof(light->light);\n    if (data->dwSize", "light->light.dwSize = sizeof(light->light);\n    light->light.dwFlags &= D3DLIGHT_ACTIVE;\n    if (data->dwSize"],
    ["explicit inactive light enabled in viewport", "index, !!(light->light.dwFlags & D3DLIGHT_ACTIVE));", "index, TRUE || !!(light->light.dwFlags & D3DLIGHT_ACTIVE));"],
    ["failed live update reported success", "return d3d_legacy_apply_viewport(light->viewport);", "{d3d_legacy_apply_viewport(light->viewport); return D3D_OK;}"],
];
try {for (const [label, before, after] of cases) {assert.equal(source.split(before).length, 2, label); const file = dir + "/legacy.inc"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "d7wg_legacy_light_test.js")], {env: {...process.env, D7WG_LEGACY_LIGHT_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /legacy light line/, "strict compilation then runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS legacy light mutations: 6/6, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
