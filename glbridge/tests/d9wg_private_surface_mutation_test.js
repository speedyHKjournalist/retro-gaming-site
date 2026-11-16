"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-mut-"));


function run(name,api,file){const r=spawnSync(process.execPath,[__dirname+'/d9wg_private_surface_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,['D'+api+'WG_PRIVATE_SURFACE_SOURCE']:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' failed compile: '+output);assert(/Private surface|Assertion|signal=/.test(output),name+' no runtime failure');}
function mutate(source,fn,old,replacement){const m=new RegExp('static [^;{}\\n]*?\\b'+fn+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,fn);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}const part=source.slice(m.index,end);assert(part.includes(old),fn+': '+old);return source.slice(0,m.index)+part.replace(old,replacement)+source.slice(end);}
try{let killed=0,total=0;for(const api of[8,9]){const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),mutations=[
['surface final cleanup',api===9?'surface_destroy_storage':'surface_release','d9wg_private_clear(&surface->private_data, &d9wg_private_ops);','/* omitted */'],
['standalone fallback','surface_private_store','return &surface->private_data;','return NULL;'],
];if(api===8)mutations.push(
['device depth cleanup','device_release','d9wg_private_clear(&device->depth_surface_private_data, &d9wg_private_ops);','ZeroMemory(&device->depth_surface_private_data, sizeof(device->depth_surface_private_data));'],
['device back cleanup','device_release','d9wg_private_clear(&device->back_buffer_private_data, &d9wg_private_ops);','ZeroMemory(&device->back_buffer_private_data, sizeof(device->back_buffer_private_data));'],
['implicit views use temporary stores','create_backbuffer_surface','surface_from_iface(*surface_out)->private_data_owner = &device->back_buffer_private_data;','/* omitted */'],
['chain backing reference leaked','swapchain_release','IDirect3DTexture8_Release(&surface->texture->iface);','(void)surface;'],
['chain shares device store','swapchain_get_backbuffer','    *surface_out = chain->back_buffer;','    surface_from_iface(chain->back_buffer)->private_data_owner = &chain->device->back_buffer_private_data;\n    *surface_out = chain->back_buffer;'],
['chain view reference missing','swapchain_get_backbuffer','IDirect3DSurface8_AddRef(*surface_out);','(void)surface_add_ref;'],
['chain eagerly clears live texture store','swapchain_release','IDirect3DTexture8_Release(&surface->texture->iface);','d9wg_private_clear(&surface->texture->levels[0].private_data, &d9wg_private_ops); IDirect3DTexture8_Release(&surface->texture->iface);'],
['chain allocation leaked','swapchain_destroy_storage','HeapFree(GetProcessHeap(), 0, chain);','/* omitted */'],
['target allocation failure ignored','device_create_swapchain','if (FAILED(hr))','if (0 && FAILED(hr))'],
['missing backing GPU handle','device_create_swapchain','command.back_buffer_handle = surface->texture ? surface->texture->handle : 0;','command.back_buffer_handle = 0; (void)surface;']
);else for(const field of['implicit_back_buffer','implicit_depth_stencil'])mutations.push([field+' cleanup','device_release','d9wg_private_clear(&device->'+field+'->private_data, &d9wg_private_ops);','ZeroMemory(&device->'+field+'->private_data, sizeof(device->'+field+'->private_data));']);total+=mutations.length;for(const[name,fn,old,replacement]of mutations){const file=dir+'/candidate.c';fs.writeFileSync(file,mutate(source,fn,old,replacement));run('API '+api+' '+name,api,file);killed++;}}
console.log('PASS surface private-data mutations: '+killed+'/'+total+'; standalone and device cleanup, separate back-buffer stores, early chain release, cached texture views, backing lifetime and allocation failures');}finally{fs.rmSync(dir,{recursive:true,force:true});}
