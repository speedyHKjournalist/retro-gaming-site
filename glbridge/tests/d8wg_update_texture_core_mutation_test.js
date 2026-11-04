"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"update8-mut-"));
const paths={policy:root+"/runtime/d9wg_texture_copy_policy.h",eight:root+"/d3d8proxy/d3d8_proxy.c",nine:root+"/d3d9proxy/d3d9_proxy.c"},source=Object.fromEntries(Object.entries(paths).map(([k,p])=>[k,fs.readFileSync(p,"utf8")]));
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[
 ['source pool','policy','d9wg_texture_copy_pools','source == 2u','source == 1u'],
 ['destination pool','policy','d9wg_texture_copy_pools','destination == 0u','destination == 1u'],
 ['both pools','policy','d9wg_texture_copy_pools','&&','||'],
 ...['update_cube_texture','update_volume_texture','device_update_texture'].map(n=>['8 pool '+n,'eight',n,'!d9wg_texture_copy_pools(source->pool, destination->pool)','0']),
 ['9 2D pool','nine','device_update_texture','!d9wg_texture_copy_pools(source->pool, destination->pool)','0'],
 ['9 volume pool','nine','device_update_texture','!d9wg_texture_copy_pools(from->pool, to->pool)','0'],
 ['9 cube pool','nine','device_update_texture','!d9wg_texture_copy_pools(from->pool, to->pool)','0'],
 ['private profile','nine','d9wg_d3d9_core_update_texture8','!= 8u','!= 9u'],
 ['private source','nine','d9wg_d3d9_core_update_texture8','iface, source, destination','iface, destination, source'],
 ['private destination','nine','d9wg_d3d9_core_update_texture8','iface, source, destination','iface, source, ((void)destination, source)'],
 ['private result','nine','d9wg_d3d9_core_update_texture8','return IDirect3DDevice9_UpdateTexture(iface, source, destination);','return IDirect3DDevice9_UpdateTexture(iface, source, destination) ^ 1;'],
 ['8 ownership','eight','update_cube_texture','source->device != device','(source->device != device && 0)'],
 ['8 format','eight','update_volume_texture','source->format != destination->format','(source->format != destination->format && 0)'],
 ['8 extent','eight','device_update_texture','source->width, source->height, 1, source->level_count','source->width + 1u, source->height, 1, source->level_count'],
 ['9 volume extent','nine','device_update_texture','from->width, from->height, from->depth, from->level_count','from->width, from->height, from->depth + 1u, from->level_count']
];
try{for(const[label,key,name,from,to]of cases){const s=source[key],[a,b,f]=body(s,name);assert(f.includes(from),label);const file=dir+'/mutation.'+(key==='policy'?'h':'c');fs.writeFileSync(file,s.slice(0,a)+(label==='9 cube pool'?f.slice(0,f.lastIndexOf(from))+to+f.slice(f.lastIndexOf(from)+from.length):f.replace(from,to))+s.slice(b));const env={...process.env,[key==='policy'?'D8WG_UPDATE_POLICY_HEADER':key==='eight'?'D8WG_UPDATE_SOURCE8':'D8WG_UPDATE_SOURCE9']:file},r=spawnSync(process.execPath,[root+'/tests/d8wg_update_texture_core_test.js'],{env,encoding:'utf8',timeout:180000}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+' survived/abnormal '+out);assert(!/error:|undefined reference|Undefined symbols|errors generated/.test(out),label+' compilation failure is notruntime kill '+out);assert(/Update8 core case \d+ line \d+:/.test(out),label+' missing actual C assertion '+out);}
 console.log(`PASS UpdateTexture policy/private8 mutations: ${cases.length}/${cases.length};strict compile then C runtime pool/type/owner/format/extent/profile/dispatch/result assertions`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
