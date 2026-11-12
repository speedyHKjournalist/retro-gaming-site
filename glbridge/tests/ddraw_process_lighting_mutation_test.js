'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),front=fs.readFileSync(root+'/ddrawproxy/d3d7_proxy.inc','utf8'),kernel=fs.readFileSync(root+'/runtime/d9wg_lighting.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-process-light-mutants-'));
const cases=[
 ["D3D3 incorrectly applies DONOTCOPYDATA","front","device->creation_version < 7u || !(flags & D3DPV_DONOTCOPYDATA)","!(flags & D3DPV_DONOTCOPYDATA)"],
 ["missing legacy material still CPU lit","front","(device->creation_version >= 7u || device->legacy_material)","TRUE"],
 ['modern GPU zero-power incorrectly disabled','front','device->creation_version < 7u && material->power == 0.0f','device->creation_version >= 7u && material->power == 0.0f'],
 ['D3D7 material alpha profile disabled','front','state->specular_alpha_material = device->creation_version >= 7u;','state->specular_alpha_material = FALSE;'],
 ['legacy CPU zero-power incorrectly enabled','front','if (device->creation_version < 7u && device->material.power == 0.0f)','if (FALSE && device->creation_version < 7u && device->material.power == 0.0f)'],
 ['zero power bypasses processing history','front','light_state.material.power = device->process_specular_power;','light_state.material.power = 0.0f;'],
 ['initial zero-power produces specular RGB','front','if (light_state.material.power == 0.0f)\n            light_state.flags.specular_enable = FALSE;','if (FALSE && light_state.material.power == 0.0f)\n            light_state.flags.specular_enable = FALSE;'],
 ['SetMaterial incorrectly advances CPU history','front','device->material = *material;','device->material = *material; if (material->power > 0.0f) device->process_specular_power = material->power;'],
 ['zero-power processing erases history','front','device->material.power > 0.0f)','device->material.power >= 0.0f)'],
 ['processed shininess history never advances','front','device->process_specular_power = device->material.power;','device->process_specular_power = 0.0f;'],
 ['recording applies material immediately','front','block->material_mask = TRUE;block->material = *material;','block->material_mask = TRUE;block->material = *material; device->material = *material;'],
 ['failed submission updates public material','front','if (!emit_command(D9WG_OP_SET_MATERIAL, &command, sizeof(command)))','if ((device->material = *material, !emit_command(D9WG_OP_SET_MATERIAL, &command, sizeof(command))))'],
 ['default D3D8/9 profile left uninitialised','kernel','state->specular_alpha_material = 0;','/* lost default profile initialisation */'],
 ['D3D7 alpha rule leaks to D3D8/9','kernel','state->flags.lighting && state->specular_alpha_material','state->flags.lighting'],
 ['D3D7 alpha ignores colour source','kernel','material_source(state, state->flags.specular_source, state->material.specular,\n                    color0, color1, (color0 ? 1u : 0u) | (color1 ? 2u : 0u))[3]','state->material.specular[3]'],
];
try{for(const[name,kind,before,after]of cases){const original=kind==='front'?front:kernel;assert.equal(original.split(before).length,2,name);const file=dir+'/mutant.'+(kind==='front'?'inc':'c');fs.writeFileSync(file,original.replace(before,after));const env={...process.env,[kind==='front'?'DDRAW_PROCESS_LIGHT_SOURCE':'DDRAW_PROCESS_LIGHT_KERNEL']:file};const run=spawnSync(process.execPath,[__dirname+'/ddraw_process_lighting_test.js'],{encoding:'utf8',env});assert.notEqual(run.status,0,'survived '+name);assert(/process lighting line \d+:/.test(run.stderr),'compiled runtime assertion required: '+name+'\n'+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log('KILL '+name);}console.log('PASS actual ProcessVertices lighting faults: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
