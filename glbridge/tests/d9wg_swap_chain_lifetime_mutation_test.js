"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-chain-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"); let killed = 0;
    for (const [label, fn, old, replacement] of [
        ["implicit pin per public ref", "swap_chain_add_ref", "!swap_chain->handle && refs == 1", "!swap_chain->handle && refs >= 1"],
        ["additional AddRef leaks device", "swap_chain_add_ref", "!swap_chain->handle && refs == 1", "swap_chain->handle || refs == 1"],
        ["implicit has no pin", "swap_chain_add_ref", "!swap_chain->handle && refs == 1", "0 && !swap_chain->handle && refs == 1"],
        ["implicit unpins on intermediate Release", "swap_chain_release", "if (!refs)\n        device_child_release(device);", "if (1)\n        device_child_release(device);"],
        ["implicit retains final pin", "swap_chain_release", "if (!refs)\n        device_child_release(device);", "if (0 && !refs)\n        device_child_release(device);"],
        ["zero Release underflows", "swap_chain_release", "    if (!InterlockedCompareExchange(&swap_chain->refcount, 0, 0))\n        return 0;", ""],
        ["additional list keeps retired chain", "swap_chain_release", "if (*link) *link = swap_chain->next_device_chain;", "if (0 && *link) *link = swap_chain->next_device_chain;"],
        ["additional backing leaked", "swap_chain_release", "            if (swap_chain->back_buffer)", "            if (0 && swap_chain->back_buffer)"],
        ["additional pin leaked", "swap_chain_release", "            device_child_release(device);", ""],
        ["additional allocation leaked", "swap_chain_release", "            HeapFree(GetProcessHeap(), 0, swap_chain);", ""],
        ["additional destroyed before final ref", "swap_chain_release", "        if (!refs) {", "        if (refs <= 1) {"],
        ["identity omits reference", "swap_chain_query_interface", "    IDirect3DSwapChain9_AddRef(iface);", ""],
        ["GetSwapChain omits reference", "device_get_swap_chain", "    IDirect3DSwapChain9_AddRef(*out);", ""],
        ["GetDevice omits reference", "swap_chain_get_device", "    IDirect3DDevice9_AddRef(*out);", ""],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement));
        const r = spawnSync(process.execPath, [__dirname + "/d9wg_swap_chain_lifetime_test.js"], {encoding: "utf8", timeout: 30000, env: {...process.env, D9WG_CHAIN_LIFETIME_SOURCE: file}}), out = r.stdout + r.stderr;
        assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived");
        assert(!/error:|undefined reference|Undefined symbols/.test(out), label + " compile failure: " + out); assert(/Swap-chain lifetime|Assertion|signal=/.test(out)); killed++;
    }
    console.log("PASS swap-chain lifetime mutations: " + killed + "/14; pin edges, additional ref balance, zero Release, unlink, backing cleanup and public identity/parent routes");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
