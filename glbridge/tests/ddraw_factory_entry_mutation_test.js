"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-entry-mut-"));
const rows=[
 ["extended factory loses lock mode","DirectDrawCreateEx","object->extended_factory = TRUE;","object->extended_factory = FALSE;"],
 ["legacy factory acquires extended mode","DirectDrawCreate","object->initialized = TRUE;","object->initialized = TRUE; object->extended_factory = TRUE;"],
 ["extended factory accepts old IID","DirectDrawCreateEx","if (!guid_equal(iid, &IID_IDirectDraw7))","if (FALSE && !guid_equal(iid, &IID_IDirectDraw7))"],
 ["extended factory accepts aggregation","DirectDrawCreateEx","if (outer)","if (FALSE && outer)"],
 ["legacy factory accepts aggregation","DirectDrawCreate","if (outer)","if (FALSE && outer)"],
];
try{for(const[label,name,from,to]of rows){const m=new RegExp("HRESULT WINAPI "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert.equal(body.split(from).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.slice(0,m.index)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_class_factory_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_FACTORY_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/line \d+:/,label+": strict compile and runtime assertion required");assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS factory-entry mutations: "+rows.length+"/"+rows.length+" compiled implementations rejected at runtime");}finally{fs.rmSync(dir,{recursive:true,force:true});}
