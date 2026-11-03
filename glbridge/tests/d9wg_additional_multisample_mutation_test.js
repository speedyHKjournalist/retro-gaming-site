"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-chain-msaa-"));
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), m = /static HRESULT WINAPI device_create_additional_swap_chain\([^;]*?\)\s*\{/.exec(source); assert(m);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    const part = source.slice(m.index, end); let killed = 0;
    for (const [label, old, replacement] of [
        ["single-sample storage", "D3DUSAGE_RENDERTARGET, params->MultiSampleType,", "D3DUSAGE_RENDERTARGET, D3DMULTISAMPLE_NONE,"],
        ["invalid storage quality", "params->MultiSampleQuality, &back_buffer);", "1, &back_buffer);"],
        ["unsupported sample mode emitted", "if (!supported_multisample(format, params->MultiSampleType))", "if (0 && !supported_multisample(format, params->MultiSampleType))"],
        ["supported four samples refused", "if (!supported_multisample(format, params->MultiSampleType))", "if (params->MultiSampleType || !supported_multisample(format, params->MultiSampleType))"],
        ["cached sample mode lost", "        width, height, format, window);", "        width, height, format, window);\n    chain->present.MultiSampleType = D3DMULTISAMPLE_NONE;"],
    ]) {
        assert(part.includes(old), label); const file = dir + "/frontend.c"; fs.writeFileSync(file, source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_additional_parameters_entry_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_ADDITIONAL_PARAMETERS_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Additional parameters|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 additional MSAA mutations: " + killed + "/5; actual storage sample/quality forwarding, unsupported-mode refusal, 4x acceptance and cached parameters");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
