"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-surface-mut-")),source=fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8");
const shared=fs.readFileSync(root+"/runtime/d9wg_backbuffer_upload.h","utf8");
function span(source,name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
const mutations=[
 ["destination overflow guard removed","copy_surface_rect","(UINT)point.x > to.Width || ",""],
 ["valid destination edge refused","copy_surface_rect","(UINT)(area.bottom - area.top) > to.Height - (UINT)point.y","(UINT)(area.bottom - area.top) >= to.Height - (UINT)point.y"],
 ["source boundary ignored","copy_surface_rect","(UINT)area.right > from.Width || ",""],
 ["GPU path falls through CPU mirror","copy_surface_rect","if (from.Pool == D3DPOOL_DEFAULT","if (0 && from.Pool == D3DPOOL_DEFAULT"],
 ["wrong GPU owner","copy_surface_rect","copy.device_handle = surface_device(source)->handle;","copy.device_handle = 0;"],
 ["wrong cube face","surface_gpu_location","*face = surface->face;","*face = 0;"],
 ["wrong cube CPU face","surface_texture_level","(D3DCUBEMAP_FACES)surface->face","(D3DCUBEMAP_FACES)0"],
 ["GPU write keeps stale mirror valid","copy_surface_rect","to_level->gpu_dirty = TRUE;","to_level->gpu_dirty = FALSE;"],
 ["skip source refresh","copy_surface_rect","HRESULT result = refresh_surface_pixels(source);","HRESULT result = D3D_OK;"],
 ["ignore failed source refresh","copy_surface_rect","if (FAILED(result))","if (0)"],
 ["CPU pools overwritten from GPU","refresh_surface_pixels","desc.Pool != D3DPOOL_DEFAULT || ",""],
 ["ignore GPU-written ordinary texture","refresh_surface_pixels","!(level && level->gpu_dirty)","TRUE"],
 ["refreshed mirror stays dirty","refresh_surface_pixels","level->gpu_dirty = FALSE;","level->gpu_dirty = TRUE;"],
 ["skip lock refresh","lock_surface_texture_level","result = refresh_surface_pixels(surface);","result = ((void)surface, D3D_OK);"],
 ["failed lock remains active","lock_surface_texture_level","level->locked = FALSE;","level->locked = TRUE;"],
 ["failed lock overwrites caller output","lock_surface_texture_level","level->locked = FALSE;","*out = locked; level->locked = FALSE;"],
 ["mandatory submission failure masked","submit_readback_batch_locked","return d9wg_transport_submit(g_process, present);","(void)present; return TRUE;"],
 ["transient back-buffer destination offset","d9wg_renderable_upload","copy.destination_top = area->top;","copy.destination_top = 0;"],
 ["transient upload ignores source stride","d9wg_renderable_upload","rows.source_pitch = pitch;","rows.source_pitch = ((void)pitch, rows.row_bytes);"],
 ["transient texture cleanup opcode","d9wg_renderable_upload","D9WG_OP_DESTROY_RESOURCE","D9WG_OP_SET_TEXTURE"],
 ["CPU upload omits rectangle origin","upload_surface_rect","pixels + area->top * pitch + area->left * pixel_bytes","pixels"],
 ["overlap uses live source","copy_surface_rect","if (input == output)","if (0)"],
 ["copy loses last byte","copy_surface_rect","UINT row_bytes = (area.right - area.left) * pixel_bytes","UINT row_bytes = (area.right - area.left) * pixel_bytes - 1"],
 ["zero-count bypasses format guard","device_copy_rects","source_desc.Format != destination_desc.Format || ",""],
 ["omitted points rejected","device_copy_rects","(count && !rects)","(count && (!rects || !points))"],
 ["foreign device accepted","device_copy_rects","if (surface_device(source) != device || surface_device(destination) != device)","if ((void)device, 0)"],
 ["front alpha stays transparent","device_get_front_buffer","output[y * destination_pitch + x * 4u + 3u] = 255;","output[y * destination_pitch + x * 4u + 3u] = 0;"],
 ["readonly Unlock uploads","unlock_texture_level","if (!(level_data->lock_flags & D3DLOCK_READONLY))","if (1)"],
 ["partial upload clears unknown pixels","copy_surface_rect","if (to_level && target.left == 0 && target.top == 0 && (UINT)target.right == to.Width && (UINT)target.bottom == to.Height)","if (to_level)"],
];
try{let killed=0;for(const[name,fn,old,replacement]of mutations){const isShared=fn==="d9wg_renderable_upload",original=isShared?shared:source;const[a,b]=span(original,fn),body=original.slice(a,b);assert(body.includes(old),name+" source match");const file=dir+"/candidate.c";let candidate=original.slice(0,a)+body.replace(old,replacement)+original.slice(b);if(isShared)candidate=candidate.replace(/#include "([^"]+)"/g,(_,p)=>'#include "'+path.resolve(root,"runtime",p)+'"');fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+"/d8wg_surface_copy_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,[isShared?"D8WG_SURFACE_UPLOAD_HEADER":"D8WG_SURFACE_SOURCE"]:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/D8 surface line|Assertion|signal=/.test(output),name+" no runtime failure");killed++;}console.log("PASS D3D8 surface mutations: "+killed+"/"+mutations.length+"; GPU identity, mirrors, exact CPU staging and overlap, lock/output/error semantics, CPU pools, bounds and opaque front readback");}finally{fs.rmSync(dir,{recursive:true,force:true});}
