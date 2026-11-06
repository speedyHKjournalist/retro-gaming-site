'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'memory-mut-'));
const cases=[
 ['managed omitted','if (t->pool <= D3DPOOL_MANAGED)','if (t->pool < D3DPOOL_MANAGED)'],
 ['CPU pools charged','if (t->pool <= D3DPOOL_MANAGED)','if (1)'],
 ['cube faces omitted','6u * texture_memory_estimate','1u * texture_memory_estimate'],
 ['volume depth omitted','t->width, t->height, t->depth,','t->width, t->height, 1,'],
 ['vertex buffers omitted','if (v->pool <= D3DPOOL_MANAGED) used += v->length;','if (v->pool <= D3DPOOL_MANAGED) used += 0;'],
 ['uint32 usage wraps','uint64_t used = 0;','UINT used = 0;'],
 ['budget underflows','used >= budget ? 0u : (UINT)(budget - used)','0 ? 0u : (UINT)(budget - used)'],
 ['subMiB usage rounded up','& ~((1u << 20) - 1u)','& 0xffffffffu'],
 ['mip chain absent','level < levels','level < (levels ? 1u : 0u)'],
 ['autogen chain absent','if ((usage & D3DUSAGE_AUTOGENMIPMAP)','if (0 && (usage & D3DUSAGE_AUTOGENMIPMAP)'],
 ['depth mip not reduced','if (depth > 1) depth >>= 1;','if (0 && depth > 1) depth >>= 1;'],
 ['MSAA omitted','samples > 1 ? samples : 1u','samples > 1 ? 1u : 1u'],
 ['block-rounded width truncated','(uint64_t)(width / bw + (width % bw != 0))','(uint64_t)(width / bw)'],
];
try{for(const[label,before,after]of cases){assert(source.includes(before),label);const file=dir+'/candidate.c';fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[__dirname+'/d9wg_memory_budget_test.js'],{env:{...process.env,D9WG_MEMORY_SOURCE:file},encoding:'utf8',timeout:30000});assert(!r.error,label);assert.notEqual(r.status,0,label+' survived');assert(!/error:|undefined reference|Undefined symbols/.test(r.stdout+r.stderr),label+' compile error '+r.stderr);assert.match(r.stdout+r.stderr,/Memory budget line/,label);}console.log('PASS memory budget mutations: '+cases.length+'/'+cases.length+' compiled faults rejected by runtime accounting assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
