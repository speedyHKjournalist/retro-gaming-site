"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-record-mutants-"));
const mutations=[
 ["recorded render state changes live device","recorded->render_states[state] = value;","device->render_states[state] = value;"],
 ["same-value recording discarded","recorded->render_mask[state] = 1;","if (device->render_states[state] == value) return D3D_OK;recorded->render_mask[state] = 1;"],
 ["recorded render state emitted early","recorded->render_states[state] = value;return D3D_OK;","recorded->render_states[state] = value;return emit_render_state(device, state, value) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;"],
 ["recorded viewport changes live device","recorded->viewport = *viewport;","device->viewport = *viewport;"],
 ["recorded material changes live device","device->recording_state_block->state.material = *material;","device->material = *material;"],
 ["recorded multiplication ignores earlier recorded matrix","recorded->transform_mask[slot] ? &recorded->transforms[slot] : &device->transforms[slot]","&device->transforms[slot]"]
];
function run(file,label){const result=spawnSync(process.execPath,[path.join(__dirname,"d8wg_recording_test.js")],{env:{...process.env,D8WG_RECORD_SOURCE:file},encoding:"utf8"});assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/recording line/,"semantic runtime failure required: "+label);}
try{
 for(const [label,before,after] of mutations){assert.equal(source.split(before).length,2,label);const file=path.join(directory,"d3d8.c");fs.writeFileSync(file,source.replace(before,after));run(file,label);}
 if(process.argv.includes("--previous-source")){const result=spawnSync("git",["show","8c86698:glbridge/d3d8proxy/d3d8_proxy.c"],{cwd:root,encoding:"utf8"});assert.equal(result.status,0,result.stderr);const file=path.join(directory,"previous.c");fs.writeFileSync(file,result.stdout);run(file,"previous production source 8c86698");console.log("PASS previous D3D8 C source 8c86698 fails deferred recording checks");}
 console.log("PASS actual D3D8 C recording mutations: 6/6; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
