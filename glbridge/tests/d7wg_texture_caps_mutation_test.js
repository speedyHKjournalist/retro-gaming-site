'use strict';const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.resolve(__dirname,'../ddrawproxy/d3d7_proxy.inc'),'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d7wg-caps-mut-'));
const rows=[
 ['modern RGB samples ordinary surfaces','device->creation_version >= 7u)','FALSE)'],
 ['legacy RGB loses ordinary textures','!guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice)','(!guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice) || guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice))'],
 ['hardware samples ordinary surfaces','!guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice)','(!guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice) && guid_equal(&device->device_guid, &IID_IDirect3DRGBDevice))'],
 ['null surface vtable accepted','if (surface_iface && view != &g_surface_vtbl','if (view && view != &g_surface_vtbl'],
 ['legacy surface views rejected','if (surface_iface && view != &g_surface_vtbl && view != &g_surface_vtbl1 &&\n            view != &g_surface_vtbl2 && view != &g_surface_vtbl3 &&\n            view != &g_surface_vtbl4)','if (surface_iface && view != &g_surface_vtbl)'],
 ['hardware query reference discarded','DDSurface *sampled = d3d7_sampled_texture(device, surface);','surface = d3d7_sampled_texture(device, surface);\n    DDSurface *sampled = surface;'],
 ['ordinary hardware surface uploaded','if (sampled) {','if (surface) {'],
 ['ignored texture call does not record unbind','        surface = NULL;','        return D3D_OK;'],
];
function bounds(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return [m.index,end];}
try{for(const[label,from,to]of rows){const [start,end]=bounds(from.startsWith('!guid_equal')?'d3d7_sampled_texture':'d3d7_device_SetTexture'),body=source.slice(start,end);assert.equal(body.split(from).length,2,label);const file=dir+'/mutated.inc';fs.writeFileSync(file,source.slice(0,start)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+'/d7wg_texture_caps_test.js'],{encoding:'utf8',env:{...process.env,D7WG_TEXTURE_CAPS_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/texture caps line/,label+': runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS texture caps mutations '+rows.length+'/'+rows.length+' compiled implementations rejected');}finally{fs.rmSync(dir,{recursive:true,force:true});}
