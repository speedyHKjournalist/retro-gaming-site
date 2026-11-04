"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-checks-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["type query drops adapter","eight","d3d_check_device_type","->backend, adapter,","->backend, adapter & 0u,"],
 ["type query drops windowed","eight","d3d_check_device_type","backbuffer_format, windowed","backbuffer_format, windowed & 0"],
 ["type query drops display format","eight","d3d_check_device_type","type, display_format, backbuffer_format","type, display_format & 0u, backbuffer_format"],
 ["depth query drops render format","eight","d3d_check_depth_stencil","adapter_format, render_format, depth_format","adapter_format, render_format & 0u, depth_format"],
 ["depth query drops depth format","eight","d3d_check_depth_stencil","render_format, depth_format","render_format, depth_format & 0u"],
 ["MSAA query drops format","eight","d3d_check_multisample","type, format, windowed","type, format & 0u, windowed"],
 ["MSAA query drops sample count","eight","d3d_check_multisample","windowed, multisample","windowed, multisample & 0u"],
 ["D3D8 type restrictions not selected","nine","d3d_check_device_type","compatibility_api == 8","compatibility_api == 7"],
 ["D3D8 type restrictions leak to ordinary9","nine","d3d_check_device_type","compatibility_api == 8","compatibility_api >= 8"],
 ["D3D8 render predicate ignores common capability","nine","supported_render_target_format8","&& supported_render_target_format(format)","&& TRUE"],
 ["D3D8 depth predicate ignores common capability","nine","supported_depth_stencil_format8","&& supported_depth_stencil_format(format)","&& TRUE"],
 ["D3D9-only depth formats leak to8","nine","supported_depth_stencil_format8","&& supported_depth_stencil_format(format)","|| supported_depth_stencil_format(format)"],
 ["D3D8 depth restrictions not selected","nine","d3d_check_depth_stencil","compatibility_api == 8","compatibility_api == 7"],
 ["D3D8 MSAA receives D3D9 format checks","nine","d3d_check_multisample","compatibility_api == 8","compatibility_api == 7"],
 ["D3D8 MSAA rules leak to ordinary9","nine","d3d_check_multisample","compatibility_api == 8","compatibility_api >= 8"],
 ["private MSAA adds quality output","nine","d9wg_d3d9_core_check_multisample8","NULL);","&(DWORD){0});"],
 ["ordinary9 failed MSAA reports one quality","nine","d3d_check_multisample","*quality_levels = ok ? 1u : 0u;","*quality_levels = 1u;"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_CHECKS_SOURCE":"D8WG_CHECKS_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_checks_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 checks core line/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 checks/core mutations:17/17, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
