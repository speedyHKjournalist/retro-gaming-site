"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),sources={profiles:fs.readFileSync(root+'/runtime/d9wg_lighting_profiles.c','utf8'),kernel:fs.readFileSync(root+'/runtime/d9wg_lighting.c','utf8'),vertex:fs.readFileSync(root+'/runtime/d9wg_vertex.c','utf8'),front:fs.readFileSync(root+'/ddrawproxy/d3d7_proxy.inc','utf8')},dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-attenuation-mutants-'));
const cases=[
 ['legacy attenuation profile omitted','profiles','state->legacy_attenuation=1;','state->legacy_attenuation=0;'],
 ['legacy coefficients reversed','kernel','light->attenuation[0] + light->attenuation[1]*fade\n                            + light->attenuation[2]*fade*fade','light->attenuation[2] + light->attenuation[1]*fade\n                            + light->attenuation[0]*fade*fade'],
 ['range fade uses distance denominator','kernel','float fade = 1.0f - distance / light->range;','float fade = 1.0f - distance / maxf(distance,1e-6f);'],
 ['range boundary incorrectly included','kernel','distance < light->range','distance <= light->range'],
 ['attenuation prematurely capped','kernel','+ light->attenuation[2]*fade*fade;','+ light->attenuation[2]*fade*fade; attenuation = clamp01(attenuation);'],
 ['legacy profile leaks into modern defaults','kernel','state->legacy_attenuation = 0;','state->legacy_attenuation = 1;'],
 ['implicit weights ignore source index','front','((uint64_t)first + vertex)*source_stride','(uint64_t)vertex*source_stride'],
 ['implicit weights read position Z','front','weight_offset = offset + 12u + weight*4u;','weight_offset = offset + 8u + weight*4u;'],
 ['implicit weights read beyond logical backing','front','weight_offset + 4u <= source_bytes','weight_offset + 4u <= (uint64_t)source_bytes + 16u'],
 ['public implicit-weight route disabled','front','if (count && fvf == D3DFVF_XYZ && weights >= 1u && weights <= 3u)','if (FALSE && count && fvf == D3DFVF_XYZ && weights >= 1u && weights <= 3u)'],
 ['D3D7 zero-W profile omitted','front','vertex_state.blend_zero_w_nan = device->creation_version >= 7u && !(vertex_op & D3DVOP_CLIP);','vertex_state.blend_zero_w_nan = FALSE;'],
 ['blended zero-W becomes finite','vertex','const uint32_t nan_word = 0xffc00000u;','const uint32_t nan_word = 0u;'],
 ['zero-W profile leaks into modern defaults','vertex','state->blend_zero_w_nan = 0;','state->blend_zero_w_nan = 1;'],
];
try{for(const[label,kind,before,after]of cases){const source=sources[kind];assert.equal(source.split(before).length,2,label);const file=dir+'/mutant.'+(kind==='front'?'inc':'c');fs.writeFileSync(file,source.replace(before,after));const key={profiles:'DDRAW_NORMAL_PROFILES_SOURCE',kernel:'DDRAW_PROCESS_LIGHT_KERNEL',vertex:'DDRAW_PROCESS_VERTEX_KERNEL',front:'DDRAW_PROCESS_LIGHT_SOURCE'}[kind];const run=spawnSync(process.execPath,[__dirname+'/ddraw_process_lighting_test.js'],{env:{...process.env,[key]:file},encoding:'utf8'});assert.notEqual(run.status,0,'survived '+label);assert.match(run.stderr,/process lighting line [0-9]+:/,'strict compilation and runtime assertion required: '+label+'\n'+run.stderr.slice(-2000));assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr.slice(-2000));console.log('KILL '+label);}console.log('PASS native attenuation/implicit-weight faults: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
