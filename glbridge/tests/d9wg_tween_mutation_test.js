"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,"../runtime"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-tween-mutants-"));
const cases=[
 ["blend","secondary position ignored","if (layout->position2 >= 0) {","if (0 && layout->position2 >= 0) {"],
 ["blend","secondary normal ignored","if (layout->normal2 >= 0) {","if (0 && layout->normal2 >= 0) {"],
 ["blend","factor clamped to unit interval","const float factor = blend->tween_factor;","const float factor = blend->tween_factor < 0 ? 0 : blend->tween_factor > 1 ? 1 : blend->tween_factor;"],
 ["blend","position weights reversed","tweened[i] = position[i]*(1.0f - factor) + second[i]*factor;","tweened[i] = position[i]*factor + second[i]*(1.0f - factor);"],
 ["blend","normal weights reversed","normal[i] = normal[i]*(1.0f - factor) + second[i]*factor;","normal[i] = normal[i]*factor + second[i]*(1.0f - factor);"],
 ["blend","both secondary channels required","return layout->position2 >= 0 || layout->normal2 >= 0;","return layout->position2 >= 0 && layout->normal2 >= 0;"],
 ["blend","missing secondary channels accepted","return layout->position2 >= 0 || layout->normal2 >= 0;","return 1;"],
 ["blend","packed secondary position read as FLOAT3","d9wg_decl_read(layout->position2_type,","d9wg_decl_read(2,"],
 ["blend","packed secondary normal read as FLOAT3","d9wg_decl_read(layout->normal2_type,","d9wg_decl_read(2,"],
 ["windows","factor reads wrong render state","(const uint8_t *)render_states + 170u*4u","(const uint8_t *)render_states + 169u*4u"],
];
try{
 for(const [kind,label,before,after] of cases){const original=fs.readFileSync(path.join(root,kind==="windows"?"d9wg_vertex_windows.h":"d9wg_blend.c"),"utf8");assert.equal(original.split(before).length,2,label);const file=path.join(directory,kind==="windows"?"windows-state.h":"blend.c");fs.writeFileSync(file,original.replace(before,after));const variable=kind==="windows"?"D9WG_TWEEN_WINDOWS_HEADER":"D9WG_TWEEN_BLEND_SOURCE";const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_tween_test.js")],{env:{...process.env,[variable]:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/tween mismatch header line/,"runtime assertion required: "+label);}
 console.log("PASS C tween mutations: 10/10; position/normal interpolation, factor range and state, independent/required channels and packed types");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
