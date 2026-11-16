'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const site=process.env.RETRO_GAMING_SITE||path.resolve(__dirname,'../..'),file=process.env.DDRAW_OVERLAY_CONTRACT_SOURCE||site+'/glbridge/ddrawproxy/ddraw_proxy.c',source=fs.readFileSync(file,'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-overlay-contract-mut-'));
const cases=[
 ['missing initial target returns old error','if (!target) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);','if (!target) return DDWG_TRACE_ERROR(DDERR_NOOVERLAYDEST);'],
 ['default destination uses source width','destination_area.right = (LONG)target->width;','destination_area.right = (LONG)surface->width;'],
 ['default destination uses source height','destination_area.bottom = (LONG)target->height;','destination_area.bottom = (LONG)surface->height;'],
 ['hidden position reports success','if (!surface->overlay_visible) {','if (0 && !surface->overlay_visible) {'],
 ['hidden position leaves y unchanged','*x = *y = 0;','*x = 0;'],
 ['new target reference omitted','surface_AddRef((IDirectDrawSurface7 *)&target->vtbl7);','(void)&surface_AddRef;'],
];
try{for(const[label,from,to]of cases){assert.equal(source.split(from).length,2,label);const f=dir+'/mutant.c';fs.writeFileSync(f,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+'/ddraw_overlay_contract_test.js'],{encoding:'utf8',env:{...process.env,RETRO_GAMING_SITE:site,DDRAW_OVERLAY_CONTRACT_SOURCE:f},timeout:30000,maxBuffer:1024*1024});assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');assert.match(r.stderr,/overlay contract line \d+:/,label+' runtime assertion required');assert(!/error:|undefined reference|Undefined symbols|runtime error:/.test(r.stderr),label+' compilation/sanitizer failure');}console.log('PASS actual overlay contract mutations: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
