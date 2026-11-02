"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-shader-mutants-"));
const cases=[
 ["empty FVF cannot unbind","if (fvf && !fvf_to_elements","if (!fvf_to_elements"],
 ["deleted public handles remain valid","shader->handle == handle && !shader->deleted","shader->handle == handle"],
 ["shader objects never collected","!shader->deleted || shader->state_refs","!shader->deleted || shader->state_refs || 1"],
 ["recorded VS does not own object","shader_assign(&state->vertex_shader_ref, shader);","state->vertex_shader_ref = shader;"],
 ["recorded PS does not own object","shader_assign(&state->pixel_shader_ref, shader);","state->pixel_shader_ref = shader;"],
 ["live VS binding does not own object","shader_assign(&device->vertex_shader_ref, shader);","device->vertex_shader_ref = shader;"],
 ["vertex declaration is leaked","!shader->is_pixel_shader && shader->declaration_handle","shader->is_pixel_shader && shader->declaration_handle"]
];
try{for(const [label,before,after] of cases){assert.equal(source.split(before).length,2,label);const file=path.join(directory,"shader.c");fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"d8wg_shader_lifetime_test.js")],{env:{...process.env,D8WG_SHADER_LIFETIME_SOURCE:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/shader lifetime line/,"runtime assertion required: "+label);}console.log("PASS D3D8 shader lifetime mutations: 7/7; expired handles, final collection, VS/PS snapshot ownership, live ownership and declaration destruction");}finally{fs.rmSync(directory,{recursive:true,force:true});}
