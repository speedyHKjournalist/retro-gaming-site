"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-standalone-container-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d8proxy/d3d8_proxy.c", "utf8"); let killed = 0;
    for (const [label, name, old, replacement] of [
        ["hidden standalone texture exposed", "surface_get_container", "&& !surface->device_container", "&& 1"],
        ["ordinary texture classified as standalone", "surface_get_container", "&& !surface->device_container", "&& surface->device_container"],
        ["wrong container identity", "surface_get_container", "*container = &surface->device->iface;", "*container = &surface->iface;"],
        ["missing device query reference", "surface_get_container", "IDirect3DDevice8_AddRef(&surface->device->iface);", "(void)surface->device;"],
        ["stale failed output", "surface_get_container", "*container = NULL;", "(void)container;"],
        ["unsupported IID accepted", "surface_get_container", "&& !guid_equal(iid, &IID_IDirect3DDevice8)", "&& 0"],
        ["render target marker missing", "device_create_render_target", "surface_from_iface(*surface_out)->device_container = TRUE;", "surface_from_iface(*surface_out)->device_container = 0;"],
        ["depth target marker missing", "device_create_depth_surface", "surface_from_iface(*surface_out)->device_container = TRUE;", "surface_from_iface(*surface_out)->device_container = 0;"],
        ["failed render target dereferenced", "device_create_render_target", "if (SUCCEEDED(hr))", "if (1)"],
        ["failed depth target dereferenced", "device_create_depth_surface", "if (SUCCEEDED(hr))", "if (1)"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, name, old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d8wg_standalone_container_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D8WG_STANDALONE_CONTAINER_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/D8 standalone container|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D8 standalone-container mutations: " + killed + "/10; real constructor classification and failed creation safety, container identity/reference, output/IID policy and ordinary texture isolation");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
