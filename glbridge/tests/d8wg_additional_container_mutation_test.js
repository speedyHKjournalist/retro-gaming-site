"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-additional-container-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d8proxy/d3d8_proxy.c", "utf8"); let killed = 0;
    for (const [label, old, replacement] of [
        ["hidden texture exposed", "surface->texture && !surface->additional_back_buffer\n            && !surface->device_container", "surface->texture"],
        ["ordinary texture container lost", "surface->texture && !surface->additional_back_buffer\n            && !surface->device_container", "surface->texture && 0"],
        ["device reference missing", "IDirect3DDevice8_AddRef(&surface->device->iface);", "(void)surface->device;"],
        ["device identity lost", "*container = &surface->device->iface;", "*container = &surface->texture->iface;"],
        ["failed output preserved", "*container = NULL;", "(void)container;"],
        ["device accepts texture IIDs", "&& !guid_equal(iid, &IID_IDirect3DDevice8)", "&& 0"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, "surface_get_container", old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d8wg_additional_container_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_CONTAINER_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compile failure: " + output); assert(output.includes("D8 additional container")); killed++;
    }
    const file = dir + "/ctor.c"; fs.writeFileSync(file, mutate(source, "device_create_swapchain", "surface->additional_back_buffer = TRUE;", "surface->additional_back_buffer = 0;"));
    const r = spawnSync(process.execPath, [__dirname + "/d9wg_additional_parameters_entry_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_ADDITIONAL_PARAMETERS_SOURCE: file}}), output = r.stdout + r.stderr;
    assert(!r.error); assert.notEqual(r.status, 0); assert(!/error:|undefined reference|Undefined symbols/.test(output), output); assert(output.includes("Additional parameters API 8")); killed++;
    console.log("PASS D3D8 additional-container mutations: " + killed + "/7; constructor classification, container identity, exact references, output clearing and texture isolation");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
