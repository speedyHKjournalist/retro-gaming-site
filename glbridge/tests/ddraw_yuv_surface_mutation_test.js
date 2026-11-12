"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-yuv-mutants-"));
const cases=[
 ["packed format allocation omitted","? layout->block_bytes / layout->block_width : 0u;","? (layout->block_width == 1u ? layout->block_bytes : 0u) : 0u;"],
 ["odd left edge not widened","left &= ~1u;","left += 0u;"],
 ["odd width upload omits shared chroma","((right + 1u) / 2u - left / 2u) * 4u","(right - left) * 2u"],
 ["raw pair halves reversed","(const BYTE *)&value + (x & 1) * 2u","(const BYTE *)&value + ((x & 1) ^ 1) * 2u"],
 ["neighbouring row written","BYTE *row = surface->shadow + y * surface->pitch;","BYTE *row = surface->shadow + ((y + 1) % surface->height) * surface->pitch;"],
 ["failed upload loses retry debt","if (!emit_surface_upload(surface, &area)) {\n        surface->upload_pending = TRUE;", "if (!emit_surface_upload(surface, &area)) {\n        surface->upload_pending = FALSE;"],
];
try{for(const [label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[path.join(__dirname,"ddraw_yuv_surface_test.js")],{env:{...process.env,DDRAW_YUV_SOURCE:file},encoding:"utf8"});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/DDraw YUV line/,"must compile then fail a runtime assertion: "+label+"\n"+r.stderr);console.log("KILL "+label);}console.log("PASS packed YUV mutations: "+cases.length+"/"+cases.length+", compilation and runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
