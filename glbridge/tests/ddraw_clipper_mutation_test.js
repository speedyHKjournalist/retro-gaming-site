"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-clipper-mutants-"));
const cases=[
 ["Blt missing-list error lost","if (!clipper->clip_list) return DDERR_NOCLIPLIST;","if (!clipper->clip_list) return DDERR_GENERIC;"],
 ["Blt custom list count lost","*count = total;","*count = 0;"],
 ["Blt destroyed window succeeds","if (!GetClientRect(clipper->window, &client)) return E_FAIL;","if (!GetClientRect(clipper->window, &client)) return DD_OK;"],
 ["HWND clip list uses client coordinates","MapWindowPoints(clipper->window, NULL, (POINT *)&client, 2);","if (FALSE) MapWindowPoints(clipper->window, NULL, (POINT *)&client, 2);"],
 ["destroyed HWND has a region","if (!IsWindow(clipper->window)) return DDWG_TRACE_ERROR(E_FAIL);","if (FALSE && !IsWindow(clipper->window)) return DDWG_TRACE_ERROR(E_FAIL);"],
 ["HWND binding loses custom region","clipper->window = window;\n    clipper->changed = TRUE;","heap_free(clipper->clip_list);clipper->clip_list = NULL;\n    clipper->window = window;\n    clipper->changed = TRUE;"],
 ["corrupt vtable Release decrements","if (!clipper || clipper->vtbl != &g_clipper_vtbl) return 0;","if (!clipper) return 0;"],
];
try{for(const [label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[path.join(__dirname,"ddraw_clipper_test.js")],{env:{...process.env,DDRAW_CLIPPER_SOURCE:file},encoding:"utf8"});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/DDraw clipper line/,"must compile then fail a runtime assertion: "+label+"\n"+r.stderr);console.log("KILL "+label);}console.log("PASS Clipper mutations: "+cases.length+"/"+cases.length+", compilation and runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
