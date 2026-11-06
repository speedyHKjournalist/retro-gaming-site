'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),sources={guest:fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),header:fs.readFileSync(root+'/runtime/d9wg_mapped_draw_bridge.h','utf8')};
const cases=[
 ['initial upload only covers first lock','header','emit_buffer_update(handle, 0, shadow, length, 0)','emit_buffer_update(handle, 0, shadow, length > 64 ? 64 : length, 0)'],
 ['residency remains pending after success','header','    *pending = FALSE;','    *pending = TRUE;'],
 ['failed initial upload loses retry','header','if (!emit_buffer_update(handle, 0, shadow, length, 0)) return FALSE;','if (!emit_buffer_update(handle, 0, shadow, length, 0)) { *pending = FALSE; return FALSE; }'],
 ['ordinary unlock no longer uploads','guest','result = buffer->full_upload_pending','result = TRUE || buffer->full_upload_pending'],
 ['eviction fails to reload VB','guest','if (buffer->pool == D3DPOOL_MANAGED) buffer->full_upload_pending = TRUE;','if (buffer->pool == D3DPOOL_MANAGED) buffer->full_upload_pending = FALSE;'],
 ['non-indexed draw uploads IB','header','    if (indexed) {','    if (indexed || device->index_buffer) {'],
];
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-managed-mutants-'));try{for(const[label,kind,before,after]of cases){assert(sources[kind].includes(before),label);const file=dir+'/'+kind+'.h';fs.writeFileSync(file,sources[kind].replace(before,after));const env={...process.env,[kind==='guest'?'D9WG_MANAGED_BUFFER_SOURCE':'D9WG_MANAGED_BUFFER_HEADER']:file};const r=spawnSync(process.execPath,[path.join(__dirname,'d9wg_managed_buffer_test.js')],{env,encoding:'utf8'});assert.notEqual(r.status,0,'survived '+label);assert.match(r.stderr,/managed buffer line/,'runtime assertion required '+label);assert(!r.stderr.includes('error:'),'compile failure '+label);}console.log('PASS managed buffer mutations: '+cases.length+'/'+cases.length+' residency/range/retry/eviction/isolation faults rejected by actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
