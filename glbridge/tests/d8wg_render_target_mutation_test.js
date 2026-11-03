"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-target-mut-")),source=fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8");
function span(name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
const mutations=[
 ["color uses back buffer","device_set_render_target","command.device_handle = device->handle;","command.color_texture_handle = 0; command.device_handle = device->handle;"],
 ["wrong cube wire face","surface_gpu_location","*face = surface->face;","*face = 0;"],
 ["wrong cube wire mip","surface_gpu_location","*level = surface->level;","*level = 0;"],
 ["bound cube CPU level ignores face","bound_render_target_level","(D3DCUBEMAP_FACES)device->render_target_face","(D3DCUBEMAP_FACES)0"],
 ["cube not retained","device_set_render_target","device->render_target_cube = cube;","device->render_target_cube = NULL;"],
 ["cube viewport uses display size","current_render_target_size","D8TextureLevel* level = bound_render_target_level(device);","D8TextureLevel* level = NULL; (void)bound_render_target_level(device);"],
 ["NULL color rejected","device_set_render_target","D8Device* device = device_from_iface(iface);","D8Device* device = device_from_iface(iface); if (!render_target_iface) return D3DERR_INVALIDCALL;"],
 ["NULL color resets viewport","device_set_render_target","if (!render_target)\n        return D3D_OK;",""],
 ["NULL color emits back buffer bind","device_set_render_target","if (render_target) {\n        command.device_handle","if (1) {\n        command.device_handle"],
 ["depth-only validates display size","device_set_render_target","current_render_target_size(device, &render_target_width, &render_target_height);","(render_target_width = device->display_mode.Width, render_target_height = device->display_mode.Height);"],
 ["larger depth refused","device_set_render_target","depth_width < render_target_width || depth_height < render_target_height","depth_width != render_target_width || depth_height != render_target_height"],
 ["explicit depth aliases auto","device_set_render_target","depth->texture->handle","D9WG_AUTO_DEPTH_STENCIL_HANDLE"],
 ["wrong depth height","device_set_render_target","depth_command.height = depth_height;","depth_command.height = depth_width;"],
 ["depth state remains enabled","device_set_render_target","device->depth_surface_enabled = depth != NULL;","device->depth_surface_enabled = TRUE;"],
 ["cube getter returns back buffer","device_get_render_target","if (device->render_target_cube)","if (0)"],
 ["cube getter wrong face","device_get_render_target","(D3DCUBEMAP_FACES)device->render_target_face","(D3DCUBEMAP_FACES)0"],
 ["cleanup leaks cube ref","clear_render_target_binding","if (cube)\n        IDirect3DCubeTexture8_Release(&cube->iface);","if (0)\n        IDirect3DCubeTexture8_Release(&cube->iface);"],
 ["viewport reset omitted","device_set_render_target","set_live_viewport(device, &viewport)","((void)viewport, TRUE)"],
 ["live viewport accidentally recorded","device_set_render_target","set_live_viewport(device, &viewport)","(device_set_viewport(iface, &viewport) == D3D_OK)"],
 ["missing depth wrong HRESULT","device_get_depth_surface","D3DERR_NOTFOUND","D3DERR_INVALIDCALL"],
 ["missing depth leaves caller pointer","device_get_depth_surface","*surface_out = NULL;","*surface_out = (void*)123;"],
 ["cube released before acquiring same ref","device_set_render_target","if (texture)\n            IDirect3DTexture8_AddRef(&texture->iface);","clear_render_target_binding(device); if (texture)\n            IDirect3DTexture8_AddRef(&texture->iface);"],
 ["CPU surface accepted as target","device_set_render_target","!(desc.Usage & D3DUSAGE_RENDERTARGET) || ",""],
 ["depth too small accepted","device_set_render_target","if (depth && (depth_width < render_target_width || depth_height < render_target_height))","if (0)"],
];
try{let killed=0;for(const[name,fn,old,replacement]of mutations){const[a,b]=span(fn),body=source.slice(a,b);assert(body.includes(old),name+" source match");let candidate=body.replace(old,replacement);if(name==='cube released before acquiring same ref')candidate=candidate.replace('        clear_render_target_binding(device);\n        device->render_target_texture','        device->render_target_texture');if(name==='CPU surface accepted as target')candidate=candidate.replace('desc.Pool != D3DPOOL_DEFAULT || ','').replace('!surface_gpu_location(render_target, &command.color_texture_handle, &command.color_level, &command.color_face)','(surface_gpu_location(render_target, &command.color_texture_handle, &command.color_level, &command.color_face), 0)');const file=dir+"/candidate.c";fs.writeFileSync(file,source.slice(0,a)+candidate+source.slice(b));const r=spawnSync(process.execPath,[__dirname+"/d8wg_render_target_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,D8WG_TARGET_SOURCE:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/D8 target line|Assertion|signal=/.test(output),name+" no runtime failure");killed++;}console.log("PASS D3D8 render-target mutations: "+killed+"/"+mutations.length+"; cube identity and lifetime, NULL color/depth, live/recorded viewport, exact depth metadata, validation and getters");}finally{fs.rmSync(dir,{recursive:true,force:true});}
