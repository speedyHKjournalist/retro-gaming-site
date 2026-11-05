'use strict';
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-chain-mutations-'));
function run(name,test,env){const r=spawnSync(process.execPath,[__dirname+'/'+test],{encoding:'utf8',timeout:30000,env:{...process.env,...env}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compile failure: '+output);assert(/nonblocking line|Chain pacing|Assertion|signal=/.test(output),name+' no runtime failure');}
function span(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return[m.index,end];}
try{let killed=0;const runtime=readComparisonSource(root+'/runtime/d9wg_present_pacing.c','utf8');for(const[name,old,replacement]of[
 ['busy accepted','if (present_step(&next, frames, refresh, now, hz, &maximum, &tolerance))\n        return 0;','if (present_step(&next, frames, refresh, now, hz, &maximum, &tolerance))\n        return 1;'],
 ['successful candidate discarded','    *state = next;','    (void)next;'],
 ['busy state modified','D9WGPresentPacing next = *state;','D9WGPresentPacing next = *state; state->fraction++;'],
]){assert(runtime.includes(old),name);const file=dir+'/runtime.c';fs.writeFileSync(file,runtime.replace(old,replacement));run(name,'d9wg_present_nonblocking_test.js',{D9WG_CHAIN_RUNTIME:file});killed++;}
const bridge=readComparisonSource(root+'/runtime/d9wg_present_pacing_bridge.h','utf8');for(const[name,old,replacement]of[
 ['nonblocking deadline ignored','return d9wg_present_try(candidate, frames, refresh, now, clock->hz);','(void)refresh; (void)now; return 1;'],
 ['nonblocking interval ignored','return d9wg_present_try(candidate, frames, refresh, now, clock->hz);','return d9wg_present_try(candidate, 0, refresh, now, clock->hz);'],
]){assert(bridge.includes(old),name);const file=dir+'/bridge.h';fs.writeFileSync(file,bridge.replace(old,replacement));run(name,'d9wg_swapchain_pacing_entry_test.js',{D9WG_CHAIN_BRIDGE:file});killed++;}
for(const api of[8,9]){const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),[a,b]=span(source,api===8?'swapchain_present':'swap_chain_present'),body=source.slice(a,b),mutations=api===8?[
 ['successful chains unpaced','if (ok)','if (0 && ok)'],
 ['failed chains paced','if (ok)','if (1)'],
 ['shared device pacing','&chain->present_pacing','&chain->device->present_pacing'],
 ['shared device clock','&chain->present_clock','&chain->device->present_clock'],
 ['window policy omitted','chain->present.Windowed, chain->present.SwapEffect','chain->present.Windowed, 1'],
]:[
 ['successful chains unpaced','d9wg_throttle_present(clock, pacing, interval, device->display_mode.RefreshRate);','(void)clock;'],
 ['failed candidate published','if (ok) {','if (1) {'],
 ['explicit clock shared with device','swap_chain->handle ? &swap_chain->present_clock : &device->present_clock','&device->present_clock'],
 ['explicit pacing shared with device','swap_chain->handle ? &swap_chain->present_pacing : &device->present_pacing','&device->present_pacing'],
 ['implicit pacing detached from device','swap_chain->handle ? &swap_chain->present_pacing : &device->present_pacing','&swap_chain->present_pacing'],
 ['busy emitted','if (nonblocking && !d9wg_prepare_present','if (0 && nonblocking && !d9wg_prepare_present'],
 ['candidate discarded','*pacing = candidate;','(void)candidate;'],
 ['DONOTWAIT ignored','(flags & D3DPRESENT_DONOTWAIT) != 0','(flags & 0) != 0'],
 ['combined LINEAR_CONTENT loses DONOTWAIT','(flags & D3DPRESENT_DONOTWAIT) != 0','flags == D3DPRESENT_DONOTWAIT'],
];for(const[name,old,replacement]of mutations){assert(body.includes(old),name);const file=dir+'/frontend.c';fs.writeFileSync(file,source.slice(0,a)+body.replace(old,replacement)+source.slice(b));run('API '+api+' '+name,'d9wg_swapchain_pacing_entry_test.js',{['D'+api+'WG_CHAIN_FRONTEND']:file});killed++;}}
console.log('PASS swap-chain pacing mutations: '+killed+'/19; independent clocks/cadence, busy no-emission, flags combinations, candidate commit and reported failure atomicity');}finally{fs.rmSync(dir,{recursive:true,force:true});}
