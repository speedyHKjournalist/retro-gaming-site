'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-implicit-mutations-'));
function span(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return[m.index,end];}
try{let killed=0;for(const api of[8,9]){const source=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),changes=api===8?[
 ['device_get_backbuffer','back cache ignored','if (device->implicit_back_buffer) {','if (0 && device->implicit_back_buffer) {'],
 ['device_get_depth_surface','depth cache ignored','if (device->implicit_depth_stencil) {','if (0 && device->implicit_depth_stencil) {'],
 ['device_get_backbuffer','back cache unpublished','device->implicit_back_buffer = surface_from_iface(*surface_out);','(void)surface_out;'],
 ['device_get_depth_surface','depth cache unpublished','device->implicit_depth_stencil = surface_from_iface(*surface_out);','(void)surface_out;'],
 ['device_get_render_target','target allocates independent wrapper','return device_get_backbuffer(iface, 0, D3DBACKBUFFER_TYPE_MONO, surface_out);','return create_backbuffer_surface(device, &device->present, TRUE, surface_out);'],
 ['free_implicit_surfaces','back wrapper leaked','HeapFree(GetProcessHeap(), 0, back);','(void)back;'],
 ['free_implicit_surfaces','depth wrapper leaked','HeapFree(GetProcessHeap(), 0, depth);','(void)depth;'],
 ['surface_add_ref','each public AddRef pins device','if (refs == 1 && surface_is_implicit(surface))','if (refs && surface_is_implicit(surface))'],
 ['surface_release','each public Release drops device','if (!refs) {\n            D8Device *device = surface->device;','if (1) {\n            D8Device *device = surface->device;'],
 ['surface_release','zero Release underflows','if (!InterlockedCompareExchange(&surface->refcount, 0, 0))','if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0))'],
 ['device_has_reset_blockers','external depth refs ignored','(UINT)device->implicit_depth_stencil->refcount, 0,','0, 0,'],
 ['device_has_reset_blockers','private binding discounted from public refs','(UINT)device->implicit_depth_stencil->refcount, 0,','(UINT)device->implicit_depth_stencil->refcount, device->depth_surface == device->implicit_depth_stencil ? 1u : 0u,'],
 ['device_get_backbuffer','stereo selector rejected','if (index)','if (index || type)'],
 ['surface_is_implicit','requires both cache identities','|| surface == surface->device->implicit_depth_stencil','&& surface == surface->device->implicit_depth_stencil'],
]:[
 ['surface_add_ref','each public AddRef pins device','if (refs == 1)','if (refs)'],
 ['surface_release','each public Release drops device','if (!refs)\n            device_child_release(device);','if (1)\n            device_child_release(device);'],
 ['surface_release','zero Release underflows','if (!InterlockedCompareExchange(&surface->refcount, 0, 0)) {','if (0 && !InterlockedCompareExchange(&surface->refcount, 0, 0)) {'],
 ['device_get_back_buffer','stereo selector rejected','if (swapchain || index) {','if (swapchain || index || type) {'],
 ['surface_get_container','auto-depth container missing','if (surface->auto_depth_stencil || !surface->texture_child) {','if (!surface->auto_depth_stencil && !surface->texture_child) {'],
 ['surface_get_container','auto-depth wrong identity','*container = &surface->device->iface;','*container = &surface->iface;'],
 ['surface_get_container','auto-depth container unretained','IDirect3DDevice9_AddRef(&surface->device->iface);','(void)surface->device;'],
 ['device_get_back_buffer','back cache unpublished','device->implicit_back_buffer = surface;','(void)surface;'],
 ['device_get_depth_stencil_surface','depth cache unpublished','device->implicit_depth_stencil = surface;','(void)surface;'],
];for(const[fn,label,old,replacement]of changes){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(old),'API '+api+' '+label);let edited=body.replace(old,replacement);if((api===8&&fn==='surface_release')||(api===9&&(fn==='surface_add_ref'||fn==='surface_release'))){const implicit=body.indexOf('    if (surface_is_implicit(surface)) {');assert(implicit>=0);assert(body.slice(implicit).includes(old));edited=body.slice(0,implicit)+body.slice(implicit).replace(old,replacement);}const file=dir+'/frontend.c';fs.writeFileSync(file,source.slice(0,a)+edited+source.slice(b));const r=spawnSync(process.execPath,[__dirname+'/d9wg_implicit_identity_test.js'],{encoding:'utf8',timeout:30000,env:{...process.env,['D'+api+'WG_IMPLICIT_FRONTEND']:file}}),output=r.stdout+r.stderr;assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');assert(!/error:|undefined reference|Undefined symbols/.test(output),label+' compile failure: '+output);assert(/Implicit identity API|Assertion|signal=/.test(output),label+' no runtime failure');killed++;}}
console.log('PASS implicit identity mutations: '+killed+'/23; cache publication/lookup/teardown, shared target identity, zero/nonzero device pins, zero guards, stereo selectors, depth container and device-owned Reset distinction');}finally{fs.rmSync(dir,{recursive:true,force:true});}
