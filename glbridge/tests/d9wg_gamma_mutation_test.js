"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-gamma-mut-")),header=fs.readFileSync(root+"/runtime/d9wg_gamma_bridge.h","utf8");
const mutations=[
 ["identity scale truncated","i * 257u","i * 256u"],
 ["red bank uses green","command.ramp[i] = ramp->red[i];","command.ramp[i] = ramp->green[i];"],
 ["blue bank uses green","command.ramp[512 + i] = ramp->blue[i];","command.ramp[512 + i] = ramp->green[i];"],
 ["sixteen bit values truncated","command.ramp[256 + i] = ramp->green[i];","command.ramp[256 + i] = ramp->green[i] & 255u;"],
 ["wrong wire flags","command.flags = flags;","command.flags = 0; (void)flags;"],
 ["wrong device","command.device_handle = device_handle;","command.device_handle = 0; (void)device_handle;"],
 ["wrong chain","command.swap_chain = 0;","command.swap_chain = 1;"],
 ["wrong reserved word","command.reserved = 0;","command.reserved = 1;"],
 ["cache not copied","*stored = *ramp;","(void)stored;"],
 ["cache presence lost","*supplied = TRUE;","*supplied = FALSE;"],
 ["cache flags lost","*stored_flags = flags;","*stored_flags = 0;"],
 ["setter ignores all valid requests","if (!ramp)\n        return;","if (ramp)\n        return;"],
 ["setter never sends","d9wg_emit_gamma_ramp(device_handle, flags, ramp);","(void)device_handle;"],
 ["getter always identity","if (supplied)","if (0 && supplied)"],
 ["failed wire result hidden","return emit_command(D9WG_OP_SET_GAMMA_RAMP, &command, sizeof(command));","return (emit_command(D9WG_OP_SET_GAMMA_RAMP, &command, sizeof(command)), TRUE);"],
];
function run(name,env){const r=spawnSync(process.execPath,[__dirname+"/d9wg_gamma_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,...env}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/Gamma API|Assertion|signal=/.test(output),name+" no runtime failure");}
try{let killed=0;for(const[name,old,replacement]of mutations){assert(header.includes(old),name);const file=dir+"/candidate.h";fs.writeFileSync(file,header.replace(old,replacement));run(name,{D9WG_GAMMA_HEADER:file});killed++;}for(const api of [8,9]){const source=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),needle='if (device->gamma_ramp_set && !d9wg_emit_gamma_ramp(device->handle,';assert(source.includes(needle));const file=dir+'/candidate.c';fs.writeFileSync(file,source.replace(needle,'if (0 && device->gamma_ramp_set && !d9wg_emit_gamma_ramp(device->handle,'));run('API '+api+' Reset drops ramp',{['D'+api+'WG_GAMMA_SOURCE']:file});killed++;}console.log('PASS shared gamma mutations: '+killed+'/'+(mutations.length+2)+'; identity, banks/precision, flags/device/chain, cache/query/NULL, failed emit and both actual Reset paths');}finally{fs.rmSync(dir,{recursive:true,force:true});}
