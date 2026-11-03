"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-selector-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
try {
    let killed=0;
    for(const api of [8,9]) {
        const source=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'), fn=api===8?'swapchain_get_backbuffer':'swap_chain_get_back_buffer';
        const changes=api===8?[
            ['stereo rejected','if (!surface_out || index)','if (!surface_out || index || type != D3DBACKBUFFER_TYPE_MONO)'],
            ['bad index clears output','    if (!surface_out || index)','    if(surface_out) *surface_out = NULL;\n    if (!surface_out || index)'],
            ['index ignored','if (!surface_out || index)','(void)index; if (!surface_out)'],
            ['successful view loses reference','IDirect3DSurface8_AddRef(*surface_out);','(void)IDirect3DSurface8_AddRef;'],
        ]:[
            ['stereo rejected','if (!out || index)','if (!out || index || type != D3DBACKBUFFER_TYPE_MONO)'],
            ['bad index clears output','    if (!out || index)','    if(out) *out = NULL;\n    if (!out || index)'],
            ['implicit bad index forwarded','if (!out || index)','if (!out || (index && swap_chain->handle))'],
            ['successful surface loses reference','IDirect3DSurface9_AddRef(*out);','(void)IDirect3DSurface9_AddRef;'],
        ];
        for (const [label,old,replacement] of changes) {
            const file=dir+'/frontend.c';fs.writeFileSync(file,mutate(source,fn,old,replacement));
            const r=spawnSync(process.execPath,[__dirname+'/d9wg_backbuffer_selector_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,['D'+api+'WG_SELECTOR_SOURCE']:file}}),out=r.stdout+r.stderr;
            assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');
            assert(!/error:|undefined reference|Undefined symbols/.test(out),label+' compile failure: '+out);assert(/Backbuffer selector|Assertion|signal=/.test(out));killed++;
        }
    }
    console.log('PASS backbuffer selector mutations: '+killed+'/8; both APIs ignore stereo, preserve invalid outputs, guard implicit delegation and retain valid view references');
} finally {fs.rmSync(dir, {recursive: true, force: true});}
