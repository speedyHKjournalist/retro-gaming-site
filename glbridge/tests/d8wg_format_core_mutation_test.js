"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-format-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["drops adapter","eight","d3d_check_device_format","->backend, adapter,","->backend, adapter & 0u,"],
 ["drops device type","eight","d3d_check_device_format","type, adapter_format, usage","type & 0u, adapter_format, usage"],
 ["drops adapter format","eight","d3d_check_device_format","type, adapter_format, usage","type, adapter_format & 0u, usage"],
 ["drops usage","eight","d3d_check_device_format","adapter_format, usage, resource_type","adapter_format, usage & 0u, resource_type"],
 ["drops resource type","eight","d3d_check_device_format","usage, resource_type, format","usage, resource_type & 0u, format"],
 ["drops queried format","eight","d3d_check_device_format","resource_type, format","resource_type, format & 0u"],
 ["legacy query profile not selected","nine","d3d_check_device_format","compatibility_api == 8","compatibility_api == 7"],
 ["legacy query rules leak to ordinary9","nine","d3d_check_device_format","compatibility_api == 8","compatibility_api >= 8"],
 ["D3D9 layout formats leak to8","nine","supported_format8","d9wg_format_layout(8,","d9wg_format_layout(9,"],
 ["packed image surfaces accepted","nine","supported_format8","(!image_surface || (width == 1 && height == 1))","(!image_surface || (width > 0 && height > 0))"],
 ["image check applied to sampled textures","nine","supported_format8","!image_surface ||","(image_surface && FALSE) ||"],
 ["compressed volume texture accepted","nine","d3d_check_device_format8","&& format != D3DFMT_DXT1","&& TRUE"],
 ["cube render-target usage broadened","nine","d3d_check_device_format8","&& usage == D3DUSAGE_RENDERTARGET","&& (usage & D3DUSAGE_RENDERTARGET)"],
 ["depth usage mask changed to equality","nine","d3d_check_device_format8","&& (usage & D3DUSAGE_DEPTHSTENCIL)","&& (usage == D3DUSAGE_DEPTHSTENCIL)"],
 ["D3D9 render formats leak to8","nine","d3d_check_device_format8","supported_render_target_format8(format)","supported_render_target_format(format)"],
 ["D3D9 depth formats leak to8","nine","d3d_check_device_format8","supported_depth_stencil_format8(format)","supported_depth_stencil_format(format)"],
 ["ordinary9 bypasses host capability query","nine","d3d_check_device_format","host_features = runtime_host_features();","host_features = D9WG_HOST_FEATURE_MASK;"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_FORMAT_SOURCE":"D8WG_FORMAT_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_format_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 format core line/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 format/core mutations:17/17, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
