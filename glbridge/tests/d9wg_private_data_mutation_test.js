"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-mut-")),source=fs.readFileSync(root+'/runtime/d9wg_private_data.c','utf8');
const mutations=[
 ['truncated copy','i < size;','i + 1 < size;'],
 ['all GUIDs collide','if (i == 16)','if (i <= 16)'],
 ['missing data wrong error','return D9WG_PRIVATE_NOT_FOUND;','return D9WG_PRIVATE_INVALID;'],
 ['small output succeeds','result = D9WG_PRIVATE_MORE_DATA;','result = D9WG_PRIVATE_OK;'],
 ['query returns more data','if (data) {','if (data || capacity < entry->size) {'],
 ['required size not reported','copy_bytes(size, &entry->size, sizeof(entry->size));','copy_bytes(size, &capacity, sizeof(capacity));'],
 ['Get does not retain interface','ops->retain_interface(ops->context, entry->object);\n            copy_bytes','/* no retain */\n            copy_bytes'],
 ['Set does not retain interface','ops->retain_interface(ops->context, entry->object);\n    } else','/* no retain */\n    } else'],
 ['Get pin omitted','++entry->references;','/* pin omitted */'],
 ['release does not drop interface','ops->release_interface(ops->context, entry->object);','(void)entry->object;'],
 ['invalid IUnknown size accepted','size != sizeof(void*)','size == UINT32_MAX'],
 ['raw data no longer copied','copy_bytes(entry->bytes, data, size);','(void)data;'],
 ['zero bytes refused','(!data && size)','(!data)'],
 ['allocation failure wrong error','if (!entry)\n        return D9WG_PRIVATE_OUT_OF_MEMORY;','if (!entry)\n        return D9WG_PRIVATE_INVALID;'],
 ['clear leaves owned entries','store->head = NULL;','store->head = NULL; return;'],
];
function run(name,test,env){const r=spawnSync(process.execPath,[__dirname+'/'+test],{encoding:'utf8',timeout:30000,env:{...process.env,...env}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' failed compile: '+output);assert(/Private data line \d+:|Private entry API [89] line \d+:|Assertion failed: \([^\n]*\), function \w+, file (?:private|entry)\.c, line \d+\.|(?:private|entry)\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(output),name+' requires an actual C semantic assertion');}
try{let killed=0;for(const[name,old,replacement]of mutations){assert(source.includes(old),name);const file=dir+'/candidate.c';fs.writeFileSync(file,source.replace(old,replacement));run(name,'d9wg_private_data_test.js',{D9WG_PRIVATE_STORE_SOURCE:file});killed++;}for(const api of[8,9])for(const kind of['vb','ib']){const original=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),start=original.indexOf(api===9?'static void '+kind+'_destroy_storage(D9'+(kind==='vb'?'VertexBuffer':'IndexBuffer')+' *buffer)\n{':'static ULONG WINAPI '+kind+'_release('),at=original.indexOf('d9wg_private_clear(&buffer->private_data, &d9wg_private_ops);',start);assert(at>start);const file=dir+'/entry.c';fs.writeFileSync(file,original.slice(0,at)+original.slice(at).replace('d9wg_private_clear(&buffer->private_data, &d9wg_private_ops);','/* leaked data */'));run('API '+api+' '+kind+' cleanup','d9wg_private_data_entry_test.js',{['D'+api+'WG_PRIVATE_SOURCE']:file});killed++;}console.log('PASS shared private data mutations: '+killed+'/'+(mutations.length+4)+'; bytes, GUIDs, errors, size/NULL, pins, interface refs and all four actual final Release paths');}finally{fs.rmSync(dir,{recursive:true,force:true});}
