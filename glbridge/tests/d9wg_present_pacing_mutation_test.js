"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-mut-"));



function run(name,test,env){const r=spawnSync(process.execPath,[__dirname+'/'+test],{encoding:'utf8',timeout:30000,env:{...process.env,...env}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compile failure: '+output);assert(/Present pacing|Present API|Assertion|signal=/.test(output),name+' no runtime failure');}
const runtime=readComparisonSource(root+'/runtime/d9wg_present_pacing.c','utf8'),bridge=readComparisonSource(root+'/runtime/d9wg_present_pacing_bridge.h','utf8');
const mutations=[
 ['early wakes not retried','sample < 8','sample < 2'],
 ['stalled short waits never increased','wait <= ceiling / 2 ? wait * 2 : ceiling','wait < ceiling ? wait : ceiling'],
 ['windowed D8 always uses requested interval','return windowed ? 0x80000000u : requested;','return requested;'],
 ['COPY and COPY_VSYNC confused','swap_effect == 4','swap_effect == 3'],
 ['fullscreen COPY_VSYNC immediate bypass','windowed || requested == 0x80000000u','windowed'],
 ['COPY_VSYNC mapped to immediate','if (swap_effect == 4 && (windowed || requested == 0x80000000u))\n        return 1;','if (swap_effect == 4 && (windowed || requested == 0x80000000u))\n        return 0x80000000u;'],
 ['DEFAULT disabled','case 0:\n    case 1:\n        return 1;','case 0: return 0;\n    case 1:\n        return 1;'],
 ['THREE flag interpreted as four','case 4:\n        return 3;','case 4:\n        return 4;'],
 ['IMMEDIATE paced','    default:\n        return 0;','    default:\n        return 1;'],
 ['refresh fallback wrong','refresh = 60;','refresh = 50;'],
 ['fractional remainder discarded','state->fraction += numerator % refresh;','state->fraction = 0;'],
 ['millisecond debt mistaken for clock jump','*maximum + *tolerance','*maximum'],
 ['wait in counter ticks','(uint32_t)remaining * 1000u / hz','(uint32_t)remaining'],
 ['clock read after sleep omitted','ops->sleep(ops->context, wait);\n        ops->read_clock(ops->context, &now, &hz);','ops->sleep(ops->context, wait);'],
 ['timebase change retains deadline','state->hz != hz || state->refresh != refresh || state->frames != frames','state->refresh != refresh || state->frames != frames'],
 ['first frame waits on invalid history','state->ready && remaining > 0','remaining > 0'],
 ['late frame keeps old deadline','(int32_t)(now - state->deadline) >= (int32_t)*maximum','0'],
 ['manual reset leaves cadence ready','state->ready = 0;','state->ready = 1;'],
];
try{let killed=0;for(const[name,old,replacement]of mutations){assert(runtime.includes(old),name);const file=dir+'/runtime.c';fs.writeFileSync(file,runtime.replaceAll(old,replacement));run(name,'d9wg_present_pacing_test.js',{D9WG_PACING_RUNTIME_SOURCE:file});killed++;}
for(const[name,old,replacement]of[
 ['stalled QPC never falls back','if (frames && !pacing->ready && clock->shift != 0xff)','if (0 && frames && !pacing->ready && clock->shift != 0xff)'],
 ['fallback keeps QPC frequency','clock->shift = 0xff;\n    clock->hz = 1000;','clock->shift = 0xff;\n    (void)clock->hz;'],
 ['successful QPC treated as failure','clock->shift != 0xff && QueryPerformanceCounter(&value)','clock->shift != 0xff && !QueryPerformanceCounter(&value)'],
 ['counter not shifted with frequency','return (uint32_t)(value.QuadPart >> clock->shift);','return (uint32_t)value.QuadPart;'],
 ['disabled setting ignored',"value = size && size < sizeof(text) && text[0] != '0';",'value = 0; (void)size;'],
]){assert(bridge.includes(old),name);const file=dir+'/bridge.h';fs.writeFileSync(file,bridge.replace(old,replacement));run(name,'d9wg_present_pacing_entry_test.js',{D9WG_PACING_BRIDGE:file});killed++;}
for(const api of[8,9])for(const[name,replacement]of[['accepted frames never paced','if (0 && ok)'],['failed frames paced','if (1)']]){const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),start=source.indexOf('static HRESULT WINAPI device_present('),end=source.indexOf('\nstatic ',start+1),part=source.slice(start,end);assert(part.includes('if (ok)'));const file=dir+'/frontend.c';fs.writeFileSync(file,source.slice(0,start)+part.replace('if (ok)',replacement)+source.slice(end));run('API '+api+' '+name,'d9wg_present_pacing_entry_test.js',{['D'+api+'WG_PACING_FRONTEND']:file});killed++;}
console.log('PASS Present pacing mutations: '+killed+'/27; interval policy, rational cadence, sub-ms debt, wrap/jump/late clocks, actual fallback/shift/disable and both public submission paths');}finally{fs.rmSync(dir,{recursive:true,force:true});}
