"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/d3d_legacy_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-material-mut-"));
const cases=[
 ["material adds public parent ref","ddraw_child_attach(owner, FALSE);\n    *out = material;","ddraw_child_attach(owner, 1);\n    *out = material;"],
 ["material releases public parent ref","ddraw_child_detach(owner, FALSE);","ddraw_child_detach(owner, 1);"],
 ["public-zero parent allocation leaked","ddraw_child_detach(owner, FALSE);","if (owner->ref) ddraw_child_detach(owner, FALSE);"],
];
try{for(const[label,from,to]of cases){assert(source.includes(from),label);const file=dir+"/mutant.c";fs.writeFileSync(file,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+"/ddraw_material_lifetime_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_MATERIAL_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/material lifetime line/,label+": must compile then fail lifetime assertions\n"+r.stderr);assert(!/error: /.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS material lifetime mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected");}finally{fs.rmSync(dir,{recursive:true,force:true});}
