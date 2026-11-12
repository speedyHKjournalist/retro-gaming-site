"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/d3d7_proxy.inc"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d7wg-light-mutants-"));
const protocol=fs.readFileSync(path.join(__dirname,'../ddrawproxy/ddraw_protocol.h'),'utf8');
const cases=[
 ['parallel point collapsed to directional','case D3D7LIGHT_GLSPOT: return D3D7LIGHT_SPOT;','case D3D7LIGHT_PARALLELPOINT: return D3D7LIGHT_DIRECTIONAL; case D3D7LIGHT_GLSPOT: return D3D7LIGHT_SPOT;',1,true],
 ['wire light direction overwritten with position','command.range = light->dvRange;','if (light->dltType == D3DLIGHT_PARALLELPOINT) CopyMemory(command.direction, &light->dvPosition, sizeof(command.direction)); command.range = light->dvRange;',1],
 ['legacy low-index limit','if (!light) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);','if (!light || index >= D3D7_MAX_LIGHTS) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);',2],
 ['unknown LightEnable refused','    if (!*set) {\n        d9wg_light_default(light);','    if (!*set) {\n        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);\n        d9wg_light_default(light);',1],
 ['recorded light mutates live','if (device->recording_state_block) {\n        D3D7StateBlock *block = device->recording_state_block;\n        if (index < D3D7_MAX_LIGHTS)','if (0 && device->recording_state_block) {\n        D3D7StateBlock *block = device->recording_state_block;\n        if (index < D3D7_MAX_LIGHTS)',2],
 ['recorded default light empty','d9wg_light_default(&entry->light);entry->light_mask = TRUE;','memset(&entry->light,0,sizeof(entry->light));entry->light_mask = TRUE;',1],
 ['End overwrites recorded light','    /* Recorded values and references are complete; End only publishes. */','    d3d7_state_block_capture(device, block);\n    /* Recorded values and references are complete; End only publishes. */',1],
 ['Capture expands to new light keys','block->all_lights_scope = FALSE;','block->all_lights_scope = TRUE;',1],
 ['vertex block captures properties','saved->light_mask = block->type == D3DSBT_ALL;','saved->light_mask = TRUE;',1],
 ['sparse lights omitted from TNL','if (entry->set && entry->enabled && !d9wg_lighting_sdk_add(state, entry->index,','if (entry->set && entry->enabled && entry->index < 8 && !d9wg_lighting_sdk_add(state, entry->index,',1]
];
try{for(const [label,before,after,count,protocolOnly] of cases){const original=protocolOnly?protocol:source;assert.equal(original.split(before).length,count+1,label);const file=path.join(directory,'light.inc');fs.writeFileSync(file,original.split(before).join(after));const run=spawnSync(process.execPath,[path.join(__dirname,'d7wg_light_state_test.js')],{env:{...process.env,[protocolOnly?"D7WG_LIGHT_PROTOCOL_SOURCE":"D7WG_LIGHT_STATE_SOURCE"]:file},encoding:'utf8'});assert.notEqual(run.status,0,'survived '+label);assert.match(run.stderr,/D3D7 light state line/,'runtime assertion required: '+label);}console.log('PASS D3D7 light-state mutations: 10/10; arbitrary indices, defaults, deferred recording/End, fixed capture keys, native vertex masks and sparse TNL');}finally{fs.rmSync(directory,{recursive:true,force:true});}
