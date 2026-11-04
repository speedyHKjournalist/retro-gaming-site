"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-modes-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["count request drops adapter","eight","d3d_get_adapter_mode_count","->backend, adapter","->backend, adapter & 0u"],
 ["enumeration drops index","eight","d3d_enum_adapter_modes","adapter, index, mode","adapter, index & 0u, mode"],
 ["enumeration drops adapter","eight","d3d_enum_adapter_modes","adapter, index, mode","adapter & 0u, index, mode"],
 ["private count uses filtered D3D9 format","nine","d9wg_d3d9_core_adapter_mode_count8","D3DFMT_UNKNOWN","D3DFMT_X8R8G8B8"],
 ["private enumeration uses filtered D3D9 format","nine","d9wg_d3d9_core_enum_adapter_modes8","D3DFMT_UNKNOWN","D3DFMT_X8R8G8B8"],
 ["legacy count profile not selected","nine","d3d_get_adapter_mode_count","compatibility_api == 8","compatibility_api == 7"],
 ["legacy enumeration profile not selected","nine","d3d_enum_adapter_modes","compatibility_api == 8","compatibility_api == 7"],
 ["legacy count leaks to ordinary D3D9","nine","d3d_get_adapter_mode_count","compatibility_api == 8","compatibility_api >= 8"],
 ["legacy enumeration leaks to ordinary D3D9","nine","d3d_enum_adapter_modes","compatibility_api == 8","compatibility_api >= 8"],
 ["legacy count loses last entry","nine","enumerate_adapter_modes8","sizeof(modes) / sizeof(modes[0]);","sizeof(modes) / sizeof(modes[0]) - 1;"],
 ["legacy X1R5G5B5 format lost","nine","enumerate_adapter_modes8","{ 640, 480, D3DFMT_X1R5G5B5 }","{ 640, 480, D3DFMT_A1R5G5B5 }"],
 ["legacy resolution-format ordering changes","nine","enumerate_adapter_modes8","{ 640, 480, D3DFMT_X1R5G5B5 },\n        { 640, 480, D3DFMT_R5G6B5 }","{ 640, 480, D3DFMT_R5G6B5 },\n        { 640, 480, D3DFMT_X1R5G5B5 }"],
 ["out of range index returns success","nine","enumerate_adapter_modes8","if (index >= count) return 0;","if(index>=count){fill_display_mode(mode,640,480,D3DFMT_X8R8G8B8);return 1;}"],
 ["mode copy loses format word","nine","d9wg_d3d9_core_enum_adapter_modes8","CopyMemory(out, &mode, sizeof(mode));","CopyMemory(out, &mode, sizeof(mode) - 4);"],
 ["mode copy overwrites guard","nine","d9wg_d3d9_core_enum_adapter_modes8","if (SUCCEEDED(result)) CopyMemory(out, &mode, sizeof(mode));","if(SUCCEEDED(result)){CopyMemory(out,&mode,sizeof(mode));ZeroMemory((BYTE*)out+sizeof(mode),4);}"],
 ["failed mode request writes output","nine","d9wg_d3d9_core_enum_adapter_modes8","if (SUCCEEDED(result)) CopyMemory(out, &mode, sizeof(mode));","if(SUCCEEDED(result))CopyMemory(out,&mode,sizeof(mode));else ZeroMemory(out,sizeof(mode));"],
 ["ordinary D3D9 end of list reports success","nine","enumerate_adapter_modes","return out ? 0 : matched;","return matched;"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_MODES_SOURCE":"D8WG_MODES_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_modes_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 modes core line/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 modes/core mutations:17/17, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
