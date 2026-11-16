"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'fvf8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
 ['profile','d9wg_d3d9_core_set_fvf8','device_compatibility_api(device_from_iface(iface)) != 8u','device_compatibility_api(device_from_iface(iface)) != 9u'],
 ['dialect9 decoder','d9wg_d3d9_core_set_fvf8','d9wg_fvf_decode(D9WG_FVF_D3D8','d9wg_fvf_decode(D9WG_FVF_D3D9'],
 ['program remains bound','d9wg_d3d9_core_set_fvf8','HRESULT hr = IDirect3DDevice9_SetVertexShader(iface, NULL);','HRESULT hr = D3D_OK;'],
 ['zero no-op','d9wg_d3d9_core_set_fvf8','D9Device *device = device_from_iface(iface);','D9Device *device = device_from_iface(iface); if (!fvf) return D3D_OK;'],
 ['recorded duplicate dropped','d9wg_d3d9_core_set_fvf8','!device->recording_result && !device->vertex_shader','!device->vertex_shader'],
 ['wrong live duplicate','d9wg_d3d9_core_set_fvf8','&& device->vertex_declaration == decl',''],
 ['invalid layout accepted','d9wg_d3d9_core_set_fvf8','return D3DERR_INVALIDCALL;\n        HRESULT hr = fvf8_declaration','return D3D_OK;\n        HRESULT hr = fvf8_declaration'],
 ['cache key lost','fvf8_declaration','decl->legacy_fvf8 = fvf;','decl->legacy_fvf8 = 0;'],
 ['cross-cache alias','fvf8_declaration','if (decl->legacy_fvf8 != fvf) continue;','if (0) continue;'],
 ['cache key scrubbed','fvf8_declaration','decl->legacy_fvf8 = fvf;','decl->legacy_fvf8 = fvf & ~0x4000u;'],
 ['API9 association leaks8 key','fvf8_declaration','decl->fvf = 0;','decl->fvf = fvf;'],
 ['ordinary9 cache collision','fvf8_declaration','decl->fvf = 0;','decl->fvf = 0; decl->fvf_converted = TRUE;'],
 ['cache fails to reacquire','fvf8_declaration','decl_binding_add_ref(decl);\n            decl->fvf_cache_ref','/* missing private hold */\n            decl->fvf_cache_ref'],
 ['cache retains public pin','fvf8_declaration','IDirect3DVertexDeclaration9_Release(iface);','/* public reference leaked */'],
 ['cache holds public ref','fvf8_declaration','decl_binding_add_ref(decl);\n    decl->fvf_cache_ref','IDirect3DVertexDeclaration9_AddRef(iface);\n    decl->fvf_cache_ref'],
 ['cache flag absent','fvf8_declaration','decl->fvf_cache_ref = TRUE;\n    /* Cache','decl->fvf_cache_ref = FALSE;\n    /* Cache'],
 ['offset conversion','fvf8_declaration','e->stream, e->offset, e->type','e->stream, 0, e->type'],
 ['usage conversion','fvf8_declaration','e->method, e->usage, e->usage_index','e->method, e->usage_index, e->usage'],
 ['SDK9 terminator','fvf8_declaration','elements[count] = (D3DVERTEXELEMENT9)D3DDECL_END();','elements[count] = (D3DVERTEXELEMENT9){0,0,17,0,0,0};'],
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_fvf_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_FVF8_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 FVF mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
