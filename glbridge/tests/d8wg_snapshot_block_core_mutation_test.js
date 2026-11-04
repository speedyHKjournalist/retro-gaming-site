"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'snapshot8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
 ["null output reports success", "d9wg_d3d9_core_record_state_block8", "if (!out) return D3DERR_INVALIDCALL;", "if (!out) return D3D_OK;"],
 ["output not cleared", "d9wg_d3d9_core_record_state_block8", "    *out = NULL;", "    /* stale output */"],
 ["profile", "d9wg_d3d9_core_record_state_block8", "device_compatibility_api(device_from_iface(iface)) != 8u", "device_compatibility_api(device_from_iface(iface)) != 9u"],
 ["recorder guard polarity", "d9wg_d3d9_core_record_state_block8", "if (!iface || !recorder", "if (!iface || recorder"],
 ["Begin error normalized", "d9wg_d3d9_core_record_state_block8", "if (FAILED(hr)) return hr;", "if (FAILED(hr)) return D3DERR_INVALIDCALL;"],
 ["existing recording cancelled", "d9wg_d3d9_core_record_state_block8", "if (FAILED(hr)) return hr;", "if (FAILED(hr)) {device_cancel_recording(device_from_iface(iface));return hr;}"],
 ["callback omitted", "d9wg_d3d9_core_record_state_block8", "hr = recorder(iface, snapshot);", "hr = D3D_OK; (void)snapshot;"],
 ["context lost", "d9wg_d3d9_core_record_state_block8", "hr = recorder(iface, snapshot);", "hr = recorder(iface, NULL); (void)snapshot;"],
 ["callback device lost", "d9wg_d3d9_core_record_state_block8", "hr = recorder(iface, snapshot);", "hr = recorder(NULL, snapshot);"],
 ["callback error publishes block", "d9wg_d3d9_core_record_state_block8", "if (SUCCEEDED(hr)) hr = IDirect3DDevice9_EndStateBlock(iface, out);", "hr = IDirect3DDevice9_EndStateBlock(iface, out);"],
 ["End omitted", "d9wg_d3d9_core_record_state_block8", "if (SUCCEEDED(hr)) hr = IDirect3DDevice9_EndStateBlock(iface, out);", "if (SUCCEEDED(hr)) hr = D3D_OK;"],
 ["abort omitted", "d9wg_d3d9_core_record_state_block8", "if (FAILED(hr)) device_cancel_recording(device_from_iface(iface));", "/* pending block leaked */"],
 ["callback error status swallowed", "d9wg_d3d9_core_record_state_block8", "if (FAILED(hr)) device_cancel_recording(device_from_iface(iface));\n    return hr;", "if (FAILED(hr)) device_cancel_recording(device_from_iface(iface));\n    return D3D_OK;"],
 ["S_FALSE never publishes", "d9wg_d3d9_core_record_state_block8", "if (SUCCEEDED(hr)) hr =", "if (hr == D3D_OK) hr ="],
 ["callback replayed", "d9wg_d3d9_core_record_state_block8", "hr = recorder(iface, snapshot);", "hr = recorder(iface, snapshot); if(SUCCEEDED(hr))hr=recorder(iface,snapshot);"],
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_snapshot_block_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_SNAPSHOT_BLOCK_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 snapshot block mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
