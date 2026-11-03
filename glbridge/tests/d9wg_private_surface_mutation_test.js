"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-mut-"));


function run(name,api,file){const r=spawnSync(process.execPath,[__dirname+'/d9wg_private_surface_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,['D'+api+'WG_PRIVATE_SURFACE_SOURCE']:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' failed compile: '+output);assert(/Private surface|Assertion|signal=/.test(output),name+' no runtime failure');}
function mutate(source,fn,old,replacement){const m=new RegExp('static [^;{}\\n]*?\\b'+fn+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,fn);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}const part=source.slice(m.index,end);assert(part.includes(old),fn+': '+old);return source.slice(0,m.index)+part.replace(old,replacement)+source.slice(end);}
try{let killed=0,total=0;for(const api of[8,9]){const source=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),mutations=[
['surface final cleanup','surface_release','d9wg_private_clear(&surface->private_data, &d9wg_private_ops);','/* omitted */'],
['standalone fallback','surface_private_store','return &surface->private_data;','return NULL;'],
];if(api===8)mutations.push(
['device depth cleanup','device_release','d9wg_private_clear(&device->depth_surface_private_data, &d9wg_private_ops);','/* omitted */'],
['device back cleanup','device_release','d9wg_private_clear(&device->back_buffer_private_data, &d9wg_private_ops);','/* omitted */'],
['owner clear','private_data_owner_release','d9wg_private_clear(&owner->store, &d9wg_private_ops);','/* omitted */'],
['owner allocation leaked','private_data_owner_release','HeapFree(GetProcessHeap(), 0, owner);','/* omitted */'],
['owner final reference never destroyed','private_data_owner_release','if (!InterlockedDecrement(&owner->refs))','if (0 && !InterlockedDecrement(&owner->refs))'],
['chain owner reference leaked','swapchain_release','private_data_owner_release(chain->private_data_owner);','/* omitted */'],
['surface owner reference leaked','surface_release','private_data_owner_release(surface->retained_private_data);','(void)surface->retained_private_data;'],
['implicit views use temporary stores','create_backbuffer_surface','surface_from_iface(*surface_out)->private_data_owner = &device->back_buffer_private_data;','/* omitted */'],
['chain shares device store','swapchain_get_backbuffer','surface->private_data_owner = &chain->private_data_owner->store;','surface->private_data_owner = &chain->device->back_buffer_private_data;'],
['chain view pin missing','swapchain_get_backbuffer','InterlockedIncrement(&chain->private_data_owner->refs);','(void)InterlockedIncrement;'],
['chain eagerly clears live views','swapchain_release','private_data_owner_release(chain->private_data_owner);','d9wg_private_clear(&chain->private_data_owner->store, &d9wg_private_ops); private_data_owner_release(chain->private_data_owner);'],
['owner initial reference absent','device_create_swapchain','chain->private_data_owner->refs = 1;','chain->private_data_owner->refs = 0;'],
['owner OOM dereferences NULL','device_create_swapchain','if (!chain->private_data_owner)','if (0 && !chain->private_data_owner)']
);else for(const field of['implicit_back_buffer','implicit_depth_stencil'])mutations.push([field+' cleanup','device_release','d9wg_private_clear(&device->'+field+'->private_data, &d9wg_private_ops);','/* omitted */']);total+=mutations.length;for(const[name,fn,old,replacement]of mutations){const file=dir+'/candidate.c';fs.writeFileSync(file,mutate(source,fn,old,replacement));run('API '+api+' '+name,api,file);killed++;}}
console.log('PASS surface private-data mutations: '+killed+'/'+total+'; standalone and device cleanup, separate back-buffer stores, early chain release, view pins, owner allocation and OOM paths');}finally{fs.rmSync(dir,{recursive:true,force:true});}
