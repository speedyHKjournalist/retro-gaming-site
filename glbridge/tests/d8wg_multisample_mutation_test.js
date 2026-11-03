"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-msaa-mut-")),source=fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8");
function span(name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
const mutations=[
 ["missing type in first create","create_texture_internal","texture->multisample_type = ms;","texture->multisample_type = 0;"],
 ["duplicate CREATE","create_texture_internal","if (!emit_texture_create(device, texture))", "if (!emit_texture_create(device, texture) || !emit_texture_create(device, texture))"],
 ["RT constructor forces no MSAA","device_create_render_target","D3DPOOL_DEFAULT, ms, &texture_iface", "D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, &texture_iface"],
 ["wire loses MSAA","emit_texture_create","command.multisample_type = texture->multisample_type;","command.multisample_type = 0;"],
 ["wire wrong quality","emit_texture_create","command.multisample_quality = 0;","command.multisample_quality = 1;"],
 ["texture descriptor loses MSAA","texture_get_level_desc","desc->MultiSampleType = texture->multisample_type;","desc->MultiSampleType = 0;"],
 ["back descriptor loses MSAA","create_backbuffer_surface","desc.MultiSampleType = present->MultiSampleType;","desc.MultiSampleType = 0;"],
 ["implicit depth descriptor loses MSAA","device_get_depth_surface","desc.MultiSampleType = device->present.MultiSampleType;","desc.MultiSampleType = 0;"],
 ["implicit depth uses display width","device_get_depth_surface","width = device->present.BackBufferWidth ? device->present.BackBufferWidth : device->display_mode.Width;","width = device->display_mode.Width;"],
 ["implicit depth uses display height","device_get_depth_surface","height = device->present.BackBufferHeight ? device->present.BackBufferHeight : device->display_mode.Height;","height = device->display_mode.Height;"],
 ["allocation cleanup leaks child ref","create_texture_internal","    device_child_release(device);", "    (void)device;"],
 ["allocation cleanup leaks level memory","create_texture_internal","HeapFree(GetProcessHeap(), 0, texture->levels);", "(void)texture->levels;"],
 ["allocation cleanup leaks shadow","create_texture_internal","HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);", "(void)texture->levels[level].shadow;"],
 ["create transport failure succeeds","create_texture_internal","if (!emit_texture_create(device, texture))","if ((emit_texture_create(device, texture), 0))"],
 ["surface conversion leaks original parent ref","device_create_render_target","IDirect3DTexture8_Release(texture_iface);","(void)texture_iface;"],
 ["lockable flag ignored","device_create_render_target","lockable_render_target = !!lockable;","lockable_render_target = FALSE;"],
 ["lockable4 accepted","device_create_render_target","if (lockable && ms != D3DMULTISAMPLE_NONE)","if (0)"],
];
try{let killed=0;for(const[name,fn,old,replacement]of mutations){const[a,b]=span(fn),body=source.slice(a,b);assert(body.includes(old),name+" source match");let candidate=body.replace(old,replacement);const file=dir+"/candidate.c";fs.writeFileSync(file,source.slice(0,a)+candidate+source.slice(b));const r=spawnSync(process.execPath,[__dirname+"/d8wg_multisample_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,D8WG_MSAA_SOURCE:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/D8 MSAA line|Assertion|signal=/.test(output),name+" no runtime failure");killed++;}console.log("PASS D3D8 MSAA mutations: "+killed+"/"+mutations.length+"; single creation, descriptors, implicit extents, allocation and transport cleanup");}finally{fs.rmSync(dir,{recursive:true,force:true});}
