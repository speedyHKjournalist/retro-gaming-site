"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/d3d7_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-lighting-mutants-"));
const cases=[
 ["prelit vertices relit","!!(fvf & D3DFVF_NORMAL) && !(flags & D3DDP_DONOTLIGHT)","!!(fvf | D3DFVF_NORMAL) && !(flags & D3DDP_DONOTLIGHT)"],
 ["DONOTLIGHT ignored","!(flags & D3DDP_DONOTLIGHT)","!(flags & (D3DDP_DONOTLIGHT * 0u))"],
 ["unrelated flag disables lighting","!(flags & D3DDP_DONOTLIGHT)","!(flags & (D3DDP_DONOTLIGHT | 1u))"],
 ["D3D7 explicit state overridden","if (device->creation_version >= 7u) return D3D_OK;","if (device->creation_version > 7u) return D3D_OK;"],
 ["draw continues after failed lighting submission","if (FAILED(prepare)) return DDWG_TRACE_ERROR(prepare);","if (FAILED(prepare) && 0) return DDWG_TRACE_ERROR(prepare);"],
];
try{for(const[label,before,after]of cases){assert(source.includes(before),label);const file=dir+"/mutant.inc";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_draw_lighting_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_DRAW_LIGHTING_SOURCE:file}});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/draw lighting line \d+:/,"strict compilation and runtime assertion required: "+label+"\n"+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log("KILL "+label);}console.log("PASS legacy draw lighting mutations: 5/5 compiled implementations rejected by draw contract assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
