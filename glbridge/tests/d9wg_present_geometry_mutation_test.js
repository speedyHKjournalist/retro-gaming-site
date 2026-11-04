"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-geometry-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const part = s.slice(m.index, end); assert(part.includes(old), name + ": " + old);
    return s.slice(0, m.index) + part.replace(old, replacement) + s.slice(end);
}
function run(label, test, env) {
    const r = spawnSync(process.execPath, [__dirname + "/" + test], {encoding: "utf8", timeout: 30000, env: {...process.env, ...env}}), out = r.stdout + r.stderr;
    assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(out), label + " compile failure: " + out); assert(/Present geometry|Reset status|Assertion|signal=/.test(out));
}
try {
    let killed = 0; const policy = fs.readFileSync(root + "/runtime/d9wg_present_parameters.h", "utf8");
    for (const [label, old, replacement] of [
        ["always fallback dimensions", "requested ? requested : client ? client : fallback", "fallback + 0u * requested + 0u * client"],
        ["client overrides explicit dimensions", "requested ? requested : client ? client : fallback", "client ? client : requested ? requested : fallback"],
        ["zero count retained", "requested->BackBufferCount : 1", "requested->BackBufferCount : 0"],
        ["cached dimensions keep request", "effective->BackBufferWidth = width;", "effective->BackBufferWidth = requested->BackBufferWidth + 0u * width;"],
        ["cached window stays NULL", "effective->hDeviceWindow = window;", "effective->hDeviceWindow = requested->hDeviceWindow; (void)window;"],
        ["D3D8 dimensions overwritten", "if (api == 9)", "if (api == 8 || api == 9)"],
        ["D3D9 dimensions not returned", "if (api == 9)", "if (api == 9 && !effective->BackBufferWidth)"],
        ["caller NULL window overwritten", "requested->BackBufferCount = effective->BackBufferCount;", "requested->hDeviceWindow = effective->hDeviceWindow; requested->BackBufferCount = effective->BackBufferCount;"],
    ]) {
        assert(policy.includes(old), label); const file = dir + "/policy.h"; fs.writeFileSync(file, policy.replace(old, replacement)); run(label, "d9wg_present_geometry_entry_test.js", {D9WG_GEOMETRY_POLICY: file}); killed++;
    }
    for (const api of [8, 9]) {
        const source = fs.readFileSync(root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        for (const [label, fn, old, replacement, test] of [
            ["guest mode keeps 640", "d3d_create_device", "        width, height,", "        640, 480,", "d9wg_present_geometry_entry_test.js"],
            ["host width keeps fallback", "d3d_create_device", "    command.width = width;", "    command.width = parameters->BackBufferWidth ? parameters->BackBufferWidth : 640;", "d9wg_present_geometry_entry_test.js"],
            ["height uses client width", "d3d_create_device", "(uint32_t)(client.bottom - client.top), 480)", "(uint32_t)(client.right - client.left), 480)", "d9wg_present_geometry_entry_test.js"],
            ["caller output published before mode succeeds", "d3d_create_device", "    if (!reset_window_mode(device, window))", "    d9wg_present_parameters_return(" + api + ", parameters, &device->present);\n    if (!reset_window_mode(device, window))", "d9wg_present_geometry_entry_test.js"],
            ["Reset keeps raw cached parameters", "device_reset", "    d9wg_present_parameters_effective(&device->present, parameters,\n        reset.width, reset.height, reset.backbuffer_format, window);", "    device->present = *parameters;", "d9wg_reset_status_test.js"],
            ["Reset omits caller normalization", "device_reset", "    d9wg_present_parameters_return(" + api + ", parameters, &device->present);", "", "d9wg_reset_status_test.js"],
        ]) {
            const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, api===8?old.replace("d9wg_present_parameters_effective(","d9wg_d3d9_core_parameters_effective8("):old, replacement));
            run("API " + api + " " + label, test, {["D" + api + (fn === "device_reset" ? "WG_RESET_STATUS_FRONTEND" : "WG_GEOMETRY_SOURCE")]: file}); killed++;
        }
    }
    console.log("PASS present geometry mutations: " + killed + "/20; shared extent/count/window/version rules, guest/host agreement, caller publication and both Reset entries");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
