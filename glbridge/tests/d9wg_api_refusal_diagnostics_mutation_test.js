"use strict";
// Execute the real constructors and their failure paths after changing only
// diagnostic severity. HRESULTs and allocation/public-state code stay intact.
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"api-refusal-diagnostics-"));
function change(name,from,to){const m=new RegExp("^static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(from),name+": "+from);return source.slice(0,m.index)+body.replace(from,to)+source.slice(end);}
const cases=[
 ["vertex caller refusal logged as failure","device_create_vertex_buffer","HOSTLOG_REFUSED(","HOSTLOG_FAILED(","d8wg_backend_buffers_test.js","D8WG_BACKEND_BUFFERS_SOURCE"],
 ["index caller refusal logged as failure","device_create_index_buffer","HOSTLOG_REFUSED(","HOSTLOG_FAILED(","d8wg_backend_buffers_test.js","D8WG_BACKEND_BUFFERS_SOURCE"],
 ["texture caller refusal logged as failure","create_texture_object","HOSTLOG_REFUSED(","HOSTLOG_FAILED(","d8wg_backend_texture_test.js","D8WG_BACKEND_TEXTURE_SOURCE"],
 ["real texture allocation/transport failure softened","create_texture_object",'HOSTLOG_FAILED("CreateTexture failed:', 'HOSTLOG_REFUSED("CreateTexture failed:',"d8wg_backend_texture_test.js","D8WG_BACKEND_TEXTURE_SOURCE"],
 ["target validation logged as resource failure","device_create_additional_swap_chain","if (hr == D3DERR_INVALIDCALL)","if (FALSE && hr == D3DERR_INVALIDCALL)","d8wg_backend_additional_test.js","D8WG_BACKEND_ADDITIONAL_SOURCE"],
 ["real target failure softened","device_create_additional_swap_chain","HOSTLOG_FAILED(","HOSTLOG_REFUSED(","d8wg_backend_additional_test.js","D8WG_BACKEND_ADDITIONAL_SOURCE"],
];
try{for(const[label,name,from,to,test,env]of cases){const file=directory+"/candidate.c";fs.writeFileSync(file,change(name,from,to));const result=spawnSync(process.execPath,[__dirname+"/"+test],{encoding:"utf8",timeout:180000,env:{...process.env,[env]:file}});assert(!result.error,label+": "+result.error);const output=result.stdout+result.stderr;assert.notEqual(result.status,0,label+" survived");assert(!/error:|undefined reference|Undefined symbols/.test(output),label+" failed compilation: "+output);assert(/case \d+ line|Backend8 additional parameters/.test(output),label+" must fail executed diagnostic checks: "+output);console.log("KILL "+label);}console.log("PASS API diagnostic severity mutations: "+cases.length+"/"+cases.length+" real C constructors; expected warnings retained and real failures stay errors");}finally{fs.rmSync(directory,{recursive:true,force:true});}
