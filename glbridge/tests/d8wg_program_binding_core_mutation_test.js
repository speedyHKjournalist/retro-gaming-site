"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'program8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
 ["profile", "d9wg_d3d9_core_set_vertex_shader8", "device_compatibility_api(device_from_iface(iface)) != 8u", "device_compatibility_api(device_from_iface(iface)) != 9u"],
 ["absent declaration accepted", "d9wg_d3d9_core_set_vertex_shader8", "return shader_iface ? D3DERR_INVALIDCALL", "return shader_iface ? D3D_OK"],
 ["zero no-op", "d9wg_d3d9_core_set_vertex_shader8", ": d9wg_d3d9_core_set_fvf8(iface, 0);", ": D3D_OK;"],
 ["zero ordinary9", "d9wg_d3d9_core_set_vertex_shader8", ": d9wg_d3d9_core_set_fvf8(iface, 0);", ": IDirect3DDevice9_SetFVF(iface, 0);"],
 ["layout guard polarity", "d9wg_d3d9_core_set_vertex_shader8", "layout_iface->lpVtbl != &g_decl_vtbl", "layout_iface->lpVtbl == &g_decl_vtbl"],
 ["layout owner polarity", "d9wg_d3d9_core_set_vertex_shader8", "layout->device != device", "layout->device == device"],
 ["program owner unchecked", "d9wg_d3d9_core_set_vertex_shader8", "|| shader->device != device", ""],
 ["program type unchecked", "d9wg_d3d9_core_set_vertex_shader8", "shader_iface->lpVtbl != &g_vertex_shader_vtbl", "FALSE"],
 ["same recording omitted", "d9wg_d3d9_core_set_vertex_shader8", "!device->recording_result && device->vertex_declaration", "device->vertex_declaration"],
 ["layout ignored in duplicate", "d9wg_d3d9_core_set_vertex_shader8", "device->vertex_declaration == layout", "TRUE"],
 ["program ignored in duplicate", "d9wg_d3d9_core_set_vertex_shader8", "&& device->vertex_shader == shader", ""],
 ["no duplicate fast path", "d9wg_d3d9_core_set_vertex_shader8", "!device->recording_result && device->vertex_declaration", "FALSE && !device->recording_result && device->vertex_declaration"],
 ["layout setter missing", "d9wg_d3d9_core_set_vertex_shader8", "HRESULT hr = IDirect3DDevice9_SetVertexDeclaration(iface, layout_iface);", "HRESULT hr = D3D_OK;"],
 ["program setter missing", "d9wg_d3d9_core_set_vertex_shader8", "return IDirect3DDevice9_SetVertexShader(iface, shader_iface);", "return D3D_OK;"],
 ["layout failure ignored", "d9wg_d3d9_core_set_vertex_shader8", "if (FAILED(hr)) return hr;", "(void)hr;"],
 ["program failure swallowed", "d9wg_d3d9_core_set_vertex_shader8", "return IDirect3DDevice9_SetVertexShader(iface, shader_iface);", "IDirect3DDevice9_SetVertexShader(iface, shader_iface); return D3D_OK;"],
 ["always unbind program", "d9wg_d3d9_core_set_vertex_shader8", "IDirect3DDevice9_SetVertexShader(iface, shader_iface)", "IDirect3DDevice9_SetVertexShader(iface, NULL)"],
 ["reverse submission order", "d9wg_d3d9_core_set_vertex_shader8", "HRESULT hr = IDirect3DDevice9_SetVertexDeclaration(iface, layout_iface);\n    if (FAILED(hr)) return hr;\n    return IDirect3DDevice9_SetVertexShader(iface, shader_iface);", "HRESULT hr = IDirect3DDevice9_SetVertexShader(iface, shader_iface);\n    if (FAILED(hr)) return hr;\n    return IDirect3DDevice9_SetVertexDeclaration(iface, layout_iface);"],
 ["declaration-only rejected", "d9wg_d3d9_core_set_vertex_shader8", "D9Device *device = device_from_iface(iface);", "if (!shader_iface) return D3DERR_INVALIDCALL; D9Device *device = device_from_iface(iface);"],
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_program_binding_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_PROGRAM_BINDING_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 program binding mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
