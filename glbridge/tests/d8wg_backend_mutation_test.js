"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-pp-core-mut-"));
function mutate(before,after,name="d9wg_d3d9_core_parameters_effective8"){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["API8 silently uses9 profile","api = d3d->compatibility_api == 8 ? 8u : 9u;","api = 9u;","d3d_create_device","constructor"],
 ["API9 silently uses8 profile","api = d3d->compatibility_api == 8 ? 8u : 9u;","api = 8u;","d3d_create_device","constructor"],
 ["API8 gets9 behavior validation","if (api == 9) {","if (1) {","d3d_create_device","constructor"],
 ["API8 gets9 backbuffer predicate","api == 8 ? supported_render_target_format8(parameters->BackBufferFormat)","api == 8 ? supported_backbuffer_format(parameters->BackBufferFormat)","d3d_create_device","constructor"],
 ["API8 accepts9 lockable depth","api == 8 ? supported_depth_stencil_format8(parameters->AutoDepthStencilFormat)","api == 8 ? supported_depth_stencil_format(parameters->AutoDepthStencilFormat)","d3d_create_device","constructor"],
 ["render defaults lose API8","d9wg_render_default(api, state,","d9wg_render_default(9, state,","device_init_states_for_api","defaults"],
 ["material defaults lose API8","d9wg_object_defaults(api,","d9wg_object_defaults(9,","device_init_states_for_api","defaults"],
 ["texture defaults lose API8","d9wg_texture_default(api, stage, state)","d9wg_texture_default(9, stage, state)","device_init_states_for_api","defaults"],
 ["Reset wrapper changes9 defaults","device_init_states_for_api(device, device_compatibility_api(device));","device_init_states_for_api(device, 8);","device_init_states","defaults"],
 ["bridge accepts ordinary9 factory","d3d_from_iface(factory)->compatibility_api != 8","d3d_from_iface(factory)->compatibility_api == 0","d9wg_d3d9_core_create_device8","bridge"],
 ["bridge drops adapter","factory, adapter, (D3DDEVTYPE)type,","factory, adapter & 0u, (D3DDEVTYPE)type,","d9wg_d3d9_core_create_device8","bridge"],
 ["bridge drops device type","(D3DDEVTYPE)type,","(D3DDEVTYPE)(type & 0u),","d9wg_d3d9_core_create_device8","bridge"],
 ["bridge drops behavior","focus, behavior, &parameters, out","focus, behavior & 0u, &parameters, out","d9wg_d3d9_core_create_device8","bridge"],
 ["bridge drops focus","focus, behavior, &parameters, out","(HWND)((uintptr_t)focus & 0u), behavior, &parameters, out","d9wg_d3d9_core_create_device8","bridge"],
 ["bridge omits failure output clear","*out = NULL;","if(factory&&parameters8)*out = NULL;","d9wg_d3d9_core_create_device8","bridge"],
];
try{for(const[label,before,after,name,test]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(before,after,name));const r=spawnSync(process.execPath,[__dirname+"/d8wg_backend_"+test+"_test.js"],{env:{...process.env,D8WG_BACKEND_SOURCE:file},encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/Backend8 (constructor|defaults|bridge) line|Assertion.*failed|Assertion failed/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log(`PASS backend8 mutations:${cases.length}/${cases.length}, strict compilation then runtime assertions`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
