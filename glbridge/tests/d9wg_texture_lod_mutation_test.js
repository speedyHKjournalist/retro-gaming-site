"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-lod-mut-")),header=fs.readFileSync(root+"/runtime/d9wg_texture_lod_bridge.h","utf8");
const mutations=[
 ["non-managed pools accepted","pool != D3DPOOL_MANAGED","(0 && pool != D3DPOOL_MANAGED)"],
 ["managed pool excluded","pool != D3DPOOL_MANAGED","pool == D3DPOOL_MANAGED"],
 ["equal-to-count not clamped","requested >= level_count","requested > level_count"],
 ["LOD clamped past last level","requested = level_count - 1;","requested = level_count;"],
 ["local state not updated","*stored_lod = requested;","(void)stored_lod;"],
 ["wire device lost","command.device_handle = device_handle;","command.device_handle = 0; (void)device_handle;"],
 ["wire resource lost","command.resource_handle = resource_handle;","command.resource_handle = 0; (void)resource_handle;"],
 ["wire carries previous LOD","command.lod = requested;","command.lod = previous;"],
 ["reserved word nonzero","command.reserved = 0;","command.reserved = 1;"],
 ["wrong return value","return previous;","return requested; (void)previous;"],
 ["same value cannot retry","*stored_lod = requested;","if (requested == previous) return previous; *stored_lod = requested;"],
 ["failure rolls back local state","emit_command(D9WG_OP_SET_TEXTURE_LOD, &command, sizeof(command));","if (!emit_command(D9WG_OP_SET_TEXTURE_LOD, &command, sizeof(command))) *stored_lod = previous;"],
];
function run(name,env){const r=spawnSync(process.execPath,[__dirname+"/d9wg_texture_lod_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,...env}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/LOD API|Assertion|signal=/.test(output),name+" no runtime failure");}
try{let killed=0;for(const[name,old,replacement]of mutations){assert(header.includes(old),name);const file=dir+"/candidate.h";fs.writeFileSync(file,header.replace(old,replacement));run(name,{D9WG_LOD_HEADER:file});killed++;}for(const api of [8,9])for(const name of ['texture_set_lod','cube_set_lod','volume_texture_set_lod']){const source=fs.readFileSync(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),m=new RegExp('static DWORD WINAPI '+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m);const end=source.indexOf('\n}',m.index)+2,body=source.slice(m.index,end);assert(body.includes('texture->pool'));const file=dir+'/candidate.c';fs.writeFileSync(file,source.slice(0,m.index)+body.replace('texture->pool','D3DPOOL_MANAGED == 1 ? 0 : texture->pool')+source.slice(end));run('API '+api+' '+name+' drops LOD',{['D'+api+'WG_LOD_SOURCE']:file});killed++;}console.log('PASS shared texture LOD mutations: '+killed+'/'+(mutations.length+6)+'; pool semantics, clamp, local/wire/previous state, same-value retry and six actual API entry points');}finally{fs.rmSync(dir,{recursive:true,force:true});}
