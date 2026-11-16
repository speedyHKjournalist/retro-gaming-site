'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-memory-graph-mut-'));
const rows=[
 ['implicit texture rejected','(surface->implicit &&\n             !(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))','surface->implicit'],
 ['shared mip graph treated as pixel aliases','surface->memory->ref > 1','surface->storage->ref > 1'],
 ['duplicate aliases overwritten','surface->memory->ref > 1','FALSE'],
 ['root graph replacement rejected','if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE) &&\n            (surface->mip_next','if (TRUE &&\n            (surface->mip_next'],
 ['detached child level left logical','return surface->mip_level - surface->storage->mip_base;','return surface->mip_level;'],
 ['detached cube face left logical','return surface->storage->resource_kind == D9WG_RESOURCE_TEXTURE_CUBE\n        ? surface->cube_face : 0u;','return surface->cube_face;'],
 ['detached level base omitted','new_storage->mip_base = surface->mip_level;','new_storage->mip_base = 0u;'],
 ['old graph reference leaked','if (new_storage) surface_storage_release(old_storage);','if (!new_storage) surface_storage_release(old_storage);'],
 ['client memory freed as owned','bytes, FALSE);','bytes, TRUE);'],
 ['peer link severed','surface->external_memory = TRUE;','surface->external_memory = TRUE; surface->mip_next = NULL;'],
];
try{for(const[label,from,to]of rows){assert.equal(source.split(from).length,2,label);const file=dir+'/source.c';fs.writeFileSync(file,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+'/ddraw_memory_graph_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_GRAPH_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/memory graph line/,label+': runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS memory graph mutations '+rows.length+'/'+rows.length+' compiled implementations rejected');}finally{fs.rmSync(dir,{recursive:true,force:true});}
