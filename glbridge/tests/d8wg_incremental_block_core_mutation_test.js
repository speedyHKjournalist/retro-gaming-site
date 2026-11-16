"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'incremental8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "Begin wrong profile",
    "d9wg_d3d9_core_begin_state_block8",
    "!= 8u",
    "!= 9u"
  ],
  [
    "Begin null reports success",
    "d9wg_d3d9_core_begin_state_block8",
    "    if (!iface ||",
    "    if (!iface) return D3D_OK;\n    if (!iface ||"
  ],
  [
    "Begin teardown allowed",
    "d9wg_d3d9_core_begin_state_block8",
    "|| device_from_iface(iface)->releasing_owned_refs",
    "|| FALSE"
  ],
  [
    "Begin omitted",
    "d9wg_d3d9_core_begin_state_block8",
    "return IDirect3DDevice9_BeginStateBlock(iface);",
    "return D3D_OK;"
  ],
  [
    "Begin existing recording cancelled",
    "d9wg_d3d9_core_begin_state_block8",
    "    return IDirect3DDevice9_BeginStateBlock(iface);",
    "    device_cancel_recording(device_from_iface(iface));\n    return IDirect3DDevice9_BeginStateBlock(iface);"
  ],
  [
    "Begin OOM normalized",
    "d9wg_d3d9_core_begin_state_block8",
    "return IDirect3DDevice9_BeginStateBlock(iface);",
    "HRESULT hr=IDirect3DDevice9_BeginStateBlock(iface);return FAILED(hr)?D3DERR_INVALIDCALL:hr;"
  ],
  [
    "End null output reports success",
    "d9wg_d3d9_core_end_state_block_handle8",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return D3D_OK;"
  ],
  [
    "End invalid output cancels recording",
    "d9wg_d3d9_core_end_state_block_handle8",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) {if(iface)device_cancel_recording(device_from_iface(iface));return D3DERR_INVALIDCALL;}"
  ],
  [
    "End output not cleared",
    "d9wg_d3d9_core_end_state_block_handle8",
    "    *out = 0;",
    "    /* stale output */"
  ],
  [
    "End wrong profile",
    "d9wg_d3d9_core_end_state_block_handle8",
    "!= 8u",
    "!= 9u"
  ],
  [
    "End teardown allowed",
    "d9wg_d3d9_core_end_state_block_handle8",
    "|| device_from_iface(iface)->releasing_owned_refs",
    "|| FALSE"
  ],
  [
    "End output lost",
    "d9wg_d3d9_core_end_state_block_handle8",
    "        *out = state_block_publish_handle8(state_block_from_iface(created));",
    "        *out = 0;"
  ],
  [
    "End allocation token changed",
    "d9wg_d3d9_core_end_state_block_handle8",
    "        *out = state_block_publish_handle8(state_block_from_iface(created));",
    "        *out = state_block_publish_handle8(state_block_from_iface(created)) + 1;"
  ],
  [
    "End failure swallowed",
    "d9wg_d3d9_core_end_state_block_handle8",
    "    return hr;",
    "    return D3D_OK;"
  ],
  [
    "End guard leaked",
    "d9wg_d3d9_core_end_state_block_handle8",
    "    IDirect3DDevice9_Release(iface);",
    "    /* guard leaked */"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_incremental_block_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_INCREMENTAL_BLOCK_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 incremental state-block mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
