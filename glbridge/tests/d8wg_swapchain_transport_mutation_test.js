"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-chain-transport-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    const source=readComparisonSource(root+'/d3d8proxy/d3d8_proxy.c','utf8');let killed=0;
    for(const[label,old,replacement]of[
        ['primary opcode used','D9WG_OP_PRESENT_SWAP_CHAIN','D9WG_OP_PRESENT'],
        ['device handle used as chain','present.swap_chain_handle = chain->handle;','present.swap_chain_handle = chain->device->handle;'],
        ['override ignored','override_window ? override_window : chain->present.hDeviceWindow','chain->present.hDeviceWindow; (void)override_window'],
        ['origin discarded','present.x = surface.x;','present.x = 0;'],
        ['empty extent clamped','present.width = surface.width;','present.width = surface.width ? surface.width : 1;'],
        ['reserved field undefined','present.reserved = 0;','present.reserved = 0xab;'],
        ['submit failure swallowed','result = submit_batch_locked(TRUE);','(void)submit_batch_locked(TRUE);'],
        ['reserve failure swallowed','return result;','return TRUE;'],
        ['transport lock leaked','LeaveCriticalSection(&g_transport_lock);','(void)LeaveCriticalSection;'],
        ['transport retry omitted','d9wg_window_watch_retry();','(void)d9wg_window_watch_retry;'],
    ]){
        const file=dir+'/frontend.c';fs.writeFileSync(file,mutate(source,'emit_present_swap_chain_and_flush',old,replacement));
        const r=spawnSync(process.execPath,[__dirname+'/d8wg_swapchain_transport_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,D8WG_CHAIN_TRANSPORT_SOURCE:file}}),out=r.stdout+r.stderr;
        assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');assert(!/error:|undefined reference|Undefined symbols/.test(out),label+' compile failure: '+out);assert(/D8 chain transport|Assertion|signal=/.test(out));killed++;
    }
    console.log('PASS D3D8 chain transport mutations: '+killed+'/10; opcode/identity, window geometry, reserved bits, failures, lock cleanup and retry');
} finally {fs.rmSync(dir, {recursive: true, force: true});}
