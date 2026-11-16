"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-display-mut-"));
function mutate(source, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) { if (source[end] === "{") depth++; if (source[end] === "}") depth--; }
    const part = source.slice(m.index, end); assert(part.includes(old), name + ": " + old);
    return source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end);
}
function run(label, env) {
    const r = spawnSync(process.execPath, [__dirname + "/d9wg_display_mode_test.js"], {encoding: "utf8", env: {...process.env, ...env}, timeout: 30000});
    assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived");
    assert(!/error:|undefined reference|Undefined symbols/.test(r.stdout + r.stderr), label + " compile failure: " + r.stderr);
    assert(/Display mode API|Assertion|assertion|signal=/.test(r.stdout + r.stderr), label + " no runtime failure");
}
try {
    let killed = 0;
    const header = readComparisonSource(root + "/runtime/d9wg_display_mode.h", "utf8");
    for (const [label, old, replacement] of [
        ["live mode ignored", "if (EnumDisplaySettingsA(", "if (0 && EnumDisplaySettingsA("],
        ["invalid width accepted", "&& current.dmPelsWidth &&", "&& 1 &&"],
        ["invalid height accepted", "&& current.dmPelsHeight)", "&& 1)"],
        ["16-bit format reported as 32-bit", "current.dmBitsPerPel == 16 ? 23u : 22u", "22u"],
        ["refresh always sixty", "current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency : 60u", "60u"],
        ["unknown refresh convention lost", "current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency : 60u", "current.dmDisplayFrequency"],
        ["fallback dimensions changed", "{ 1024, 768, 22, 60 }", "{ 640, 480, 22, 60 }"],
        ["fallback format changed", "{ 1024, 768, 22, 60 }", "{ 1024, 768, 23, 60 }"],
        ["Win32 structure size omitted", "current.dmSize = sizeof(current);", "current.dmSize = 0;"],
        ["enumeration index queried instead of active mode", "EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current)", "EnumDisplaySettingsA(NULL, 0, &current)"],
    ]) {
        assert(header.includes(old), label); const file = dir + "/display.h"; fs.writeFileSync(file, header.replace(old, replacement)); run(label, {D9WG_DISPLAY_HEADER: file}); killed++;
    }
    for (const api of [8, 9]) {
        const source = readComparisonSource(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        const changes = [
            ["factory dimensions fixed", "d3d_get_adapter_display_mode", "fill_display_mode(mode, current.width, current.height,", "fill_display_mode(mode, 1024, 768,"],
            ["factory refresh lost", "d3d_get_adapter_display_mode", "mode->RefreshRate = current.refresh_rate;", ""],
            ["invalid adapter accepted", "d3d_get_adapter_display_mode", "if (adapter || !mode)", "if ((0 && adapter) || !mode)"],
            ["device adapter ordinal ignored", "device_get_display_mode", "device->creation.AdapterOrdinal, mode)", "0, mode)"],
            ["device returns cached buffer mode", "device_get_display_mode", api === 8 ? "return d3d_get_adapter_display_mode(&device->parent->iface,\n        device->creation.AdapterOrdinal, mode);" : "HRESULT result = d3d_get_adapter_display_mode(&device->parent->iface,\n        device->creation.AdapterOrdinal, mode);", api === 8 ? "*mode = device->display_mode; return D3D_OK;" : "HRESULT result = D3D_OK; *mode = device->display_mode;"],
        ];
        if(api===8){changes.splice(0,3,
            ["wrapper dimensions fixed","d3d_get_adapter_display_mode","return d9wg_d3d9_core_adapter_display_mode(d3d_from_iface(iface)->backend, adapter, mode);","HRESULT r=d9wg_d3d9_core_adapter_display_mode(d3d_from_iface(iface)->backend,adapter,mode);if(SUCCEEDED(r))mode->Width=1024;return r;"],
            ["wrapper refresh lost","d3d_get_adapter_display_mode","return d9wg_d3d9_core_adapter_display_mode(d3d_from_iface(iface)->backend, adapter, mode);","HRESULT r=d9wg_d3d9_core_adapter_display_mode(d3d_from_iface(iface)->backend,adapter,mode);if(SUCCEEDED(r))mode->RefreshRate=60;return r;"],
            ["wrapper adapter ignored","d3d_get_adapter_display_mode","->backend, adapter, mode","->backend, adapter & 0u, mode"]
        );}
        if (api === 9) changes.push(
            ["chain adapter ordinal ignored", "swap_chain_get_display_mode", "swap_chain->device->creation.AdapterOrdinal, mode)", "0, mode)"],
            ["chain returns cached buffer mode", "swap_chain_get_display_mode", "HRESULT result = d3d_get_adapter_display_mode(&swap_chain->device->parent->iface,\n        swap_chain->device->creation.AdapterOrdinal, mode);", "HRESULT result = D3D_OK; *mode = swap_chain->device->display_mode;"],
        );
        for (const [label, fn, old, replacement] of changes) {
            const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement)); run("API " + api + " " + label, {["D" + api + "WG_DISPLAY_FRONTEND"]: file}); killed++;
        }
    }
    console.log("PASS display mode mutations: " + killed + "/22; live Win32 mode, dimensions/format/refresh/fallback, guards and all five public factory/device/chain routes");
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
