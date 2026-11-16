"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-additional-mut-"));
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
        const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),fn=api===8?'device_create_swapchain':'device_create_additional_swap_chain',p=api===8?'parameters':'params';
        const changes=[
            ['caller output omitted','    d9wg_present_parameters_return('+(api===9?'api':api)+', '+p+', &chain->present);',''],
            ['cached parameters not resolved','    d9wg_present_parameters_effective(&chain->present, '+p+',\n        width, height, format, window);','    chain->present = *'+p+'; (void)format;'],
            ['secondary window ignored',p+'->hDeviceWindow ? '+p+'->hDeviceWindow','0 && '+p+'->hDeviceWindow ? '+p+'->hDeviceWindow'],
            ['count limit ignored',api===8?'|| parameters->BackBufferCount > 3':'if (params->BackBufferCount > 3)',api===8?'|| (0 && parameters->BackBufferCount > 3)':'if (0 && params->BackBufferCount > 3)'],
            ['explicit width ignored',api===8?'d9wg_present_extent(parameters->BackBufferWidth':'width = params->BackBufferWidth ? params->BackBufferWidth',api===8?'d9wg_present_extent(0u * parameters->BackBufferWidth':'width = 0 && params->BackBufferWidth ? params->BackBufferWidth'],
        ];
        for(const[label,old,replacement]of changes){
            const file=dir+'/frontend.c';fs.writeFileSync(file,mutate(source,fn,api===8?old.replace("d9wg_present_parameters_effective(","d9wg_d3d9_core_parameters_effective8("):old,replacement));
            const r=spawnSync(process.execPath,[__dirname+'/d9wg_additional_parameters_entry_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,['D'+api+'WG_ADDITIONAL_PARAMETERS_SOURCE']:file}}),out=r.stdout+r.stderr;
            assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');assert(!/error:|undefined reference|Undefined symbols/.test(out),label+' compile failure: '+out);assert(/Additional parameters|Assertion|signal=/.test(out));killed++;
        }
    }
    console.log('PASS additional parameter mutations: '+killed+'/10; both caller/cache routes, distinct windows, count limits and explicit dimensions');
} finally {fs.rmSync(dir, {recursive: true, force: true});}
