"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-modes-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
 ["vertex version limit","eight","ValidateVertexShader","shader[0] <= caps->VertexShaderVersion","shader[0] >= caps->VertexShaderVersion"],
 ["pixel version limit","eight","ValidatePixelShader","shader[0] <= caps->PixelShaderVersion","shader[0] >= caps->PixelShaderVersion"],
 ["pixel NULL output preservation","eight","ValidatePixelShader","if (!shader) return D8WG_TRACE_ERROR(E_FAIL);","if (!shader) { if(errors)*errors=NULL; return E_FAIL; }"],
 ["muted error string","eight","shader_validation_result","FAILED(result) && return_error","FAILED(result) || return_error"],
 ["allocation failure status","eight","shader_validation_result","if (!*errors) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);","if (!*errors) return S_OK;"],
 ["caps constant bound","nine","d9wg_d3d9_core_validate_shader_export8","reg > max_constants - span","reg > max_constants"],
 ["missing declared input","nine","d9wg_d3d9_core_validate_shader_export8","inputs = 0;","inputs = 0xffffffffu;"],
 ["matrix constant span","nine","d9wg_d3d9_core_validate_shader_export8","span = 4;","span = 1;"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_VALIDATION_EXPORTS_SOURCE":"D8WG_VALIDATION_EXPORTS_CORE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_validation_exports_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/line [0-9]+:/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 validation exports mutations:8/8, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
