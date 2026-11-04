"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-pp-core-mut-"));
function mutate(before,after){const name="d9wg_d3d9_core_parameters_effective8",m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const call="d9wg_present_parameters_effective(&effective, &requested,\n            width, height, format, window);",cases=[
 ["input prefix shifted","CopyMemory(&requested, requested8, 20);","CopyMemory(&requested, (const BYTE*)requested8+4, 20);"],
 ["quality not zero","requested.MultiSampleQuality = 0;","requested.MultiSampleQuality = 1;"],
 ["input tail offset shifted","(const BYTE *)requested8 + 20","(const BYTE *)requested8 + 24"],
 ["output prefix loses sample word","CopyMemory(out8, &effective, 20);","CopyMemory(out8, &effective, 16);"],
 ["output contains quality slot","(const BYTE *)&effective + 24","(const BYTE *)&effective + 20"],
 ["output interval not copied","CopyMemory((BYTE *)out8 + 20, (const BYTE *)&effective + 24,\n            D9WG_D3D8_PARAMETERS_BYTES - 20);","CopyMemory((BYTE *)out8 + 20, (const BYTE *)&effective + 24,\n            D9WG_D3D8_PARAMETERS_BYTES - 24);"],
 ["output overruns52 bytes","CopyMemory(out8, &effective, 20);","CopyMemory(out8, &effective, 20);ZeroMemory((BYTE*)out8+52,4);"],
 ["bypasses canonical normalizer",call,"effective=requested;(void)width;(void)height;(void)format;(void)window;"],
 ["drops width","width, height, format, window);","width & 0u, height, format, window);"],
 ["drops height","width, height, format, window);","width, height & 0u, format, window);"],
 ["drops format","width, height, format, window);","width, height, format & 0u, window);"],
 ["drops window","width, height, format, window);","width, height, format, (HWND)((uintptr_t)window & 0u));"],
 ["legacy copy-vsync value rewritten","CopyMemory(out8, &effective, 20);","if((UINT)effective.SwapEffect==4)effective.SwapEffect=(D3DSWAPEFFECT)3;CopyMemory(out8, &effective, 20);"],
 ["legacy interval lost","CopyMemory(out8, &effective, 20);","effective.PresentationInterval=0;CopyMemory(out8, &effective, 20);"],
 ["input overwritten before staging","CopyMemory(&requested, requested8, 20);","if(out8==requested8)ZeroMemory(out8,52);CopyMemory(&requested, requested8, 20);"],
];
try{for(const[label,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(before,after));const r=spawnSync(process.execPath,[__dirname+"/d8wg_parameters_core_test.js"],{env:{...process.env,D8WG_PARAMETERS_BACKEND_SOURCE:file},encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/PP8 core line|Assertion.*failed|Assertion failed/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log(`PASS PP8/core mutations:${cases.length}/${cases.length}, strict compilation then runtime assertions`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
