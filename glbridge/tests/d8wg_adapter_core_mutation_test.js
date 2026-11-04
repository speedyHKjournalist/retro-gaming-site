"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-adapter-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["legacy identity request drops WHQL flag","eight","d3d_get_adapter_identifier","adapter, flags, identifier","adapter, flags & 0u, identifier"],
 ["legacy identity request drops adapter","eight","d3d_get_adapter_identifier","adapter, flags, identifier","adapter & 0u, flags, identifier"],
 ["legacy identity size is SDK9 size","eight","d3d_get_adapter_identifier","sizeof(*identifier)","1100"],
 ["identifier copy keeps SDK9 DeviceName","nine","d9wg_d3d9_core_adapter_identifier8","&identifier.DriverVersion","(BYTE *)&identifier + 1024"],
 ["identifier tail copy loses WHQL word","nine","d9wg_d3d9_core_adapter_identifier8","D9WG_D3D8_IDENTIFIER_BYTES - 1024","D9WG_D3D8_IDENTIFIER_BYTES - 1028"],
 ["identifier tail copy overwrites output","nine","d9wg_d3d9_core_adapter_identifier8","D9WG_D3D8_IDENTIFIER_BYTES - 1024","D9WG_D3D8_IDENTIFIER_BYTES - 1020"],
 ["identifier error writes output","nine","d9wg_d3d9_core_adapter_identifier8","if (FAILED(result)) return result;","if (FAILED(result)) { ZeroMemory(out,bytes);return result;}"],
 ["private identity accepts wrong output size","nine","d9wg_d3d9_core_adapter_identifier8","!out || bytes != D9WG_D3D8_IDENTIFIER_BYTES","!out || (bytes & 0u)"],
 ["D3D8 profile does not select legacy identity","nine","d3d_get_adapter_identifier","compatibility_api == 8","compatibility_api == 7"],
 ["D3D8 profile leaks into normal D3D9","nine","d3d_get_adapter_identifier","compatibility_api == 8","compatibility_api >= 8"],
 ["WHQL polarity reversed","nine","fill_adapter_identifier8","? 0 : 1","? 1 : 0"],
 ["legacy GUID takes D3D9 prefix","nine","fill_adapter_identifier8","0xD8E60000u","0xD9E60000u"],
 ["software callback is discarded","eight","d3d_register_software_device","->backend, initialize","->backend, (void *)((uintptr_t)initialize & 0u)"],
 ["software registration bypasses core","nine","d9wg_d3d9_core_register_software","return IDirect3D9_RegisterSoftwareDevice(factory, initialize);","(void)factory;(void)initialize;return D3DERR_INVALIDCALL;"],
 ["display mode drops adapter","eight","d3d_get_adapter_display_mode","->backend, adapter, mode","->backend, adapter & 0u, mode"],
 ["display mode copy drops format","nine","d9wg_d3d9_core_adapter_display_mode","CopyMemory(out, &mode, sizeof(mode));","CopyMemory(out, &mode, sizeof(mode) - 4);"],
 ["display mode error writes output","nine","d9wg_d3d9_core_adapter_display_mode","if (SUCCEEDED(result)) CopyMemory(out, &mode, sizeof(mode));","if (SUCCEEDED(result)) CopyMemory(out,&mode,sizeof(mode));else ZeroMemory(out,sizeof(mode));"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_ADAPTER_SOURCE":"D8WG_ADAPTER_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_adapter_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 adapter core line/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 adapter/core mutations:17/17, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
