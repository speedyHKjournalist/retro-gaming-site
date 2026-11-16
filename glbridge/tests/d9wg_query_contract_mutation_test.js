'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.resolve(__dirname,'../d3d9proxy/d3d9_proxy.c'),'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-query-mutants-'));
const cases=[
 ['initial result falsely pending','value = 0xddddddddddddddddULL;','return S_FALSE;value = 0xddddddddddddddddULL;'],
 ['marker BEGIN rejected','return D3D_OK;\n    if (begin) {','return D3DERR_INVALIDCALL;\n    if (begin) {'],
 ['repeated BEGIN rejected','/* A second BEGIN discards the previous interval and starts anew. */','if (query->begun) return D3DERR_INVALIDCALL;'],
 ['END without BEGIN rejected','if (!query->begun) {','if (!query->begun) return D3DERR_INVALIDCALL;\n    if (!query->begun) {'],
 ['partial reads overwrite guard','size < sizeof(value) ? size : sizeof(value)','sizeof(value)'],
 ['completed partial reads overwrite guard','CopyMemory(data, &value, size < sizeof(value) ? size : sizeof(value));\n    return S_OK;','CopyMemory(data, &value, sizeof(value));\n    return S_OK;'],
 ['open interval reports completion','if (query->begun) return S_FALSE;','if (query->begun) return S_OK;'],
 ['stale generation accepted','response->request_id != query->request_id','FALSE'],
 ['failed response exposes stale bytes','value = query_fallback_value(query);\n    } else {','value = response->value_low;\n    } else {'],
 ['high result word discarded','value = ((ULONGLONG)response->value_high << 32) | response->value_low;','value = response->value_low;'],
 ['heartbeat progress ignored','query->silent_since = GetTickCount();\n        }','query->silent_since += 0;\n        }'],
 ['NULL result buffer accepted','(!data && size) || (flags & ~D3DGETDATA_FLUSH)','(flags & ~D3DGETDATA_FLUSH)'],
];
try{for(const [label,before,after] of cases){assert(source.includes(before),label);const file=dir+'/proxy.c';fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[__dirname+'/d9wg_query_contract_test.js'],{env:{...process.env,D9WG_QUERY_SOURCE:file},encoding:'utf8',timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,'Survived '+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+' compile failure: '+out);assert.match(out,/Query contract line|AddressSanitizer|runtime error:/,label+' must fail a runtime check: '+out);console.log('KILL '+label);}console.log('PASS native query contract mutations: '+cases.length+'/'+cases.length+'; compilation succeeds and runtime checks reject each fault');}finally{fs.rmSync(dir,{recursive:true,force:true});}
