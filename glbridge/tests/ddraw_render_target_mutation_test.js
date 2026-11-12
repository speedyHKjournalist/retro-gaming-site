"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/d3d7_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-target-mut-"));
const cases=[
 ["target without 3D caps accepted","d3d7_device_SetRenderTarget","!(caps & DDSCAPS_3DDEVICE) || surface->indexed","FALSE || surface->indexed"],
 ["indexed target accepted","d3d7_device_SetRenderTarget","|| surface->indexed","|| FALSE"],
 ["modern system target accepted","d3d7_device_SetRenderTarget","device->creation_version >= 7u && (caps & DDSCAPS_SYSTEMMEMORY)","FALSE"],
 ["invalid format target not assigned","d3d7_device_SetRenderTarget","surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);","if (invalid_format) return DDERR_INVALIDPIXELFORMAT; surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);"],
 ["legacy compensation reference leaked","d3d7_release_target","legacy_failed && surface->public_refs[DD_VERSION_1] > 0","FALSE && legacy_failed && surface->public_refs[DD_VERSION_1] > 0"],
 ["compensation applied to Device3","d3d7_device_SetRenderTarget","device->creation_version == 2u","device->creation_version <= 3u"],
 ["invalid target bound to GPU","d3d7_device_SetRenderTarget","!invalid_format && !d3d7_emit_render_target(device)","!d3d7_emit_render_target(device)"],
 ["failed target remains logical target","d3d7_device_SetRenderTarget","device->render_target = old;","device->render_target = surface;"],
 ["rollback loses invalid flag","d3d7_device_SetRenderTarget","device->render_target_invalid = old_invalid;","device->render_target_invalid = FALSE; (void)old_invalid;"],
 ["rollback loses compensation flag","d3d7_device_SetRenderTarget","device->legacy_failed_rt = old_failed;","device->legacy_failed_rt = FALSE;"],
 ["partial binding never repaired immediately","d3d7_device_SetRenderTarget","if (!old_invalid) (void)d3d7_emit_render_target(device);","(void)old_invalid;"],
 ["draw ignores binding debt","d3d7_prepare_legacy_draw","device->render_target_pending && !d3d7_emit_render_target(device)","FALSE"],
 ["Clear ignores binding debt","d3d7_device_Clear","device->render_target_pending && !d3d7_emit_render_target(device)","FALSE"],
 ["same-target retry ignores binding debt","d3d7_device_SetRenderTarget","device->render_target_pending && !d3d7_emit_render_target(device)","FALSE"],
 ["draw accepts invalid target","d3d7_prepare_legacy_draw","if (device->render_target_invalid) return DDERR_INVALIDPIXELFORMAT;","if (FALSE) return DDERR_INVALIDPIXELFORMAT;"],
 ["Clear accepts invalid target","d3d7_device_Clear","if (device->render_target_invalid) return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);","if (FALSE) return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);"],
 ["successful binding retains debt","d3d7_emit_render_target","device->render_target_pending = FALSE;","device->render_target_pending = TRUE;"],
];
try{for(const[label,name,from,to]of cases){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert.equal(body.split(from).length,2,label);const file=dir+"/proxy.inc";fs.writeFileSync(file,source.slice(0,m.index)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_render_target_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_RT_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/render target line/,label+": strict compilation and runtime rejection required");assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS render-target mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected at runtime");}finally{fs.rmSync(dir,{recursive:true,force:true});}
