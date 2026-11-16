"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'shader8-pair-mut-')),core=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),defs=fs.readFileSync(root+'/runtime/d9wg_shader8_local_defs.h','utf8');
const plan=[
 ['missing vertex input accepted','shader8_inputs_declared','if (reg >= 32u || !(declared & (1u << reg))) return FALSE;','if (reg >= 32u || declared == 0xffffffffu) return FALSE;'],
 ['pair profile','d9wg_d3d9_core_vertex_shader8','device_compatibility_api(device_from_iface(iface)) != 8u','device_compatibility_api(device_from_iface(iface)) != 9u'],
 ['layout failure output','d9wg_d3d9_core_vertex_shader8','if (layout_out) *layout_out = NULL;','/* layout output not cleared */'],
 ['shader failure output','d9wg_d3d9_core_vertex_shader8','if (shader_out) *shader_out = NULL;','/* shader output not cleared */'],
 ['VS2 version','d9wg_d3d9_core_vertex_shader8','((function[0] >> 8) & 0xffu) != 1u','((function[0] >> 8) & 0xffu) > 2u'],
 ['wrong shader kind','d9wg_d3d9_core_vertex_shader8','(function[0] >> 16) != 0xfffeu\n            || ',''],
 ['original bound','d9wg_d3d9_core_vertex_shader8','shader8_validate_body(function + 1, D9WG_D3D8_SHADER_TOKENS,','shader8_validate_body(function + 1, D9WG_D3D8_SHADER_TOKENS + 1,'],
 ['expanded tail END','d9wg_d3d9_core_vertex_shader8','d9wg_shader8_build_local_defs(expanded, function, body + 2u,','d9wg_shader8_build_local_defs(expanded, function, body + 1u,'],
 ['revalidate trusted expansion','d9wg_d3d9_core_vertex_shader8','hr = create_shader_object(device_from_iface(iface), expanded,\n                count, FALSE, &shader);','hr = create_shader(device_from_iface(iface), expanded, FALSE, &shader);'],
 ['expanded storage count','d9wg_d3d9_core_vertex_shader8','vectors * 6u','vectors * 5u'],
 ['c95 omitted','d9wg_d3d9_core_vertex_shader8','constants, mask, 96);','constants, mask, 95);'],
 ['FFP primary normal','d9wg_d3d9_core_vertex_shader8','function == NULL, &layout','FALSE, &layout'],
 ['expanded allocation rollback','d9wg_d3d9_core_vertex_shader8','IDirect3DVertexDeclaration9_Release(layout);\n            return E_OUTOFMEMORY;','/* leaked layout */\n            return E_OUTOFMEMORY;'],
 ['shader submission rollback','d9wg_d3d9_core_vertex_shader8','IDirect3DVertexDeclaration9_Release(layout);\n            return hr;','/* leaked layout */\n            return hr;'],
 ['shader public kind','d9wg_d3d9_core_vertex_shader8','count, FALSE, &shader);','count, TRUE, &shader);'],
 ['pixel profile','d9wg_d3d9_core_pixel_shader8','device_compatibility_api(device_from_iface(iface)) != 8u','device_compatibility_api(device_from_iface(iface)) != 9u'],
 ['pixel failure output','d9wg_d3d9_core_pixel_shader8','*out = NULL;','/* pixel output not cleared */'],
 ['parent pin','create_shader_object','device_child_add_ref(device);','/* missing child pin */'],
 ['resource publication','create_shader_object','device->shaders = shader;','device->shaders = NULL;'],
 ['DEF opcode','defs','uint32_t opcode = 0x00000051u;','uint32_t opcode = 0x00000000u;'],
 ['DEF target','defs','0xa00f0000u | reg','0xa00f0000u | (reg + 1u)'],
 ['DEF constant offset','defs','(const uint8_t *)const_data + reg * 16u','(const uint8_t *)const_data'],
 ['version copied into body','defs','(const uint8_t *)original + 4','(const uint8_t *)original'],
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const shared=fn==='defs',original=shared?defs:core;let candidate;if(shared){assert(original.includes(from),name);candidate=original.replace(from,to);}else{const[a,b]=span(original,fn),body=original.slice(a,b);assert(body.includes(from),name);candidate=original.slice(0,a)+body.replace(from,to)+original.slice(b);}const file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_shader_pair_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,[shared?'D8WG_LOCAL_DEFS_HEADER':'D8WG_SHADER_PAIR_CORE_SOURCE']:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compiler failure: '+output);assert(/Shader8 pair case \d+ line \d+:|Assertion failed: \([^\n]*\), function \w+, file create\.c, line \d+\.|create\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(output),name+' must fail actual C assertion: '+output);killed++;}console.log('PASS shader8 pair mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C semantic/guard assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
