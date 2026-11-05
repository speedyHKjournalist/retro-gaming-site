"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={header:readComparisonSource(path.join(root,"runtime/d9wg_state_block_scope.h"),"utf8"),d8:readComparisonSource(path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8"),d9:readComparisonSource(path.join(root,"d3d9proxy/d3d9_proxy.c"),"utf8")},directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-scope-mutants-"));
const mutations=[
 ["pixel captures culling","header","D3DRS_ZWRITEENABLE,","D3DRS_CULLMODE,"],
 ["vertex loses tween factor","header","D3DRS_TWEENFACTOR,","D3DRS_ZENABLE,"],
 ["pixel loses shared texture routing","header","return type == D3DSBT_VERTEXSTATE || type == D3DSBT_PIXELSTATE;","return type == D3DSBT_VERTEXSTATE;"],
 ["DMAP captured by pixel","header","return state == D3DSAMP_DMAPOFFSET ? type == D3DSBT_VERTEXSTATE : type == D3DSBT_PIXELSTATE;","return type == D3DSBT_PIXELSTATE;"],
 ["last world matrix omitted","header","#define D9WG_STATE_BLOCK_TRANSFORMS 266u","#define D9WG_STATE_BLOCK_TRANSFORMS 265u"],
 ["vertex captures ALL-only bindings","d8","if (type == D3DSBT_ALL) {\n        FillMemory(state->render_mask","if (type == D3DSBT_ALL || type == D3DSBT_VERTEXSTATE) {\n        FillMemory(state->render_mask"],
 ["NPatch omitted from vertex","d9","block->has_npatch_mode = TRUE;\n","block->has_npatch_mode = FALSE;\n"],
 ["material captured by vertex","d9","block->has_npatch_mode = TRUE;\n","block->has_npatch_mode = TRUE;block->has_material = TRUE;\n"],
 ["vertex captures buffer instead of frequency","d9","block->has_stream_frequency[index] = TRUE;","block->has_stream[index] = TRUE;"],
 ["ALL omits clip planes","d9","FillMemory(block->has_clip_plane, sizeof(block->has_clip_plane), 1);","FillMemory(block->has_clip_plane, sizeof(block->has_clip_plane), 0);"],
 ["pixel captures native-excluded CCW function","header","D3DRS_CCW_STENCILPASS,","D3DRS_CCW_STENCILPASS, D3DRS_CCW_STENCILFUNC,"]
];
const variables={header:"D9WG_SCOPE_HEADER",d8:"D8WG_SCOPE_SOURCE",d9:"D9WG_SCOPE_SOURCE"};
function run(file,kind,label){const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_state_block_scope_test.js")],{env:{...process.env,[variables[kind]]:file},encoding:"utf8"});assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/scope API [89] line/,"semantic runtime failure required: "+label);}
try{
 for(const [label,kind,before,after] of mutations){const source=sources[kind];assert.equal(source.split(before).length,2,label);const file=path.join(directory,kind+".h");fs.writeFileSync(file,source.replace(before,after));run(file,kind,label);}
 if(process.argv.includes("--previous-source"))for(const api of [8,9]){const result=spawnSync("git",["show","ead272a:glbridge/d3d"+api+"proxy/d3d"+api+"_proxy.c"],{cwd:root,encoding:"utf8"});assert.equal(result.status,0,result.stderr);const file=path.join(directory,"previous"+api+".c");fs.writeFileSync(file,result.stdout);run(file,"d"+api,"previous production C ead272a API "+api);console.log("PASS previous D3D"+api+" C source ead272a fails independent scope checks");}
 console.log("PASS predefined scope C mutations: 11/11; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
