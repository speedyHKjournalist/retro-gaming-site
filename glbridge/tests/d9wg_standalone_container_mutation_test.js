"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-standalone-container-mut-"));
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), m = /static HRESULT WINAPI surface_get_container\([^;]*?\)\s*\{/.exec(source); assert(m);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    const part = source.slice(m.index, end); let killed = 0;
    for (const [label, old, replacement] of [
        ["standalone omitted", "surface->auto_depth_stencil || !surface->texture_child", "surface->auto_depth_stencil"],
        ["CPU standalone omitted", "surface->auto_depth_stencil || !surface->texture_child", "surface->auto_depth_stencil || (!surface->texture_child && surface->texture)"],
        ["GPU standalone omitted", "surface->auto_depth_stencil || !surface->texture_child", "surface->auto_depth_stencil || (!surface->texture_child && !surface->texture)"],
        ["child texture misclassified", "surface->auto_depth_stencil || !surface->texture_child", "surface->auto_depth_stencil || surface->texture_child"],
        ["wrong device identity", "*container = &surface->device->iface;", "*container = &surface->iface;"],
        ["unretained device identity", "IDirect3DDevice9_AddRef(&surface->device->iface);", "(void)surface->device;"],
        ["unsupported device IID accepted", "if (!riid || (!iid_is_unknown(riid)\n                && !guid_equal(riid, &IID_IDirect3DDevice9)))", "if (0 && (!riid || (!iid_is_unknown(riid)\n                && !guid_equal(riid, &IID_IDirect3DDevice9))))"],
    ]) {
        assert(part.includes(old), label); const file = dir + "/frontend.c"; fs.writeFileSync(file, source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_standalone_container_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_STANDALONE_CONTAINER_SOURCE: file}}), output = r.stdout + r.stderr;
        assert(!r.error, label); assert.notEqual(r.status, 0, label + " survived"); assert(!/error:|undefined reference|Undefined symbols/.test(output), label + " compiler failure: " + output); assert(/Standalone container|Assertion|signal=/.test(output), output); killed++;
    }
    console.log("PASS D3D9 standalone-container mutations: " + killed + "/7; GPU/CPU classification, device identity/reference and IID policy, texture/cube/chain isolation");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
