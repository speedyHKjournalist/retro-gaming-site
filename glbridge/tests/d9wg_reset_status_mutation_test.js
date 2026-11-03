"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-reset-status-mut-"));
function run(name, env) {
    const r = spawnSync(process.execPath, [__dirname + "/d9wg_reset_status_test.js"], {env: {...process.env, ...env}, encoding: "utf8", timeout: 30000});
    assert(!r.error, name + ": " + r.error);
    assert.notEqual(r.status, 0, name + " survived");
    assert(!/error:|undefined reference|Undefined symbols/.test(r.stdout + r.stderr), name + " failed to compile: " + r.stderr);
    assert(/Reset status|Assertion|assertion|signal=/.test(r.stdout + r.stderr), name + " missing runtime failure");
}
function mutateFunction(source, name, old, replacement) {
    const match = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source);
    assert(match, name);
    let end = source.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; end++) { if (source[end] === "{") depth++; if (source[end] === "}") depth--; }
    const part = source.slice(match.index, end);
    assert(part.includes(old), name + ": " + old);
    return source.slice(0, match.index) + part.replace(old, replacement) + source.slice(end);
}
try {
    let killed = 0;
    const policy = fs.readFileSync(root + "/runtime/d9wg_reset.h", "utf8");
    for (const [name, old, replacement] of [
        ["Reset attempt leaves ready", "state->required = 1;", "state->required = 0;"],
        ["successful Reset stays lost", "state->required = 0;", "state->required = 1;"],
        ["cooperative query always ready", "state->required ? (int32_t)UINT32_C(0x88760869) : 0", "state->required ? 0 : 0"],
        ["cooperative query uses DEVICELOST", "UINT32_C(0x88760869)", "UINT32_C(0x88760868)"],
        ["Present ignores loss", "state->required ? (int32_t)UINT32_C(0x88760868) : 0", "state->required ? 0 : 0"],
        ["Present returns DEVICENOTRESET", "UINT32_C(0x88760868)", "UINT32_C(0x88760869)"],
    ]) {
        assert(policy.includes(old), name);
        const file = dir + "/policy.h";
        fs.writeFileSync(file, policy.replace(old, replacement));
        run(name, {D9WG_RESET_STATUS_POLICY: file}); killed++;
    }
    for (const api of [8, 9]) {
        const source = fs.readFileSync(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        for (const [name, fn, old, replacement] of [
            ["Scene not ended by accepted Reset", "device_reset", "    device->in_scene = FALSE;", ""],
            ["Scene ended before Reset acceptance", "device_reset", "d9wg_reset_begin(&device->reset_state);", "device->in_scene = FALSE;\n    d9wg_reset_begin(&device->reset_state);"],
            ["Reset never marks failure", "device_reset", "d9wg_reset_begin(&device->reset_state);", ""],
            ["success never restores readiness", "device_reset", "d9wg_reset_complete(&device->reset_state);", ""],
            ["recovery published before state replay", "device_reset", "if (!emit_device_state_after_reset(device))", "d9wg_reset_complete(&device->reset_state);\n    if (!emit_device_state_after_reset(device))"],
            ["cooperative query ignores device", "device_test_cooperative_level", "return d9wg_reset_cooperative_result(&device_from_iface(iface)->reset_state);", "(void)iface; return D3D_OK;"],
            ["device Present ignores loss", "device_present", "if (status != D3D_OK)", "if (0 && status != D3D_OK)"],
            ["chain Present ignores loss", api === 8 ? "swapchain_present" : "swap_chain_present", api === 8 ? "if (status != D3D_OK)" : "if (result != D3D_OK)", api === 8 ? "if (0 && status != D3D_OK)" : "if (0 && result != D3D_OK)"],
        ]) {
            const file = dir + "/frontend.c";
            fs.writeFileSync(file, mutateFunction(source, fn, old, replacement));
            run("API " + api + " " + name, {["D" + api + "WG_RESET_STATUS_FRONTEND"]: file}); killed++;
        }
        if (api === 8) {
            const file = dir + "/frontend.c";
            fs.writeFileSync(file, mutateFunction(source, "device_reset", "if (device_has_reset_blockers(device))\n        return D8WG_TRACE_ERROR(D3DERR_DEVICELOST);", "if (device_has_reset_blockers(device))\n        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);"));
            run("D3D8 resource refusal HRESULT", {D8WG_RESET_STATUS_FRONTEND: file}); killed++;
        }
    }
    console.log("PASS Reset status mutations: " + killed + "/23; both public Reset/cooperative/Present paths, HRESULT distinction, recovery publication and shared status policy");
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
